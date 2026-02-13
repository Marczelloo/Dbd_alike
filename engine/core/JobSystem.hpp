#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
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
};

class JobCounter
{
public:
    explicit JobCounter(std::size_t initial = 0) : m_count(static_cast<std::ptrdiff_t>(initial)) {}

    void Increment() { ++m_count; }
    void Decrement() { --m_count; }
    [[nodiscard]] bool IsZero() const { return m_count.load() == 0; }
    [[nodiscard]] std::size_t Get() const 
    { 
        const auto v = m_count.load();
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

    JobId Schedule(JobFunction job, JobPriority priority = JobPriority::Normal, std::string_view name = "");
    
    void ScheduleBatch(std::vector<JobFunction> jobs, JobPriority priority = JobPriority::Normal);
    
    template <typename Func>
    void ParallelFor(std::size_t count, std::size_t batchSize, Func&& func, JobPriority priority = JobPriority::Normal)
    {
        if (count == 0) return;
        
        const std::size_t batches = (count + batchSize - 1) / batchSize;
        std::vector<JobFunction> jobs;
        jobs.reserve(batches);
        
        for (std::size_t batch = 0; batch < batches; ++batch)
        {
            const std::size_t start = batch * batchSize;
            const std::size_t end = std::min(start + batchSize, count);
            jobs.push_back([start, end, &func]() {
                for (std::size_t i = start; i < end; ++i)
                {
                    func(i);
                }
            });
        }
        
        ScheduleBatch(std::move(jobs), priority);
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
