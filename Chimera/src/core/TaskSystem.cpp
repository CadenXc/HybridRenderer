#include "pch.h"
#include "TaskSystem.h"
#include "Core/Log.h"

namespace Chimera
{
TaskSystem::TaskSystem(size_t numThreads)
{
        // 如果硬件核心数获取失败，保底给 4 个线程
    if (numThreads == 0) numThreads = 4;

    CH_CORE_INFO("TaskSystem: Initializing with {0} worker threads.",
                 numThreads);

    for (size_t i = 0; i < numThreads; ++i)
    {
        m_Workers.emplace_back([this] { WorkerThread(); });
    }
}

TaskSystem::~TaskSystem()
{
    Shutdown();
}

void TaskSystem::Shutdown()
{
    {
        std::unique_lock<std::mutex> lock(m_QueueMutex);
        if (m_Stop) return; // 已经停止过了
        m_Stop = true;
    }

        // 唤醒所有线程，让它们看到 m_Stop == true 后自行退出
    m_Condition.notify_all();

    for (std::thread& worker : m_Workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }

    CH_CORE_INFO("TaskSystem: Shutdown complete.");
}

bool TaskSystem::TryExecuteOneTask()
{
    std::function<void()> task;

    {
        std::lock_guard<std::mutex> lock(m_QueueMutex);

        if (m_Tasks.empty()) return false;

        task = std::move(m_Tasks.front());
        m_Tasks.pop();
    }

    // 必须在释放 m_QueueMutex 后执行。
    // 任务内部可能再次调用 Enqueue()，它也需要获取 m_QueueMutex。
    try
    {
        task();
    }
    catch (const std::exception& e)
    {
        CH_CORE_ERROR("TaskSystem: Task exception: {0}", e.what());
    }
    catch (...)
    {
        CH_CORE_ERROR("TaskSystem: Task threw an unknown exception.");
    }

    return true;
}

void TaskSystem::WorkerThread()
{
    s_CurrentWorkerPool = this;

    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(m_QueueMutex);

            m_Condition.wait(
                lock,
                [this]
                {
                    return m_Stop || !m_Tasks.empty();
                });

            if (m_Stop && m_Tasks.empty())
                break;
        }

        TryExecuteOneTask();
    }

    s_CurrentWorkerPool = nullptr;
}
} // namespace Chimera
