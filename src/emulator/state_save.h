// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Writing a saved state (.ecats / .ecats.zip), header
//
// The writing half of state.h. Collects everything the machine depends on -
// the resolved configuration, the ROMs, the key tables, the drawing, the
// media - and hands back either one archive or one text file with a directory
// of its own beside it. Kept apart from state.cpp so that reading a state
// needs no ZIP writer: the web build reads far more often than it writes.

#pragma once

#include <string>
#include <vector>

#include "config.h"
#include "result.h"
#include "state.h"

struct SystemData;
class DeviceManager;

//Builds the member list of a snapshot. Files go in by their resolved path and
//come back out under a name relative to the archive root; the same file asked
//for twice is stored once
class StateBundle: public StateBlobSink
{
public:
    //folder is where inside the bundle a dependency of this kind lands
    std::string add_file(const std::string &path, const std::string &folder);
    std::string put(const std::string &suggested, const uint8_t * data, size_t size) override;

    //Put in front of every member name. Empty inside an archive, where the
    //members sit beside the .ecats; "<name>.files/" for an unpacked state,
    //whose members live in a directory of its own next to the text. Either
    //way the name in the file is relative to where the .ecats is, so both
    //forms resolve through MachineSource::ext_path without a special case
    void set_prefix(const std::string &prefix) { m_prefix = prefix; }

    struct Member { std::string name; std::vector<uint8_t> data; };
    const std::vector<Member> & members() const { return m_members; }

    const std::string & error() const { return m_error; }

private:
    std::vector<Member> m_members;
    //Resolved source path to the name it got, so that a ROM two devices share
    //is stored once
    std::vector<std::pair<std::string, std::string>> m_by_source;
    std::string m_error;
    std::string m_prefix;

    std::string unique_name(const std::string &wanted);
};

//Everything a snapshot needs from the running emulator. Filled by
//Emulator::save_state(), which is the only caller
struct StateSaveRequest
{
    std::string      file_name;     //.ecats or .ecats.zip
    EmulatorConfig * config;        //The resolved configuration of the machine
    DeviceManager *  dm;
    SystemData *     sd;
    std::string      machine;       //What the machine was loaded from
    std::string      version;       //Name to show for this state
    uint64_t         clock;         //Emulator::clock_counter
    std::vector<uint64_t> domains;  //ClockDomain::norm, already rebased
};

emulator::Result write_state_file(const StateSaveRequest &request);
