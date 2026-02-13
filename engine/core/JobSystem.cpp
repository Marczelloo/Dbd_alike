#include "engine/core/JobSystem.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>

namespace engine::core
{

thread_local std::size_t JobSystem::t_workerIndex = static_cast<std::size_t>(-1);

bool JobSystem::Initialize(std::size_t workerCount)
{
    if (m_initialized)
    {
        return true;
    }

    if (workerCount == 0)
    {
        const auto hardware = std::thread::hardware_concurrency();
        workerCount = hardware > 1 ? hardware - 1 : 1;
    }

    workerCount = std::max<std::size_t>(1, workerCount);

    m_shutdown = false;
    m_activeJobs = 0;
    m_completedJobs = 0;
    m_nextJobId = 1;
    m_busyWorkerTimeNs = 0;
    m_statsLastSampleBusyNs = 0;
    m_statsLastSampleTimeNs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());

    m_workers.reserve(workerCount);
    for (std::size_t i = 0; i < workerCount; ++i)
    {
        m_workers.emplace_back(&JobSystem::WorkerThread, this, i);
    }

    m_initialized = true;
    std::cout << "[JobSystem] Initialized with " << workerCount << " workers\n";
    return true;
}

void JobSystem::Shutdown()
{
    if (!m_initialized)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_shutdown = true;
    }
    m_condition.notify_all();

    for (std::thread& worker : m_workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
    m_workers.clear();

    for (auto& queue : m_queues)
    {
        while (!queue.jobs.empty())
        {
            queue.jobs.pop();
        }
    }

    m_initialized = false;
    std::cout << "[JobSystem] Shutdown complete\n";
}

JobId JobSystem::Schedule(JobFunction job, JobPriority priority, std::string_view name, JobCounter* counter)
{
    if (!m_initialized || !m_enabled || !job)
    {
        return kInvalidJobId;
    }

    const JobId id = m_nextJobId.fetch_add(1);

    Job j;
    j.function = std::move(job);
    j.name = std::string(name);
    j.priority = priority;
    j.counter = counter;

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (j.counter != nullptr)
        {
            j.counter->Increment();
        }
        m_queues[static_cast<std::size_t>(priority)].jobs.push(std::move(j));
    }

    m_condition.notify_one();
    return id;
}

void JobSystem::ScheduleBatch(std::vector<JobFunction> jobs, JobPriority priority, JobCounter* counter)
{
    if (!m_initialized || !m_enabled || jobs.empty())
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        for (auto& job : jobs)
        {
            Job j;
            j.function = std::move(job);
            j.priority = priority;
            j.counter = counter;
            if (j.counter != nullptr)
            {
                j.counter->Increment();
            }
            m_queues[static_cast<std::size_t>(priority)].jobs.push(std::move(j));
        }
    }

    m_condition.notify_all();
}

void JobSystem::WaitForAll()
{
    std::unique_lock<std::mutex> lock(m_queueMutex);
    m_completeCondition.wait(lock, [this]() {
        bool allEmpty = true;
        for (const auto& queue : m_queues)
        {
            if (!queue.jobs.empty())
            {
                allEmpty = false;
                break;
            }
        }
        return allEmpty && m_activeJobs.load() == 0;
    });
}

void JobSystem::WaitForCounter(JobCounter& counter)
{
    counter.Wait();
}

JobStats JobSystem::GetStats() const
{
    JobStats stats;
    stats.totalWorkers = m_workers.size();
    stats.completedJobs = m_completedJobs.load();

    std::lock_guard<std::mutex> lock(m_queueMutex);

    for (const auto& queue : m_queues)
    {
        stats.pendingJobs += queue.jobs.size();
    }
    stats.highPriorityPending = m_queues[static_cast<std::size_t>(JobPriority::High)].jobs.size();
    stats.normalPriorityPending = m_queues[static_cast<std::size_t>(JobPriority::Normal)].jobs.size();
    stats.lowPriorityPending = m_queues[static_cast<std::size_t>(JobPriority::Low)].jobs.size();

    stats.activeWorkers = std::min(m_activeJobs.load(), stats.totalWorkers);

    const std::uint64_t nowNs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    const std::uint64_t busyNowNs = m_busyWorkerTimeNs.load(std::memory_order_relaxed);
    const std::uint64_t prevSampleNs = m_statsLastSampleTimeNs.exchange(nowNs, std::memory_order_acq_rel);
    const std::uint64_t prevBusyNs = m_statsLastSampleBusyNs.exchange(busyNowNs, std::memory_order_acq_rel);

    if (stats.totalWorkers > 0 && nowNs > prevSampleNs && busyNowNs >= prevBusyNs)
    {
        const std::uint64_t elapsedNs = nowNs - prevSampleNs;
        const std::uint64_t busyDeltaNs = busyNowNs - prevBusyNs;
        const double capacityNs = static_cast<double>(elapsedNs) * static_cast<double>(stats.totalWorkers);
        if (capacityNs > 0.0)
        {
            const double utilization = (static_cast<double>(busyDeltaNs) / capacityNs) * 100.0;
            stats.frameWorkerUtilizationPct = static_cast<float>(std::clamp(utilization, 0.0, 100.0));
            stats.frameAverageActiveWorkers = static_cast<float>(static_cast<double>(busyDeltaNs) / static_cast<double>(elapsedNs));
        }
    }

    return stats;
}

std::size_t JobSystem::GetWorkerIndex() const
{
    return t_workerIndex;
}

void JobSystem::WorkerThread(std::size_t index)
{
    t_workerIndex = index;

    while (true)
    {
        Job job;

        {
            std::unique_lock<std::mutex> lock(m_queueMutex);

            m_condition.wait(lock, [this]() {
                if (m_shutdown)
                {
                    return true;
                }
                for (const auto& queue : m_queues)
                {
                    if (!queue.jobs.empty())
                    {
                        return true;
                    }
                }
                return false;
            });

            if (m_shutdown)
            {
                for (const auto& queue : m_queues)
                {
                    if (!queue.jobs.empty())
                    {
                        bool found = false;
                        for (std::size_t p = 0; p < static_cast<std::size_t>(JobPriority::Count); ++p)
                        {
                            if (!m_queues[p].jobs.empty())
                            {
                                job = std::move(m_queues[p].jobs.front());
                                m_queues[p].jobs.pop();
                                found = true;
                                break;
                            }
                        }
                        if (found)
                        {
                            break;
                        }
                    }
                }
                if (!job.function)
                {
                    return;
                }
            }
            else
            {
                for (std::size_t p = 0; p < static_cast<std::size_t>(JobPriority::Count); ++p)
                {
                    if (!m_queues[p].jobs.empty())
                    {
                        job = std::move(m_queues[p].jobs.front());
                        m_queues[p].jobs.pop();
                        break;
                    }
                }
            }
        }

        if (job.function)
        {
            ++m_activeJobs;
            const auto busyStart = std::chrono::steady_clock::now();

            try
            {
                job.function();
            }
            catch (const std::exception& e)
            {
                std::cerr << "[JobSystem] Job '" << job.name << "' threw exception: " << e.what() << "\n";
            }
            catch (...)
            {
                std::cerr << "[JobSystem] Job '" << job.name << "' threw unknown exception\n";
            }

            const auto busyEnd = std::chrono::steady_clock::now();
            const auto busyNs = std::chrono::duration_cast<std::chrono::nanoseconds>(busyEnd - busyStart).count();
            if (busyNs > 0)
            {
                m_busyWorkerTimeNs.fetch_add(static_cast<std::uint64_t>(busyNs), std::memory_order_relaxed);
            }

            --m_activeJobs;
            if (job.counter != nullptr)
            {
                job.counter->Decrement();
            }
            ++m_completedJobs;
            m_completeCondition.notify_all();
        }
    }
}

}
