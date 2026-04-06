// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: RAM pages memory manager device

#include <cmath>
#include <cstring>

#include "page_mapper.h"
#include "emulator/utils.h"


PageMapper::PageMapper(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , PagesCount(0)
    , address_mask(_FFFF)
    , i_page(this, im, 8, "page", MODE_R)
    , i_segment(this, im, 8, "segment", MODE_R)
{
    memset(&pages, 0, sizeof(pages));
}

emulator::Result PageMapper::load_config(SystemData *sd)
{
    emulator::Result res = ComputerDevice::load_config(sd);
    if (!res) return res;

    std::string parameter_name, range;
    unsigned int page_id;

    try {
        PagesCount = parse_numeric_value(cd->get_parameter("pages").value);
    } catch (std::exception &e) {
        PagesCount = 0;
    }

    std::fill_n(pages, sizeof(pages)/sizeof(pages[0]), nullptr);

    unsigned last_page_size = 0;

    for (auto & parameter : cd->parameters)
    {
        if (parameter.name == "@page")
        {
            range = parameter.left_range;
            if (range.empty()) {
                return emulator::Result::error(emulator::ErrorCode::ConfigError, "{MemoryMapper|" + std::string(QT_TRANSLATE_NOOP("MemoryMapper", "Incorrect range for")) + "} " + parameter_name);
            }
            page_id = parse_numeric_value(range.substr(1, range.length()-2));
            if (page_id >= PagesCount) PagesCount = page_id + 1;
            pages[page_id] = dynamic_cast<Memory*>(im->dm->get_device_by_name(parameter.value));
            last_page_size = pages[page_id]->get_size();
        }
    }

    PageMask = create_mask(round(log2(PagesCount+1)), 0);

    m_single_frame = false;
    try {
        Frame = parse_numeric_value(cd->get_parameter("frame").value);
    } catch (std::exception &e) {
        Frame = last_page_size;
        m_single_frame = true;
    }
    SegmentMask = create_mask(round(log2(last_page_size / Frame)), 0);
    address_mask = create_mask(round(log2(Frame)), 0);

    return emulator::Result::ok();
}

unsigned int PageMapper::get_value(const unsigned address)
{
    auto device = pages[i_page.value & PageMask];
    if (device) {
        if (m_single_frame) return device->get_value(address);
        const unsigned address_on_device = (i_segment.value & SegmentMask)*Frame + (address & address_mask);
        return device->get_value(address_on_device);
    }
    return _FFFF;
}

void PageMapper::set_value(const unsigned address, const unsigned value, bool force)
{
    auto device = pages[i_page.value & PageMask];
    if (device) {
        if (m_single_frame)
            device->set_value(address, value);
        else {
            const unsigned address_on_device = (i_segment.value & SegmentMask)*Frame + (address & address_mask);
            device->set_value(address_on_device, value);
#ifdef LOG_PAGE_MAPPER
            // logs(("W SEG: " + QString::number(i_segment->value & SegmentMask, 2) + ", " + QString::number(address, 16) + " -> " + QString::number(address_on_device, 16)).toStdString());
#endif
        }
    }
}

ComputerDevice * create_page_mapper(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new PageMapper(im, cd);
}
