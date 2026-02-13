#pragma once

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace engine::core
{

using JobId = std::uint64_t;
constexpr JobId kInvalidJobId = 0;

enum class JobPriority : std::uint8_t
{
    High = 0,
    Normal = 1,
    Low = 2,
    Count = 3
};

struct JobStats
{
    std::size_t activeWorkers = 0;
    std::size_t totalWorkers = 0;
    std::size_t pendingJobs = 0;
    std::size_t completedJobs = 0;
    std::size_t highPriorityPending = 0;
    std::size_t normalPriorityPending = 0;
    std::size_t lowPriorityPending = 0;
    float frameWorkerUtilizationPct = 0.0F;
    float frameAverageActiveWorkers = 0.0F;
};

class JobCounter
{
public:
    explicit JobCounter(std::size_t initial = 0) : m_count(static_cast<std::ptrdiff_t>(initial)) {}

    void Increment()
    {
        m_count.fetch_add(1, std::memory_order_relaxed);
    }

    void Decrement()
    {
        const auto previous = m_count.fetch_sub(1, std::memory_order_acq_rel);
        if (previous <= 1)
        {
            m_count.store(0, std::memory_order_release);
            m_count.notify_all();
        }
    }

    [[nodiscard]] bool IsZero() const
    {
        return m_count.load(std::memory_order_acquire) <= 0;
    }

    void Wait()
    {
        while (true)
        {
            const auto value = m_count.load(std::memory_order_acquire);
            if (value <= 0)
            {
                return;
            }
            m_count.wait(value, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] std::size_t Get() const 
    { 
        const auto v = m_count.load(std::memory_order_acquire);
        return v > 0 ? static_cast<std::size_t>(v) : 0;
    }

private:
    std::atomic<std::ptrdiff_t> m_count;
};

class JobSystem
{
public:
    using JobFunction = std::function<void()>;

    static JobSystem& Instance()
    {
        static JobSystem s_instance;
        return s_instance;
    }

    bool Initialize(std::size_t workerCount = 0);
    void Shutdown();

    [[nodiscard]] bool IsInitialized() const { return m_initialized; }

    JobId Schedule(JobFunction job, JobPriority priority = JobPriority::Normal, std::string_view name = "", JobCounter* counter = nullptr);
    
    void ScheduleBatch(std::vector<JobFunction> jobs, JobPriority priority = JobPriority::Normal, JobCounter* counter = nullptr);
    
    template <typename Func>
    void ParallelFor(std::size_t count, std::size_t batchSize, Func&& func, JobPriority priority = JobPriority::Normal, JobCounter* counter = nullptr)
    {
        if (count == 0)
        {
            return;
        }

        if (batchSize == 0)
        {
            batchSize = 1;
        }

        using FuncType = std::decay_t<Func>;
        auto sharedFunc = std::make_shared<FuncType>(std::forward<Func>(func));

        if (!m_initialized || !m_enabled || m_workers.size() <= 1 || count <= batchSize)
        {
            for (std::size_t i = 0; i < count; ++i)
            {
                (*sharedFunc)(i);
            }
            return;
        }

        const std::size_t batches = (count + batchSize - 1) / batchSize;
        std::vector<JobFunction> jobs;
        jobs.reserve(batches);
        
        for (std::size_t batch = 0; batch < batches; ++batch)
        {
            const std::size_t start = batch * batchSize;
            const std::size_t end = std::min(start + batchSize, count);
            jobs.push_back([start, end, sharedFunc]() {
                for (std::size_t i = start; i < end; ++i)
                {
                    (*sharedFunc)(i);
                }
            });
        }
        
        ScheduleBatch(std::move(jobs), priority, counter);
    }

    void WaitForAll();
    void WaitForCounter(JobCounter& counter);

    [[nodiscard]] JobStats GetStats() const;

    [[nodiscard]] std::size_t WorkerCount() const { return m_workers.size(); }
    [[nodiscard]] std::size_t GetWorkerIndex() const;

    void SetEnabled(bool enabled) { m_enabled = enabled; }
    [[nodiscard]] bool IsEnabled() const { return m_enabled; }

private:
    JobSystem() = default;
    ~JobSystem() { Shutdown(); }

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    void WorkerThread(std::size_t index);

    struct Job
    {
        JobFunction function;
        std::string name;
        JobPriority priority = JobPriority::Normal;
        JobCounter* counter = nullptr;
    };

    struct PriorityQueue
    {
        std::queue<Job> jobs;
    };

    std::vector<std::thread> m_workers;
    std::array<PriorityQueue, static_cast<std::size_t>(JobPriority::Count)> m_queues;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_condition;
    std::condition_variable m_completeCondition;

    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_enabled{true};
    std::atomic<bool> m_shutdown{false};
    std::atomic<std::size_t> m_activeJobs{0};
    std::atomic<std::size_t> m_completedJobs{0};
    std::atomic<JobId> m_nextJobId{1};
    std::atomic<std::uint64_t> m_busyWorkerTimeNs{0};
    mutable std::atomic<std::uint64_t> m_statsLastSampleTimeNs{0};
    mutable std::atomic<std::uint64_t> m_statsLastSampleBusyNs{0};

    static thread_local std::size_t t_workerIndex;
};

class ScopedJobCounter
{
public:
    explicit ScopedJobCounter(JobCounter& counter) : m_counter(counter) { m_counter.Increment(); }
    ~ScopedJobCounter() { m_counter.Decrement(); }

    ScopedJobCounter(const ScopedJobCounter&) = delete;
    ScopedJobCounter& operator=(const ScopedJobCounter&) = delete;

private:
    JobCounter& m_counter;
};

}
