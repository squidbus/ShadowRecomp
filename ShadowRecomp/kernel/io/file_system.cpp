#include "file_system.h"
#include <kernel/kernel.h>
#include <kernel/function.h>
#include <stdafx.h>
#include <iostream>
#include <os/logger.h>
#include <user/paths.h>
#include <mod/mod_loader.h>
#include <user/achievement_manager.h>

// Original code from Unleashed Recomp, O.G Xbox uses similar file / write operation functions than Xbox 360.

struct FileHandle : KernelObject
{
	std::fstream stream;
	std::filesystem::path path;
};

struct FindHandle : KernelObject
{
	std::error_code ec;
	ankerl::unordered_dense::map<std::u8string, std::pair<size_t, bool>> searchResult;
	decltype(searchResult)::iterator iterator;
	FindHandle() = default;
	FindHandle(const std::string& path)
	{
		auto addDirectory = [&](const std::filesystem::path& directory)
			{
				for (auto& entry : std::filesystem::directory_iterator(directory, ec))
				{
					std::u8string relativePath = entry.path().filename().u8string();
					searchResult.emplace(relativePath, std::make_pair(
						entry.is_directory(ec) ? 0 : entry.file_size(ec),
						entry.is_directory(ec)
					));
				}
			};

		std::string_view pathNoPrefix = path;
		size_t index = pathNoPrefix.find(":\\");
		if (index != std::string_view::npos)
			pathNoPrefix.remove_prefix(index + 2);
		std::string pathStr(pathNoPrefix);
		std::replace(pathStr.begin(), pathStr.end(), '\\', '/');

		for (size_t i = 0; ; i++)
		{
			auto* includeDirs = ModLoader::GetIncludeDirectories(i);
			if (includeDirs == nullptr)
				break;

			for (auto& includeDir : *includeDirs)
				addDirectory(includeDir / pathStr);
		}

		addDirectory(std::filesystem::path(path));
		iterator = searchResult.begin();
	}

	void fillFindData(WIN32_FIND_DATAA* lpFindFileData)
	{
		if (iterator->second.second)
			lpFindFileData->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
		else
			lpFindFileData->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;

		strncpy(lpFindFileData->cFileName, (const char*)(iterator->first.c_str()), sizeof(lpFindFileData->cFileName));
		lpFindFileData->nFileSizeLow = (uint32_t(iterator->second.first));
		lpFindFileData->nFileSizeHigh = (uint32_t(iterator->second.first >> 32U));
		memset(&lpFindFileData->ftCreationTime, 0, sizeof(lpFindFileData->ftCreationTime));
		memset(&lpFindFileData->ftLastAccessTime, 0, sizeof(lpFindFileData->ftLastAccessTime));
		memset(&lpFindFileData->ftLastWriteTime, 0, sizeof(lpFindFileData->ftLastWriteTime));
	}
};

// for save file path detection, we don't want the title ID in it.
static bool isHexSegment(const std::string_view& seg) {
	if (seg.size() < 8)
		return false;

	return std::all_of(seg.begin(), seg.end(), [](char c) {
		return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
		});
}


static bool isSaveSegment(const std::string_view& seg) {
	if (isHexSegment(seg))
		return true;

	return seg.size() == 4 && std::equal(seg.begin(), seg.end(), "save",
		[](char a, char b) { return std::tolower(a) == b; });
}

struct DriveMapping
{
	std::string_view letter;
	std::filesystem::path root;
	std::filesystem::path saveRoot;  // used when stripSegment triggers
	std::function<bool(const std::string_view&)> stripSegment;
};

static const DriveMapping& GetDriveMapping(const std::string_view& letter)
{
	static const std::vector<DriveMapping> table = {
		{ "d:", GetMainGameFolder(), {}, nullptr },
		{ "u:", GetUserPath(), GetSavePath(true), isSaveSegment},
	};

	for (auto& m : table)
	{
		if (std::equal(letter.begin(), letter.end(), m.letter.begin(), m.letter.end(),
			[](char a, char b) { return std::tolower(a) == std::tolower(b); }))
			return m;
	}

	static const DriveMapping fallback = { "", GetMainGameFolder() };
	return fallback;
}

std::filesystem::path FileSystem::ResolvePath(const std::string_view& path, bool checkForMods, bool logModFile)
{
	if (checkForMods)
	{
		std::filesystem::path resolvedPath = ModLoader::ResolvePath(path);
		if (!resolvedPath.empty())
		{
			if (logModFile)
				LOGF_IMPL(Utility, "Mod Loader", "Loading file: \"{}\"", reinterpret_cast<const char*>(resolvedPath.u8string().c_str()));
			return resolvedPath;
		}
	}

	std::filesystem::path p(path);
	auto colonPos = path.find(':');
	bool rootResolved = false;
	thread_local std::filesystem::path result;
	std::string_view relative;

	if (colonPos != std::string_view::npos)
	{
		auto driveLetter = path.substr(0, colonPos + 1);
		const auto& mapping = GetDriveMapping(driveLetter);
		result = mapping.root;

		// Skip past ":\" or ":/"
		size_t start = colonPos + 1;
		if (start < path.size() && (path[start] == '\\' || path[start] == '/'))
			++start;
		relative = path.substr(start);

		// Walk each path segment, applying strip filter
		std::string_view remaining = relative;
		while (!remaining.empty())
		{
			auto sep = remaining.find_first_of("/\\");
			std::string_view seg = (sep == std::string_view::npos)
				? remaining
				: remaining.substr(0, sep);

			if (mapping.stripSegment && mapping.stripSegment(seg))
			{
				if (!rootResolved && !mapping.saveRoot.empty())
				{
					result = mapping.saveRoot;
					rootResolved = true;
				}
			}
			else
			{
				result /= std::string(seg);
			}

			remaining = (sep == std::string_view::npos)
				? std::string_view{}
			: remaining.substr(sep + 1);
		}
	}
	else
	{
		result = GetMainGameFolder();
		result /= std::string(path);
	}
	return result;
}


FileHandle* XCreateFile_r(const char* SourceString, ULONG CreateDisposition, ULONG ShareAccess, int a4, int mode, unsigned int a6, int a7)
{
	ULONG CreateDisposition_ = FILE_OPEN;
	switch (mode)
	{
	case 1:
		CreateDisposition_ = FILE_CREATE;
		break;
	case 2:
		CreateDisposition_ = FILE_OVERWRITE_IF;
		break;
	case 3:
		CreateDisposition_ = FILE_OPEN;
		break;
	case 4:
		CreateDisposition_ = FILE_OPEN_IF;
		break;
	default:
		if (mode != 5 || (CreateDisposition_ = 4, (CreateDisposition & 0x40000000) == 0))
		{
			return GetInvalidKernelObject<FileHandle>();
		}
		break;
	}

	std::filesystem::path filePath = FileSystem::ResolvePath(SourceString, true, true);
	std::fstream fileStream;
	std::ios::openmode fileOpenMode = std::ios::binary;

	if (CreateDisposition_ == FILE_OPEN || CreateDisposition_ == FILE_OPEN_IF)
	{
		os::logger::LogFile("Loading", filePath.c_str());
		fileOpenMode |= std::ios::in;
	}
	else
	{
		fileOpenMode |= std::ios::out;
	}

	if (CreateDisposition & 0x80000000) // generic read
		fileOpenMode |= std::ios::in;

	if (CreateDisposition & 0x40000000) // generic write
		fileOpenMode |= std::ios::out;

	if ((fileOpenMode & std::ios::out) &&
		CreateDisposition != FILE_OPEN &&
		CreateDisposition != FILE_OPEN_IF)
		fileOpenMode |= std::ios::trunc;

	if (fileOpenMode & std::ios::out)
	{
		os::logger::LogFile("Write", filePath.c_str());
	}

	std::error_code ec;
	fileStream.open(filePath, fileOpenMode);
	const bool isNotSaveFile = filePath.string().find("Save") == std::string::npos;
	if (!std::filesystem::exists(filePath) && isNotSaveFile)
		LOGFN_ERROR("File does not exist: {}", filePath.string());

	if (!fileStream.is_open() && isNotSaveFile)
	{
		LOGFN_ERROR("Failed to open: {} errno: {}, message: {}", filePath.string(), errno, strerror(errno));
		return GetInvalidKernelObject<FileHandle>();
	}

	FileHandle* fileHandle = CreateKernelObject<FileHandle>();
	fileHandle->stream = std::move(fileStream);
	fileHandle->path = std::move(filePath);
	return fileHandle;
}

// Because the game call ntCreateFile for save file creation and we don't have it implemented, we use a mid asm hook
// to redirect it to XCreateFile instead.
void CreateSaveMetaMidAsmHook(X86Context& __restrict ctx, uint8_t* base)
{
	ctx.esp.u32 -= 4; // adjust esp to simulate guest call instruction that was skipped from ntCreateFile hook

	// The hook is right when the game is about to call ntCreateFile so we can safely pull the parameters at the same region than ntCreateFile
	auto ObjectAttributes_ = GetHostParameter(ctx, 3, CallConvention::Stdcall);
	auto DesiredAccess = GetHostParameter(ctx, 2, CallConvention::Stdcall);
	auto ShareAccess = GetHostParameter(ctx, 7, CallConvention::Stdcall);
	auto FileHandlePtr = (uint32_t*)GetHostParameter(ctx, 1, CallConvention::Stdcall, ParameterType::Pointer);

	// Get the string that contains the path destination for the file
	auto ObjectAttributesAddr = X86_LOAD_U32(ObjectAttributes_ + 4);
	auto pathAddr = X86_LOAD_U32(ObjectAttributesAddr + 4);
	auto path = (char*)g_memory.Translate(pathAddr);

	// create empty SaveMeta.xml
	auto newFileHandle = XCreateFile_r(path, DesiredAccess, ShareAccess, 0, 1, 0, 0);

	*FileHandlePtr = g_memory.MapVirtual(newFileHandle);

	ctx.esp.u32 += (4 * 9) + 4; // re adjust esp to simulate guest function clean up (clear 9 params + ret instruction)
}

uint32_t XReadFile_r(FileHandle* Handle, void* buffer, int length, uint32_t* bytesRead, XOVERLAPPED* overLapped)
{
	uint32_t result = FALSE;
	if (overLapped != nullptr)
	{
		std::streamoff streamOffset = overLapped->Offset + (std::streamoff(overLapped->OffsetHigh) << 32U);
		Handle->stream.clear();
		Handle->stream.seekg(streamOffset, std::ios::beg);
		if (Handle->stream.bad())
		{
			return FALSE;
		}
	}

	uint32_t numberOfBytesRead;
	Handle->stream.read((char*)(buffer), length);
	if (!Handle->stream.bad())
	{
		numberOfBytesRead = uint32_t(Handle->stream.gcount());
		result = TRUE;
	}

	if (result)
	{
		if (overLapped != nullptr)
		{
			overLapped->Internal = 0;
			overLapped->InternalHigh = numberOfBytesRead;
		}
		else if (bytesRead != nullptr)
		{
			*bytesRead = numberOfBytesRead;
		}
	}
	return result;
}

static uint32_t XGetFileSize_r(FileHandle* hFile, uint32_t* lpFileSizeHigh)
{
	std::error_code ec;
	auto fileSize = std::filesystem::file_size(hFile->path, ec);
	if (!ec)
	{
		if (lpFileSizeHigh != nullptr)
		{
			*lpFileSizeHigh = uint32_t(fileSize >> 32U);
		}

		return (uint32_t)(fileSize);
	}
	return INVALID_FILE_SIZE;
}

uint32_t XGetFileSizeEx_r(FileHandle* hFile, LARGE_INTEGER* lpFileSize)
{
	std::error_code ec;
	auto fileSize = std::filesystem::file_size(hFile->path, ec);
	if (!ec)
	{
		if (lpFileSize != nullptr)
		{
			lpFileSize->QuadPart = fileSize;
		}

		return TRUE;
	}
	return FALSE;
}

uint32_t XSetFilePointer_r(FileHandle* hFile, int32_t lDistanceToMove, int32_t* lpDistanceToMoveHigh, uint32_t dwMoveMethod)
{
	int32_t distanceToMoveHigh = (lpDistanceToMoveHigh != nullptr) ? *lpDistanceToMoveHigh : 0;
	std::streamoff streamOffset = lDistanceToMove + (std::streamoff(distanceToMoveHigh) << 32U);
	std::fstream::seekdir streamSeekDir = {};
	switch (dwMoveMethod)
	{
	case FILE_BEGIN:
		streamSeekDir = std::ios::beg;
		break;
	case FILE_CURRENT:
		streamSeekDir = std::ios::cur;
		break;
	case FILE_END:
		streamSeekDir = std::ios::end;
		break;
	default:
		assert(false && "Unknown move method.");
		break;
	}

	hFile->stream.clear();
	hFile->stream.seekg(streamOffset, streamSeekDir);
	if (hFile->stream.bad())
	{
		return INVALID_SET_FILE_POINTER;
	}

	std::streampos streamPos = hFile->stream.tellg();
	if (lpDistanceToMoveHigh != nullptr)
		*lpDistanceToMoveHigh = int32_t(streamPos >> 32U);

	return uint32_t(streamPos);
}

uint32_t XSetFilePointerEx_r(FileHandle* hFile, int32_t lDistanceToMove, LARGE_INTEGER* lpNewFilePointer, uint32_t dwMoveMethod)
{
	std::fstream::seekdir streamSeekDir = {};
	switch (dwMoveMethod)
	{
	case FILE_BEGIN:
		streamSeekDir = std::ios::beg;
		break;
	case FILE_CURRENT:
		streamSeekDir = std::ios::cur;
		break;
	case FILE_END:
		streamSeekDir = std::ios::end;
		break;
	default:
		assert(false && "Unknown move method.");
		break;
	}

	hFile->stream.clear();
	hFile->stream.seekg(lDistanceToMove, streamSeekDir);
	if (hFile->stream.bad())
	{
		return FALSE;
	}

	if (lpNewFilePointer != nullptr)
	{
		lpNewFilePointer->QuadPart = hFile->stream.tellg();
	}
	return TRUE;
}

uint32_t XWriteFile_r(FileHandle* hFile, const void* lpBuffer, uint32_t nNumberOfBytesToWrite, uint32_t* lpNumberOfBytesWritten, void* lpOverlapped)
{
	assert(lpOverlapped == nullptr && "Overlapped not implemented.");
	hFile->stream.write((const char*)(lpBuffer), nNumberOfBytesToWrite);
	if (hFile->stream.fail())
	{
		if (lpNumberOfBytesWritten != nullptr)
			*lpNumberOfBytesWritten = 0;

		return FALSE;
	}

	if (lpNumberOfBytesWritten != nullptr)
		*lpNumberOfBytesWritten = nNumberOfBytesToWrite;

	return TRUE;
}

FindHandle* XFindFirstFile(const char* lpFileName, WIN32_FIND_DATAA* lpFindFileData)
{
	std::string_view originalPath = lpFileName;

	// First, resolve the Xbox path to PC path before processing wildcards
	std::string resolvedPathStr;
	bool isDirectorySearch = false;

	// Check for wildcards in the original path first
	if (originalPath.find("\\*") == (originalPath.size() - 2) || originalPath.find("/*") == (originalPath.size() - 2))
	{
		// Directory search with "*" - remove the wildcard before resolving
		std::string_view pathWithoutWildcard = originalPath.substr(0, originalPath.size() - 2);
		resolvedPathStr = FileSystem::ResolvePath(pathWithoutWildcard, true).string();
		isDirectorySearch = true;
	}
	else if (originalPath.find("\\*.*") == (originalPath.size() - 4) || originalPath.find("/*.*") == (originalPath.size() - 4))
	{
		// Directory search with "*.*" - remove the wildcard before resolving
		std::string_view pathWithoutWildcard = originalPath.substr(0, originalPath.size() - 4);
		resolvedPathStr = FileSystem::ResolvePath(pathWithoutWildcard, true).string();
		isDirectorySearch = true;
	}
	else
	{
		// Resolve the full path as-is
		resolvedPathStr = FileSystem::ResolvePath(originalPath, true).string();
	}

	if (isDirectorySearch)
	{
		// Handle directory search
		FindHandle findHandle(resolvedPathStr);

		// Detect save data operations 'u:\*' and gate to 'save' folder
		if (originalPath == "u:\\*")
		{
			findHandle.searchResult.clear();
			std::error_code ec;
			if (std::filesystem::is_regular_file(GetSaveFilePath(true), ec))
			{
				findHandle.searchResult.emplace(u8"save", std::make_pair(0, true));
			}
			findHandle.iterator = findHandle.searchResult.begin();
		}

		if (findHandle.searchResult.empty())
			return GetInvalidKernelObject<FindHandle>();

		findHandle.fillFindData(lpFindFileData);
		return CreateKernelObject<FindHandle>(std::move(findHandle));
	}
	else
	{
		// Handle specific file search
		std::filesystem::path resolvedPath(resolvedPathStr);
		std::error_code ec;

		if (std::filesystem::exists(resolvedPath, ec) && !ec)
		{
			// It's a specific file - create a search result with just this file
			FindHandle* findHandle = CreateKernelObject<FindHandle>();

			std::filesystem::path filename = resolvedPath.filename();
			bool isDirectory = std::filesystem::is_directory(resolvedPath, ec);
			size_t fileSize = isDirectory ? 0 : std::filesystem::file_size(resolvedPath, ec);

			findHandle->searchResult.emplace(
				filename.u8string(),
				std::make_pair(fileSize, isDirectory)
			);
			findHandle->iterator = findHandle->searchResult.begin();
			findHandle->ec = ec;

			findHandle->fillFindData(lpFindFileData);
			return findHandle;
		}
		else
		{
			LOGF_WARNING("File not found: {} (resolved to {})",
				lpFileName, resolvedPathStr);
			return GetInvalidKernelObject<FindHandle>();
		}
	}
}

uint32_t XFindNextFile(FindHandle* Handle, WIN32_FIND_DATAA* lpFindFileData)
{
	Handle->iterator++;
	if (Handle->iterator == Handle->searchResult.end())
	{
		return FALSE;
	}
	else
	{
		Handle->fillFindData(lpFindFileData);
		return TRUE;
	}
}

int32_t UserPathExist(const char* SourceString, int32_t freeSpaceAvailable, int32_t totalBytes, int32_t a4)
{
	auto base = g_memory.base;
	constexpr uint32_t fake_free = 1ULL * 1024 * 1024 * 1024;
	constexpr uint32_t fake_total = 2ULL * 1024 * 1024 * 1024;

	X86_STORE_U32(freeSpaceAvailable, fake_free); // lie to the game that we have enough free space in the non-existent Xbox HDD.
	X86_STORE_U32(totalBytes, fake_total);
	return std::filesystem::exists(GetUserPath());
}

int32_t CreateSaveFolder(const char* SourceString, int32_t a2)
{
	const auto saveFilePath = GetSaveFilePath(true);
	bool saveFileExists = std::filesystem::exists(saveFilePath);

	if (!saveFileExists)
	{
		std::error_code ec;
		std::filesystem::create_directories(saveFilePath.parent_path(), ec);
		saveFileExists = !ec;
	}
	return saveFileExists;
}

int CacheGameFiles(int a1)
{
	LOGN("CACHE STUB");
	return 0;
}

// load save data
X86_FUNC_IMPL(__imp__sub_2A4420);
X86_OVERRIDE_FUNC(sub_2A4420)
{
	__imp__sub_2A4420(ctx, base);
	if (AchievementManager::LoadBinary())
	{
		AchievementManager::SaveBinary();
	}
}

// XAPILIB::XCreateSaveGame
X86_FUNC_IMPL(__imp__sub_2AEC9A);
X86_OVERRIDE_FUNC(sub_2AEC9A)
{
	__imp__sub_2AEC9A(ctx, base);
	if (AchievementManager::LoadBinary())
	{
		AchievementManager::SaveBinary();
	}
}

GUEST_FUNCTION_HOOK_STDCALL(sub_2AF653, XCreateFile_r);
GUEST_FUNCTION_HOOK_STDCALL(sub_2ADFF0, XReadFile_r);
GUEST_FUNCTION_HOOK_STDCALL(sub_2AE0DD, XWriteFile_r);
GUEST_FUNCTION_HOOK_STDCALL(sub_2AE45E, XGetFileSize_r);
GUEST_FUNCTION_HOOK_STDCALL(sub_2AE420, XGetFileSizeEx_r);
GUEST_FUNCTION_HOOK_STDCALL(sub_2AE237, XSetFilePointer_r);
GUEST_FUNCTION_HOOK_STDCALL(sub_2AE331, XSetFilePointerEx_r);
GUEST_FUNCTION_HOOK_STDCALL(sub_2AD93C, XFindFirstFile);
GUEST_FUNCTION_HOOK_STDCALL(sub_2ADA49, XFindNextFile);

// save file stuff (fake U partition)
GUEST_FUNCTION_HOOK_STDCALL(sub_2AF174, UserPathExist);
GUEST_FUNCTION_HOOK_STDCALL(sub_2B04AE, CreateSaveFolder);

// stub
GUEST_FUNCTION_HOOK(sub_332B60, CacheGameFiles);
