#pragma once

#include "pch.h"
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

namespace Chimera {

    class Log
    {
    public:
        static void Init();

        static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
        static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }
    private:
        static std::shared_ptr<spdlog::logger> s_CoreLogger;
        static std::shared_ptr<spdlog::logger> s_ClientLogger;
    };

}

// --- Enhanced Macros with File, Function and Line ---
#define CH_CORE_TRACE(...)  ::Chimera::Log::GetCoreLogger()->trace("[{0}]:[{1}] {2}", strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__, __LINE__, fmt::format(__VA_ARGS__))
#define CH_CORE_INFO(...)   ::Chimera::Log::GetCoreLogger()->info("[{0}]:[{1}] {2}", strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__, __LINE__, fmt::format(__VA_ARGS__))
#define CH_CORE_WARN(...)   ::Chimera::Log::GetCoreLogger()->warn("[{0}]:[{1}] {2}", strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__, __LINE__, fmt::format(__VA_ARGS__))
#define CH_CORE_ERROR(...)  ::Chimera::Log::GetCoreLogger()->error("[{0}]:[{1}] {2}", strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__, __LINE__, fmt::format(__VA_ARGS__))
#define CH_CORE_CRITICAL(...) ::Chimera::Log::GetCoreLogger()->critical("[{0}]:[{1}] {2}", strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__, __LINE__, fmt::format(__VA_ARGS__))

#define CH_TRACE(...)       ::Chimera::Log::GetClientLogger()->trace("[{0}]:[{1}] {2}", strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__, __LINE__, fmt::format(__VA_ARGS__))
#define CH_INFO(...)        ::Chimera::Log::GetClientLogger()->info("[{0}]:[{1}] {2}", strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__, __LINE__, fmt::format(__VA_ARGS__))
#define CH_WARN(...)        ::Chimera::Log::GetClientLogger()->warn("[{0}]:[{1}] {2}", strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__, __LINE__, fmt::format(__VA_ARGS__))
#define CH_ERROR(...)       ::Chimera::Log::GetClientLogger()->error("[{0}]:[{1}] {2}", strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__, __LINE__, fmt::format(__VA_ARGS__))
#define CH_CRITICAL(...)    ::Chimera::Log::GetClientLogger()->critical("[{0}]:[{1}] {2}", strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__, __LINE__, fmt::format(__VA_ARGS__))
