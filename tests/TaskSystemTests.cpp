#include "Core/Log.h"
#include "Core/TaskSystem.h"

#include <chrono>
#include <exception>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace
{
void Require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

template <typename TestFunction>
bool RunTest(const char* name, TestFunction&& test)
{
    try
    {
        test();
        std::cout << "[PASS] " << name << '\n';
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[FAIL] " << name << ": " << e.what() << '\n';
        return false;
    }
}

void TestSingleTaskReturnsResult()
{
    Chimera::TaskSystem tasks(1);

    auto result = tasks.Enqueue([] { return 42; });

    Require(result.get() == 42, "task returned an unexpected value");
}

void TestMultipleTasksComplete()
{
    Chimera::TaskSystem tasks(2);
    std::vector<std::future<int>> results;

    for (int i = 1; i <= 8; ++i)
        results.push_back(tasks.Enqueue([i] { return i; }));

    int sum = 0;
    for (auto& result : results)
        sum += result.get();

    Require(sum == 36, "not all queued tasks completed correctly");
}

void TestShutdownIsIdempotent()
{
    Chimera::TaskSystem tasks(1);

    tasks.Shutdown();
    tasks.Shutdown();
}

void TestEnqueueAfterShutdownThrows()
{
    Chimera::TaskSystem tasks(1);
    tasks.Shutdown();

    bool threw = false;

    try
    {
        auto unused = tasks.Enqueue([] {});
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }

    Require(threw, "Enqueue after Shutdown did not throw");
}

void TestNestedTaskCompletesWithSingleWorker()
{
    Chimera::TaskSystem tasks(1);

    auto outer = tasks.Enqueue([&tasks]
    {
        auto inner = tasks.Enqueue([] { return 42; });

        tasks.Wait(inner);

        return inner.get() == 42;

    });

    Require(
        outer.wait_for(2s) == std::future_status::ready,
        "outer task did not finish within the safety timeout");

    Require(
        outer.get(),
        "nested task could not run while the only worker was waiting");
}
}

int main()
{
    Chimera::Log::Init();

    int failed = 0;

    failed += !RunTest(
        "single task returns result",
        TestSingleTaskReturnsResult);

    failed += !RunTest(
        "multiple tasks complete",
        TestMultipleTasksComplete);

    failed += !RunTest(
        "Shutdown is idempotent",
        TestShutdownIsIdempotent);

    failed += !RunTest(
        "Enqueue after Shutdown throws",
        TestEnqueueAfterShutdownThrows);

    failed += !RunTest(
        "nested task completes with one worker",
        TestNestedTaskCompletesWithSingleWorker);

    std::cout << '\n';

    if (failed == 0)
    {
        std::cout << "All TaskSystem tests passed.\n";
        return 0;
    }

    std::cerr << failed << " TaskSystem test(s) failed.\n";
    return 1;
}
