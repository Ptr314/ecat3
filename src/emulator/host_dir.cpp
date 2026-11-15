// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Directories and files of the host by UTF-8 names

#include "host_dir.h"

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <share.h>
    #include "libs/dsk_tools/src/utils.h"
#else
    #include <cstdlib>
    #include <dirent.h>
    #include <sys/stat.h>
#endif

namespace {

std::string with_separator(const std::string &dir)
{
    if (dir.empty()) return "./";
    const char last = dir[dir.size() - 1];
    return (last == '/' || last == '\\') ? dir : dir + "/";
}

std::string without_separator(const std::string &path)
{
    std::string p = path;
    //A drive root keeps its separator: "C:/"
    while (p.size() > 1 && (p[p.size() - 1] == '/' || p[p.size() - 1] == '\\')
           && !(p.size() == 3 && p[1] == ':'))
        p.erase(p.size() - 1);
    return p;
}

#ifdef _WIN32

//FILETIME counts 100 ns from 1601, time_t seconds from 1970
time_t to_time_t(const FILETIME &ft)
{
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<time_t>((u.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

std::string to_utf8(const std::wstring &w)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

#ifndef IO_REPARSE_TAG_SYMLINK
    #define IO_REPARSE_TAG_SYMLINK      0xA000000CL
#endif
#ifndef IO_REPARSE_TAG_MOUNT_POINT
    #define IO_REPARSE_TAG_MOUNT_POINT  0xA0000003L
#endif

//The tag of a reparse point is in dwReserved0 of the find data
void fill(HostDirEntry &e, const WIN32_FIND_DATAW &fd)
{
    e.dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    e.link = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0
          && (fd.dwReserved0 == (DWORD)IO_REPARSE_TAG_SYMLINK || fd.dwReserved0 == (DWORD)IO_REPARSE_TAG_MOUNT_POINT);
    e.size = e.dir ? 0 : ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
    e.modified = to_time_t(fd.ftLastWriteTime);
}

#endif

} // namespace

#ifdef _WIN32

std::vector<HostDirEntry> host_dir_list(const std::string &dir)
{
    std::vector<HostDirEntry> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(dsk_tools::utf8_to_wide(with_separator(dir) + "*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        const std::wstring w = fd.cFileName;
        if (w == L"." || w == L"..") continue;
        HostDirEntry e;
        e.name = to_utf8(w);
        fill(e, fd);
        out.push_back(e);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

bool host_stat(const std::string &path, HostDirEntry &out)
{
    const std::wstring w = dsk_tools::utf8_to_wide(without_separator(path));
    out = HostDirEntry();
    //The find data carries the tag of a reparse point; a drive root has none
    WIN32_FIND_DATAW fd;
    HANDLE h = (w.find_first_of(L"*?") == std::wstring::npos) ? FindFirstFileW(w.c_str(), &fd) : INVALID_HANDLE_VALUE;
    if (h != INVALID_HANDLE_VALUE) {
        FindClose(h);
        fill(out, fd);
        return true;
    }
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(w.c_str(), GetFileExInfoStandard, &d)) return false;
    out.dir = (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    out.size = out.dir ? 0 : ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
    out.modified = to_time_t(d.ftLastWriteTime);
    return true;
}

std::FILE * host_fopen(const std::string &path, const char * mode)
{
    return _wfsopen(dsk_tools::utf8_to_wide(path).c_str(), dsk_tools::utf8_to_wide(mode).c_str(), _SH_DENYNO);
}

bool host_mkdir(const std::string &path)
{
    return CreateDirectoryW(dsk_tools::utf8_to_wide(without_separator(path)).c_str(), nullptr) != 0;
}

std::string host_absolute(const std::string &path)
{
    HostDirEntry e;
    if (!host_stat(path, e)) return std::string();
    wchar_t buf[4096];
    if (_wfullpath(buf, dsk_tools::utf8_to_wide(without_separator(path)).c_str(), 4096) == nullptr) return std::string();
    std::string s = to_utf8(buf);
    for (size_t i = 0; i < s.size(); i++) if (s[i] == '\\') s[i] = '/';
    return s;
}

#else

std::vector<HostDirEntry> host_dir_list(const std::string &dir)
{
    std::vector<HostDirEntry> out;
    const std::string base = with_separator(dir);
    DIR * d = opendir(base.c_str());
    if (d == nullptr) return out;
    while (dirent * de = readdir(d))
    {
        const std::string name = de->d_name;
        if (name == "." || name == "..") continue;
        HostDirEntry e;
        if (!host_stat(base + name, e)) continue;
        e.name = name;
        out.push_back(e);
    }
    closedir(d);
    return out;
}

bool host_stat(const std::string &path, HostDirEntry &out)
{
    struct stat st;
    if (lstat(without_separator(path).c_str(), &st) != 0) return false;
    out = HostDirEntry();
    out.link = S_ISLNK(st.st_mode);
    //What a link points to decides whether the cache removes it as a directory
    struct stat target;
    const bool dir = out.link ? (stat(path.c_str(), &target) == 0 && S_ISDIR(target.st_mode)) : S_ISDIR(st.st_mode);
    out.dir = dir;
    out.size = out.dir ? 0 : (uint64_t)st.st_size;
    out.modified = st.st_mtime;
    return true;
}

std::FILE * host_fopen(const std::string &path, const char * mode)
{
    return std::fopen(path.c_str(), mode);
}

bool host_mkdir(const std::string &path)
{
    return mkdir(without_separator(path).c_str(), 0777) == 0;
}

std::string host_absolute(const std::string &path)
{
    char * p = realpath(without_separator(path).c_str(), nullptr);
    if (p == nullptr) return std::string();
    const std::string s = p;
    std::free(p);
    return s;
}

#endif
