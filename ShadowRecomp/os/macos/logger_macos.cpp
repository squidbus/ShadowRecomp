#include <os/logger.h>
#include <os/process.h>

void os::logger::Init()
{
    // stub
}

void os::logger::Log(const std::string_view str, ELogType type, const char* func)
{
    if (!os::process::g_consoleVisible)
        return;

    if (func)
    {
        fmt::println("[{}] {}", func, str);
    }
    else
    {
        fmt::println("{}", str);
    }
}

void os::logger::LogFile(const std::string_view str, std::filesystem::path fileName)
{
#ifdef _DEBUG
    fmt::println("{} '{}'...", str, fileName.string());
#endif
}
