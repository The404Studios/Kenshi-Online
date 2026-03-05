#include "task_orchestrator.h"
#include <spdlog/spdlog.h>
#include <windows.h>

namespace kmp {

// SEH wrapper for task execution on worker threads.
// C++ try/catch does NOT catch access violations (structured exceptions).
// Without this, an AV in BackgroundReadEntities or BackgroundInterpolate
// terminates the entire process silently. With SEH, the AV is caught,
// logged, and the worker thread continues processing future tasks.
//
// This function MUST NOT contain C++ objects with destructors (MSVC rule).
// The std::function is passed by pointer (no construction inside __try).
static bool SEH_ExecuteWorkerTask(void (*executor)(void*), void* context) {
    __try {
        executor(context);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        DWORD code = GetExceptionCode();
        char buf[256];
        sprintf_s(buf, "KMP: TaskOrchestrator worker SEH caught exception 0x%08lX "
                       "(likely AV in game memory read on background thread)\n", code);
        OutputDebugStringA(buf);
        return false;
    }
}

// Trampoline: calls std::function<void()> via void* cast
static void ExecuteFnPtr(void* ctx) {
    auto* fn = static_cast<std::function<void()>*>(ctx);
    (*fn)();
}

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

        // Execute the task with SEH + C++ exception protection.
        // SEH catches structured exceptions (access violations from game memory reads).
        // Without this, an AV on a worker thread terminates the entire process.
        if (!SEH_ExecuteWorkerTask(ExecuteFnPtr, &task.work)) {
            spdlog::error("TaskOrchestrator: Worker caught structured exception "
                          "(AV in game memory) — task skipped, worker continues");
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
