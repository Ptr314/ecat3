// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Directories and files of the host by UTF-8 names, header
//
// Plain system calls rather than <filesystem>: the core builds as C++11 on the
// old toolchains too. Names are UTF-8 everywhere; on Windows they become UTF-16
// for the system, or a name in Cyrillic would not open.
#pragma once

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

struct HostDirEntry {
    std::string name;
    bool dir = false;
    // A symbolic link (on Windows a symbolic link or a junction; a cloud file
    // of OneDrive is a reparse point too, but a plain file): whoever must stay
    // inside a directory does not follow it
    bool link = false;
    uint64_t size = 0;
    time_t modified = 0;
};

// The entries of a directory, without "." and "..", in the order the system
// gives them. A directory that cannot be read gives none
std::vector<HostDirEntry> host_dir_list(const std::string &dir);

// What a path is, without following a link at its end; false if it is not there
bool host_stat(const std::string &path, HostDirEntry &out);

// The full path of an existing one ("" if it is not there), with / separators
std::string host_absolute(const std::string &path);

// fopen() by a UTF-8 name. Other processes may open the file at the same time
// (on Windows _wfsopen with no sharing denied, as the disk images do)
std::FILE * host_fopen(const std::string &path, const char * mode);
