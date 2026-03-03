#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace kmp {

class TaskOrchestrator {
public:
    void Start(int numWorkers = 2);
    void Stop();

    // Enqueue a general background task (fire-and-forget)
    void Post(std::function<void()> task);

    // Enqueue a task that MUST complete before the next frame swap.
    // Game thread calls WaitForFrameWork() to block until all frame tasks are done.
    void PostFrameWork(std::function<void()> task);

    // Block until all PostFrameWork tasks have completed (called from game thread)
    void WaitForFrameWork();

    bool IsRunning() const { return m_running.load(std::memory_order_acquire); }

private:
    void WorkerLoop();

    struct Task {
        std::function<void()> work;
        bool isFrameWork = false;
    };

    std::vector<std::thread>  m_workers;
    std::queue<Task>          m_tasks;
    std::mutex                m_taskMutex;
    std::condition_variable   m_taskCV;

    std::atomic<int>          m_pendingFrameWork{0};
    std::mutex                m_frameMutex;
    std::condition_variable   m_frameCV;

    std::atomic<bool>         m_running{false};
};

} // namespace kmp
