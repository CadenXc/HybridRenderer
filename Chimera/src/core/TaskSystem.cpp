#include "pch.h"
#include "TaskSystem.h"
#include "Core/Log.h"

namespace Chimera
{
    TaskSystem::TaskSystem(size_t numThreads)
    {
        // 如果硬件核心数获取失败，保底给 4 个线程
        if (numThreads == 0) numThreads = 4;

        CH_CORE_INFO("TaskSystem: Initializing with {0} worker threads.", numThreads);

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

    void TaskSystem::WorkerThread()
    {
        while (true)
        {
            std::function<void()> task;

            {
                std::unique_lock<std::mutex> lock(m_QueueMutex);
                // 线程在这里沉睡，直到有新任务进来，或者系统要求停止
                m_Condition.wait(lock, [this] { return m_Stop || !m_Tasks.empty(); });

                // 如果要求停止且队列空了，线程结束生命周期
                if (m_Stop && m_Tasks.empty())
                    return;

                // 取出最前面的任务
                task = std::move(m_Tasks.front());
                m_Tasks.pop();
            }

            // 执行任务
            if (task)
            {
                try {
                    task();
                } catch (const std::exception& e) {
                    CH_CORE_ERROR("TaskSystem: Task exception: {0}", e.what());
                }
            }
        }
    }
}
