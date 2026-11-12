// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: The cache of unpacked archives and inline data, header
//
// Emulator::cache_path holds what a machine file brings along: ext/ the
// unpacked .ext.zip and .ecats.zip archives, a directory per archive version,
// inline/ the {base64: ...} data of extensions, a file per content, and
// launch/ the programs taken out of an archive for a platform launch. They
// are needed only while the machine runs - a drive that wrote into a copy gets
// the original back on the next load - so what nobody has used for a while is
// removed. The cache is shared by every running emulator (a parallel test run
// loads one archive in several processes at once), which is why it goes by
// age and not by "mine, now that I exit".
//
// Plain system calls rather than <filesystem>: the core builds as C++11 on the
// old toolchains too.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

//Sets the modification time of a file or a directory to now. A cached entry
//that is used again is touched, so that the cleanup keeps it
void cache_touch(const std::string &path);

//Removes from cache_path's ext/, inline/ and launch/ every entry not touched
//for max_age_days. Quietly: an entry another process holds open stays for the
//next time, and nothing goes to stderr
void cache_cleanup(const std::string &cache_path, unsigned int max_age_days);

//Creates every directory along the path that is not there yet
void cache_make_dirs(const std::string &path);

//Writes a file of the cache. The cache is shared by every running emulator: a
//parallel test run loads the same archive several times at once. A file that
//already holds these bytes is left alone, so only the first load writes and
//nobody truncates a file another process is reading. A copy a drive has
//written into differs and is put back
bool cache_write(const std::string &path, const uint8_t * data, size_t size);

//How long an unused entry stays
const unsigned int CACHE_MAX_AGE_DAYS = 30;
