// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: The cache of unpacked archives and inline data, source

#include "cache.h"

#include <ctime>
#include <vector>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include "libs/dsk_tools/src/utils.h"
#else
    #include <dirent.h>
    #include <sys/stat.h>
    #include <unistd.h>
    #include <utime.h>
#endif

namespace {

struct Item {
    std::string name;
    bool dir;
    time_t modified;
};

#ifdef _WIN32

//FILETIME counts 100 ns from 1601, time_t seconds from 1970
time_t to_time_t(const FILETIME &ft)
{
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<time_t>((u.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

std::vector<Item> list(const std::string &dir)
{
    std::vector<Item> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(dsk_tools::utf8_to_wide(dir + "*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        const std::wstring w = fd.cFileName;
        if (w == L"." || w == L"..") continue;
        const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string name(n > 0 ? n - 1 : 0, '\0');
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &name[0], n, nullptr, nullptr);
        out.push_back({name, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, to_time_t(fd.ftLastWriteTime)});
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

void remove_file(const std::string &path) { DeleteFileW(dsk_tools::utf8_to_wide(path).c_str()); }
void remove_dir(const std::string &path) { RemoveDirectoryW(dsk_tools::utf8_to_wide(path).c_str()); }

#else

std::vector<Item> list(const std::string &dir)
{
    std::vector<Item> out;
    DIR * d = opendir(dir.c_str());
    if (d == nullptr) return out;
    while (dirent * e = readdir(d))
    {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        struct stat st;
        if (stat((dir + name).c_str(), &st) != 0) continue;
        out.push_back({name, S_ISDIR(st.st_mode), st.st_mtime});
    }
    closedir(d);
    return out;
}

void remove_file(const std::string &path) { unlink(path.c_str()); }
void remove_dir(const std::string &path) { rmdir(path.c_str()); }

#endif

//A directory with everything in it. What cannot go (a file open elsewhere)
//stays, and so does the directory holding it
void remove_tree(const std::string &dir)
{
    const std::vector<Item> items = list(dir);
    for (size_t i = 0; i < items.size(); i++)
    {
        if (items[i].dir) remove_tree(dir + items[i].name + "/");
        else remove_file(dir + items[i].name);
    }
    remove_dir(dir.substr(0, dir.size() - 1));
}

void clean(const std::string &dir, time_t older_than)
{
    const std::vector<Item> items = list(dir);
    for (size_t i = 0; i < items.size(); i++)
    {
        if (items[i].modified >= older_than) continue;
        if (items[i].dir) remove_tree(dir + items[i].name + "/");
        else remove_file(dir + items[i].name);
    }
}

} // namespace

void cache_touch(const std::string &path)
{
#ifdef _WIN32
    std::string p = path;
    //A directory opens without its trailing separator
    while (p.size() > 3 && (p[p.size() - 1] == '/' || p[p.size() - 1] == '\\')) p.erase(p.size() - 1);
    HANDLE h = CreateFileW(dsk_tools::utf8_to_wide(p).c_str(), FILE_WRITE_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    SetFileTime(h, nullptr, nullptr, &now);
    CloseHandle(h);
#else
    utime(path.c_str(), nullptr);
#endif
}

void cache_cleanup(const std::string &cache_path, unsigned int max_age_days)
{
    if (cache_path.empty()) return;
    const time_t older_than = time(nullptr) - static_cast<time_t>(max_age_days) * 24 * 60 * 60;
    clean(cache_path + "ext/", older_than);
    clean(cache_path + "inline/", older_than);
}
