#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>

namespace Chimera
{
    class TaskSystem
    {
    public:
        // 初始化线程池，自动获取 CPU 核心数
        TaskSystem(size_t numThreads = std::thread::hardware_concurrency() - 1);
        ~TaskSystem();

        // 核心大法：将任意函数扔进后台线程执行，并返回一个 future 用于获取结果
        template<class F, class... Args>
        auto Enqueue(F&& f, Args&&... args) 
            -> std::future<typename std::invoke_result<F, Args...>::type>;

        // 停止所有任务（退出引擎时调用）
        void Shutdown();

    private:
        // 工作线程的主循环
        void WorkerThread();

    private:
        std::vector<std::thread> m_Workers;
        std::queue<std::function<void()>> m_Tasks;

        std::mutex m_QueueMutex;
        std::condition_variable m_Condition;
        bool m_Stop = false;
    };

    // --- Template Implementation ---
    template<class F, class... Args>
    auto TaskSystem::Enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result<F, Args...>::type>
    {
        using return_type = typename std::invoke_result<F, Args...>::type;

        // 将任务包装成 packaged_task，以便能够提取 future
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();

        {
            std::unique_lock<std::mutex> lock(m_QueueMutex);
            if (m_Stop)
                throw std::runtime_error("Enqueue on stopped TaskSystem");

            // 将任务塞入队列
            m_Tasks.emplace([task]() { (*task)(); });
        }
        
        // 唤醒一个正在睡眠的工人线程
        m_Condition.notify_one();
        return res;
    }
}
