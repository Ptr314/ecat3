// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Wait states chosen by the address of the cycle

#include "wait_map.h"
#include "emulator/utils.h"

WaitMap::WaitMap(InterfaceManager *im, EmulatorConfigDevice *cd):
    ComputerDevice(im, cd)
{
}

emulator::Result WaitMap::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;

    auto find = [this](const std::string & n, WaitSource ** out) -> emulator::Result {
        *out = dynamic_cast<WaitSource*>(im->dm->get_device_by_name(n, false));
        if (*out == nullptr)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{ComputerDevice|" + std::string(QT_TRANSLATE_NOOP("ComputerDevice", "Not a device that adds wait states")) + "} " + name + ": " + n);
        return emulator::Result::ok();
    };

    m_default = nullptr;
    const std::string def = cd->get_parameter("default", false).value;
    if (!def.empty()) {
        res = find(def, &m_default);
        if (!res) return res;
    }

    m_ranges.clear();
    for (size_t i = 0; i < cd->parameters.size(); i++) {
        if (cd->parameters[i].name != "@wait") continue;
        const std::string & range = cd->parameters[i].left_range;
        const size_t p = range.find('-');
        if (range.size() < 3 || p == std::string::npos)
            return emulator::Result::error(emulator::ErrorCode::ConfigError,
                "{MemoryMapper|" + std::string(QT_TRANSLATE_NOOP("MemoryMapper", "Incorrect range for")) + "} " + name);
        Range r;
        r.lo = parse_numeric_value(range.substr(1, p - 1));
        r.hi = parse_numeric_value(range.substr(p + 1, range.size() - p - 2));
        res = find(cd->parameters[i].value, &r.wait);
        if (!res) return res;
        m_ranges.push_back(r);
    }
    build_pages();
    return emulator::Result::ok();
}

void WaitMap::build_pages()
{
    m_pages.assign(PAGES, nullptr);
    for (unsigned int page = 0; page < PAGES; page++) {
        const unsigned int lo = page << PAGE_SHIFT;
        const unsigned int hi = lo + (1u << PAGE_SHIFT) - 1;
        // The first range that touches the block decides it, if it holds all
        // of it; one that holds a part leaves the block to the list
        WaitSource * w = m_default;
        for (const Range & r : m_ranges) {
            if (r.hi < lo || r.lo > hi) continue;
            w = (r.lo <= lo && r.hi >= hi) ? r.wait : static_cast<WaitSource*>(this);
            break;
        }
        m_pages[page] = w;
    }
}

unsigned int WaitMap::wait_states(unsigned int address, bool write, unsigned int offset)
{
    WaitSource * w = source(address);
    return (w != nullptr) ? w->wait_states(address, write, offset) : 0;
}

unsigned int WaitMap::wait_cycle(unsigned int address, unsigned int flags, unsigned int offset)
{
    WaitSource * w = source(address);
    return (w != nullptr) ? w->wait_cycle(address, flags, offset) : 0;
}

ComputerDevice * create_wait_map(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new WaitMap(im, cd);
}
