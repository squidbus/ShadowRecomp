#pragma once

#include "user/paths.h"

static const std::string GameExecutableFile = "default.xbe";
inline std::string GetMainGameFolder() { return (GetGamePath() / "game").string(); }
inline std::string GetGameExecutable() { return GetMainGameFolder() + "/" + GameExecutableFile; }

struct FileSystem
{
    static std::filesystem::path ResolvePath(const std::string_view& path, bool checkForMods, bool logModFile = false);
};
