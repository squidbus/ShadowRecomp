#pragma once

#define NOMINMAX

#if defined(_WIN32)
#include <windows.h>
#include <ShlObj_core.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
#elif defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#include <pwd.h>
#include <cstdint>
#include <cctype>
#include <cstdio>

#ifndef LONG
typedef int32_t LONG;
#endif
#ifndef BYTE
typedef uint8_t BYTE;
#endif
#ifndef WORD
typedef uint16_t WORD;
#endif
#ifndef CONST
#define CONST const
#endif

#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif
#ifndef OPTIONAL
#define OPTIONAL
#endif

#define MAKEFOURCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

#ifndef STATUS_TIMEOUT
#define STATUS_TIMEOUT 0x00000102L
#endif

#ifndef DEFAULT_UNREACHABLE
#define DEFAULT_UNREACHABLE __builtin_unreachable()
#endif

#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_PCM 1
#endif

#ifndef MAKE_HRESULT
#define MAKE_HRESULT(sev, fac, code) ((int32_t)(((uint32_t)(sev) << 31) | ((uint32_t)(fac) << 16) | ((uint32_t)(code))))
#endif

#ifndef E_INVALIDARG
#define E_INVALIDARG ((int32_t)0x80070057)
#endif

#ifndef WAVEFORMATEX
typedef struct _WAVEFORMATEX {
    uint16_t wFormatTag;
    uint16_t nChannels;
    uint32_t nSamplesPerSec;
    uint32_t nAvgBytesPerSec;
    uint16_t nBlockAlign;
    uint16_t wBitsPerSample;
    uint16_t cbSize;
} WAVEFORMATEX;
#endif

#ifndef ULONG
typedef uint32_t ULONG;
#endif
#ifndef ULONGLONG
typedef uint64_t ULONGLONG;
#endif
#ifndef ULONG32
typedef uint32_t ULONG32;
#endif
#ifndef BOOLEAN
typedef uint8_t BOOLEAN;
#endif
#ifndef FLOAT
typedef float FLOAT;
#endif
#ifndef SHORT
typedef int16_t SHORT;
#endif
#ifndef UINT
typedef unsigned int UINT;
#endif
#ifndef INT
typedef int INT;
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef INFINITE
#define INFINITE 0xFFFFFFFF
#endif
#ifndef S_OK
#define S_OK ((int32_t)0)
#endif
#ifndef REG_DWORD
#define REG_DWORD 4
#endif
#ifndef INVALID_FILE_SIZE
#define INVALID_FILE_SIZE ((uint32_t)0xFFFFFFFF)
#endif
#ifndef INVALID_SET_FILE_POINTER
#define INVALID_SET_FILE_POINTER ((uint32_t)0xFFFFFFFF)
#endif
#ifndef FILE_BEGIN
#define FILE_BEGIN 0
#endif
#ifndef FILE_CURRENT
#define FILE_CURRENT 1
#endif
#ifndef FILE_END
#define FILE_END 2
#endif
#ifndef FILE_OPEN
#define FILE_OPEN 1
#endif
#ifndef FILE_CREATE
#define FILE_CREATE 2
#endif
#ifndef FILE_ATTRIBUTE_NORMAL
#define FILE_ATTRIBUTE_NORMAL 0x80
#endif
#ifndef FILE_ATTRIBUTE_DIRECTORY
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#endif
#ifndef PURE
#define PURE = 0
#endif

#ifndef RECT
typedef struct _RECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT;
#endif

#ifndef LARGE_INTEGER
typedef union _LARGE_INTEGER {
    struct {
        uint32_t LowPart;
        int32_t HighPart;
    };
    int64_t QuadPart;
} LARGE_INTEGER;
#endif
#ifndef ULARGE_INTEGER
typedef LARGE_INTEGER ULARGE_INTEGER;
#endif

#ifndef WIN32_FIND_DATAA
typedef struct _WIN32_FIND_DATAA {
    uint32_t dwFileAttributes;
    uint32_t ftCreationTime[2];
    uint32_t ftLastAccessTime[2];
    uint32_t ftLastWriteTime[2];
    uint32_t nFileSizeHigh;
    uint32_t nFileSizeLow;
    uint32_t dwReserved0;
    uint32_t dwReserved1;
    char cFileName[260];
    char cAlternateFileName[14];
} WIN32_FIND_DATAA;
#endif

#ifndef NTSTATUS
typedef int32_t NTSTATUS;
#endif
#ifndef HRESULT
typedef int32_t HRESULT;
#endif
#ifndef LPCVOID
typedef const void* LPCVOID;
#endif
#ifndef VOID
typedef void VOID;
#endif
#ifndef HANDLE
typedef void* HANDLE;
#endif
#ifndef PCHAR
typedef char* PCHAR;
#endif
#ifndef PSIZE_T
typedef uint32_t* PSIZE_T;
#endif
#ifndef SIZE_T
typedef uint32_t SIZE_T;
#endif

#ifndef E_FAIL
#define E_FAIL ((HRESULT)0x80004005)
#endif
#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS 0
#endif
#ifndef ERROR_BAD_ARGUMENTS
#define ERROR_BAD_ARGUMENTS 87
#endif
#ifndef ERROR_DEVICE_NOT_CONNECTED
#define ERROR_DEVICE_NOT_CONNECTED 1167
#endif
#ifndef STATUS_USER_APC
#define STATUS_USER_APC 0x000000C0L
#endif
#ifndef STATUS_ALERTED
#define STATUS_ALERTED 0x00000101L
#endif
#ifndef STATUS_PENDING
#define STATUS_PENDING 0x00000103L
#endif
#ifndef THREAD_PRIORITY_LOWEST
#define THREAD_PRIORITY_LOWEST 0
#endif
#ifndef THREAD_PRIORITY_BELOW_NORMAL
#define THREAD_PRIORITY_BELOW_NORMAL 1
#endif
#ifndef THREAD_PRIORITY_NORMAL
#define THREAD_PRIORITY_NORMAL 2
#endif
#ifndef THREAD_PRIORITY_ABOVE_NORMAL
#define THREAD_PRIORITY_ABOVE_NORMAL 3
#endif
#ifndef THREAD_PRIORITY_HIGHEST
#define THREAD_PRIORITY_HIGHEST 4
#endif
#ifndef THREAD_PRIORITY_TIME_CRITICAL
#define THREAD_PRIORITY_TIME_CRITICAL 15
#endif

#ifndef STATUS_WAIT_0
#define STATUS_WAIT_0 0x00000000L
#endif
#ifndef STATUS_INVALID_HANDLE
#define STATUS_INVALID_HANDLE ((int32_t)0xC0000008L)
#endif
#ifndef PAGE_READWRITE
#define PAGE_READWRITE 0x04
#endif

#ifndef ACCESS_MASK
typedef uint32_t ACCESS_MASK;
#endif
#ifndef PHANDLE
typedef void** PHANDLE;
#endif
#ifndef PULONG
typedef uint32_t* PULONG;
#endif
#ifndef USHORT
typedef uint16_t USHORT;
#endif
#ifndef PCSTR
typedef const char* PCSTR;
#endif

#ifndef CONTAINING_RECORD
#define CONTAINING_RECORD(address, type, field) ((type*)((char*)(address) - offsetof(type, field)))
#endif

#ifndef Sleep
inline void Sleep(uint32_t ms) { usleep(ms * 1000); }
#endif

#ifndef _stricmp
#define _stricmp strcasecmp
#endif
#ifndef _strupr
inline char* _strupr(char* s) { for (char* p = s; *p; ++p) *p = (char)toupper((unsigned char)*p); return s; }
#endif
#ifndef _strlwr
inline char* _strlwr(char* s) { for (char* p = s; *p; ++p) *p = (char)tolower((unsigned char)*p); return s; }
#endif
#ifndef OutputDebugStringA
inline void OutputDebugStringA(const char* s) { printf("%s", s); }
#endif

#endif

#ifdef SHADOW_RECOMP_D3D12
#include <dxcapi.h>
#endif

#include <algorithm>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <cassert>
#include <chrono>
#include <span>
#include <xbox.h>
#include <xxhash.h>

#include <ankerl/unordered_dense.h>
#include <x86/x86_recomp_shared.h>
#include <toml++/toml.hpp>
#include <zstd.h>
#include <ddspp.h>
#include <stb_image.h>
#include <blockingconcurrentqueue.h>
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>
#include <backends/imgui_impl_sdl3.h>

#ifdef None
#undef None
#endif


#include <o1heap.h>
#include <cstddef>
#include <smolv.h>
#include <set>
#include <fmt/core.h>
#include <list>
#include <semaphore>
#include <numeric>
#include <charconv>

#include "util.h"

#include "framework.h"
#include "mutex.h"
#include "include/xshader_api.h"
#include "thread_dispatcher.h"

#ifndef _WIN32
#include <sys/mman.h>
#endif

#ifndef _WIN32
typedef XLIST_ENTRY LIST_ENTRY;
#endif
