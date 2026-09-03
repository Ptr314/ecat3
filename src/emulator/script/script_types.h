// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Scripting engine data types, header

#pragma once

#include <string>
#include <vector>

// One parsed line of a script.
//
// The structure is the unit of execution of the engine. It is filled in by the
// parser, but can also be built directly by an external driver such as an MCP
// server, so a script file is not the only possible input.
struct ScriptCommand {
    unsigned int                verb;       //One of SCRIPT_CMD_*
    unsigned int                line;       //Source line number, for messages
    std::vector<std::string>    args;       //Arguments, quotes already stripped
    std::string                 device;     //LOG / COMMAND / WAITFOR: device name
    std::string                 member;     //LOG / COMMAND / WAITFOR: field or command
    std::string                 params;     //LOG / COMMAND / WAITFOR: raw text in brackets

    ScriptCommand(): verb(0), line(0) {}
};

#define SCRIPT_CMD_NONE     0
#define SCRIPT_CMD_MACHINE  1
#define SCRIPT_CMD_WAIT     2
#define SCRIPT_CMD_KEY      3
#define SCRIPT_CMD_SCREEN   4
#define SCRIPT_CMD_LOGDEFS  5
#define SCRIPT_CMD_LOG      6
#define SCRIPT_CMD_COMMAND  7
#define SCRIPT_CMD_EXIT     8
#define SCRIPT_CMD_RESET    9
#define SCRIPT_CMD_TYPE     10
#define SCRIPT_CMD_LOAD     11
#define SCRIPT_CMD_PRINT    12
#define SCRIPT_CMD_LOGFILE  13
#define SCRIPT_CMD_WAITFOR  14

//Comparison operators of WAITFOR
#define SCRIPT_OP_EQ        0
#define SCRIPT_OP_NE        1
#define SCRIPT_OP_LT        2
#define SCRIPT_OP_LE        3
#define SCRIPT_OP_GT        4
#define SCRIPT_OP_GE        5

//Default delays of KEY and TYPE, ms
#define SCRIPT_KEY_DELAY    50
#define SCRIPT_KEY_HOLD     50

//Default WAITFOR timeout, ms
#define SCRIPT_WAITFOR_TIMEOUT 10000
