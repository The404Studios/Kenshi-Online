#include "task_orchestrator.h"
#include <spdlog/spdlog.h>

namespace kmp {

void TaskOrchestrator::Start(int numWorkers) {
    if (m_running.exchange(true)) return; // Already running

    spdlog::info("TaskOrchestrator: Starting {} worker threads", numWorkers);
    m_workers.reserve(numWorkers);
    for (int i = 0; i < numWorkers; ++i) {
        m_workers.emplace_back(&TaskOrchestrator::WorkerLoop, this);
    }
}

void TaskOrchestrator::Stop() {
    if (!m_running.exchange(false)) return; // Already stopped

    // Wake all workers so they exit
    m_taskCV.notify_all();

    for (auto& w : m_workers) {
        if (w.joinable()) w.join();
    }
    m_workers.clear();

    // Drain any remaining tasks
    {
        std::lock_guard lock(m_taskMutex);
        while (!m_tasks.empty()) m_tasks.pop();
    }
    m_pendingFrameWork.store(0);

    spdlog::info("TaskOrchestrator: Stopped");
}

void TaskOrchestrator::Post(std::function<void()> task) {
    {
        std::lock_guard lock(m_taskMutex);
        m_tasks.push({std::move(task), false});
    }
    m_taskCV.notify_one();
}

void TaskOrchestrator::PostFrameWork(std::function<void()> task) {
    m_pendingFrameWork.fetch_add(1, std::memory_order_acq_rel);
    {
        std::lock_guard lock(m_taskMutex);
        m_tasks.push({std::move(task), true});
    }
    m_taskCV.notify_one();
}

void TaskOrchestrator::WaitForFrameWork() {
    std::unique_lock lock(m_frameMutex);
    m_frameCV.wait(lock, [this] {
        return m_pendingFrameWork.load(std::memory_order_acquire) <= 0;
    });
}

void TaskOrchestrator::WorkerLoop() {
    while (m_running.load(std::memory_order_acquire)) {
        Task task;
        {
            std::unique_lock lock(m_taskMutex);
            m_taskCV.wait(lock, [this] {
                return !m_tasks.empty() || !m_running.load(std::memory_order_acquire);
            });

            if (!m_running.load(std::memory_order_acquire) && m_tasks.empty()) {
                return;
            }

            task = std::move(m_tasks.front());
            m_tasks.pop();
        }

        // Execute the task
        try {
            task.work();
        } catch (const std::exception& e) {
            spdlog::error("TaskOrchestrator: Worker caught exception: {}", e.what());
        } catch (...) {
            spdlog::error("TaskOrchestrator: Worker caught unknown exception");
        }

        // If this was frame work, decrement counter and notify game thread
        if (task.isFrameWork) {
            if (m_pendingFrameWork.fetch_sub(1, std::memory_order_acq_rel) <= 1) {
                m_frameCV.notify_all();
            }
        }
    }
}

} // namespace kmp
