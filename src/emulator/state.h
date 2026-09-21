// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Saved machine state (.ecats), reading and writing the text
//
// A snapshot is a text file carrying everything the machine needs, so that it
// depends on no other file: the configuration it was taken from, already
// resolved, and the live state of every device. Packed into a .ecats.zip it
// carries its ROMs, key tables and media as ordinary members beside it.
//
//   // eCat3 machine state
//   @ecats 1
//   @version БК0010-01 (Бейсик)
//   @machine BK-0010-01.cfg          // where it was taken from, informative
//
//   @config
//   system { type = bk10  radix = 8 }
//   cpu : 1801vm1 { clock = _3000000 }
//   monitor : rom { image = rom/MONIT10.ROM }
//
//   @state 1
//   system { clock = _1843200000 }
//   cpu : 1801vm1 { PSW = &000200  ~halt = &1 {old = &1} }
//   ram : ram { data = {hex:
//       000000: 40 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00
//       *
//   } }
//
// Both sections are in .cfg syntax and are read by EmulatorConfig, so the
// tokenizer, the ranges, the {extended} blocks, the quoting and the comments
// are the ones every configuration already uses. Numbers always carry their
// base as a prefix ($ hex, & octal, # binary, _ decimal), so the file reads
// the same whatever radix the machine works in.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "config.h"
#include "result.h"

class Interface;
struct SystemData;

//Version of the container, and of the state layout inside it. A file that
//declares a higher one is refused: it was written by a newer eCat3 and we
//would read it wrong rather than not at all
#define ECATS_CONTAINER_VERSION 1
#define ECATS_STATE_VERSION     1

//----------------------------- The file itself -----------------------------//

class MachineStateFile
{
public:
    unsigned int    container_version = 0;
    unsigned int    state_version = 0;
    std::string     version;        //@version, shown as the name of the machine
    std::string     machine;        //@machine, what it was taken from
    std::string     system_type;    //@type and @name, so that the machine
    std::string     system_name;    //chooser needs neither section
    std::string     saved;          //@saved, a time stamp for a human

    std::string     config_text;    //Body of @config
    std::string     state_text;     //Body of @state

    //file_name only labels the error messages
    emulator::Result parse(const std::string &text, const std::string &file_name);

    //The header directives, without either section. The writer builds the
    //file as header() + "@config\n" + config + "\n@state 1\n" + state
    std::string header() const;
};

//------------------------------- Large blobs -------------------------------//

//Where a whole medium goes. The snapshot writer implements it with the
//archive it is building; with no archive (a single text file) it is null and
//the writer falls back to an inline {base64: ...} block
class StateBlobSink
{
public:
    virtual ~StateBlobSink() {}
    //Stores the bytes and answers with the name the state should refer to,
    //which may differ from the suggestion if that one is taken
    virtual std::string put(const std::string &suggested, const uint8_t * data, size_t size) = 0;
};

//---------------------------------- Writer ---------------------------------//

class StateWriter
{
public:
    //base is the radix of the machine: values that belong to it (registers,
    //addresses, port contents) are written in it, counters stay decimal
    StateWriter(unsigned int base, StateBlobSink * blobs = nullptr);

    void begin_device(const std::string &name, const std::string &type);
    void end_device();

    //A value of the machine. base 0 means the machine's own
    void u  (const char * key, uint32_t value, unsigned int bits = 16, unsigned int base = 0);
    //A count, a delay, a cycle number: decimal, because it is ours, not the
    //machine's
    void n  (const char * key, uint32_t value);
    void n64(const char * key, uint64_t value);
    void b  (const char * key, bool value);
    void s  (const char * key, const std::string &value);

    //One element: key[3] = $12
    void u_at(const char * key, unsigned int index, uint32_t value, unsigned int bits = 8);
    //The same for a count of ours rather than a value of the machine
    void n_at(const char * key, unsigned int index, uint32_t value);
    //A short array on one or more lines: key[0-7] = $00, $01, ...
    void array(const char * key, const uint8_t  * data, size_t count, unsigned int per_line = 16);
    void array(const char * key, const uint16_t * data, size_t count, unsigned int per_line = 8);
    void array(const char * key, const uint32_t * data, size_t count, unsigned int per_line = 8);
    void array(const char * key, const bool * data, size_t count, unsigned int per_line = 64);

    //A large buffer as an addressed hex dump with repeated lines folded into
    //a '*'. Still readable and still editable, and an untouched RAM costs
    //almost nothing
    void hex(const char * key, const uint8_t * data, size_t size, unsigned int bits = 8);

    //A whole medium: into the archive as an ordinary file, or inline as
    //base64 when there is no archive
    void blob(const char * key, const std::string &suggested, const uint8_t * data, size_t size);

    //An interface, written by ComputerDevice::save_state() for all of them at
    //once - a device never writes its own lines
    void iface(const Interface &i);

    //An embedded device: every key until pop() is prefixed with "name."
    void push(const char * name);
    void pop();

    //True when nothing was written since begin_device(): such a device is
    //left out of the file altogether
    bool device_is_empty() const { return m_device_empty; }

    const std::string & text() const { return m_text; }

private:
    std::string     m_text;
    std::string     m_device;       //Open block, empty when none
    std::string     m_prefix;
    std::vector<std::string> m_stack;
    unsigned int    m_base;
    StateBlobSink * m_blobs;
    bool            m_device_empty = true;

    void line(const std::string &text);
    std::string num(uint32_t value, unsigned int bits, unsigned int base) const;
    //A value on a line, where _FFFF means "nobody is driving this"
    std::string line_value(unsigned int value, unsigned int mask) const;
};

//---------------------------------- Reader ---------------------------------//

//Everything a device reads about itself. A key that is not there leaves the
//value alone - a device then keeps what reset(true) gave it, which is how a
//state written by an older build still loads. A key this build does not know
//is ignored and reported, which is the other direction
class StateReader
{
public:
    //used marks which parameters were asked for, so that whoever applies
    //the state can report the ones this build does not know. error holds
    //the first value that could not be read at all
    StateReader(EmulatorConfigDevice * device, SystemData * sd,
                std::vector<char> * used = nullptr, std::string * error = nullptr);

    //uint32_t is unsigned int on every toolchain here, so there is no
    //separate overload for it - adding one is a redefinition
    bool u  (const char * key, uint32_t &out) const;
    bool u  (const char * key, uint16_t &out) const;
    bool u  (const char * key, uint8_t  &out) const;
    bool u  (const char * key, int &out) const;
    bool n64(const char * key, uint64_t &out) const;
    bool b  (const char * key, bool &out) const;
    bool s  (const char * key, std::string &out) const;

    bool u_at(const char * key, unsigned int index, uint32_t &out) const;
    //Fills as far as the file goes: elements it does not mention keep their
    //value. False only when no line with this key exists at all
    bool array(const char * key, uint8_t  * data, size_t count) const;
    bool array(const char * key, uint16_t * data, size_t count) const;
    bool array(const char * key, uint32_t * data, size_t count) const;
    bool array(const char * key, bool * data, size_t count) const;

    bool hex(const char * key, uint8_t * data, size_t size, unsigned int bits = 8) const;

    //One line of a device. Only what the file mentions is changed: the value
    //is the line itself, the rest lives in the {old = ..., edge = ...,
    //mode = ...} block and is left out when it equals the value
    bool iface(const std::string &name, unsigned int &value, unsigned int &old_value,
               unsigned int &edge_value, unsigned int &mode) const;

    //A medium: a member of the archive, an inline base64 block, or a file
    //found the usual way
    bool blob(const char * key, std::vector<uint8_t> &out) const;
    bool blob_into(const char * key, uint8_t * data, size_t size) const;

    StateReader sub(const char * name) const;

    //The device this reads, for the messages of whoever builds on it
    const std::string & device_name() const;

    //Set when a value could not be read at all. Never a silent zero
    bool ok() const { return m_error == nullptr || m_error->empty(); }
    const std::string & error() const;

private:
    EmulatorConfigDevice * m_dev;
    SystemData *           m_sd;
    std::string            m_prefix;
    std::vector<char> *    m_used;      //Which parameters were asked for
    std::string *          m_error;

    const EmulatorConfigParameter * find(const std::string &key) const;
    bool value_of(const char * key, uint32_t &out) const;
    void set_error(const std::string &key, const std::string &text) const;
    template<typename T> bool read_array(const char * key, T * data, size_t count) const;
};

//------------------------------ Hex dump ------------------------------------//
//Written and read on their own so that a test can check the pair directly

std::string encode_hex_dump(const uint8_t * data, size_t size, unsigned int bits,
                            const std::string &indent);
//False on a line that is not a dump. Bytes the dump does not cover are left
//alone, so a hand-written fragment patches a buffer rather than replacing it
bool decode_hex_dump(const std::string &text, uint8_t * data, size_t size, unsigned int bits,
                     std::string &error);
