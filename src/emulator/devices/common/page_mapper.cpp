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
        }
    }
}

std::vector<DeviceFieldInfo> PageMapper::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"page",    "Index of the page the window currently shows",     false});
    r.push_back({"device",  "Name of the memory device behind the window",      false});
    r.push_back({"segment", "Segment of that device, for a windowed page",      false});
    r.push_back({"pages",   "Number of pages that can be selected",             false});
    r.push_back({"frame",   "Window size in bytes, 0 when a whole page is mapped", false});
    r.push_back({"map",     "Every page index and the device it selects",       false});
    return r;
}

bool PageMapper::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    const unsigned int page = i_page.value & PageMask;

    if (field == "page")
    {
        out.numeric = true;
        out.values.push_back(page);
        return true;
    }

    //The one that answers "which bank is in the window right now", which is the
    //question a paging problem always comes down to
    if (field == "device")
    {
        out.numeric = false;
        out.text = (pages[page] != nullptr) ? pages[page]->name : std::string("-");
        return true;
    }

    if (field == "segment")
    {
        out.numeric = true;
        out.values.push_back(m_single_frame ? 0 : (i_segment.value & SegmentMask));
        return true;
    }

    if (field == "pages")
    {
        out.numeric = true;
        out.values.push_back(PagesCount);
        return true;
    }

    if (field == "frame")
    {
        out.numeric = true;
        out.width = 16;
        out.values.push_back(m_single_frame ? 0 : Frame);
        return true;
    }

    if (field == "map")
    {
        out.numeric = false;
        for (unsigned int i = 0; i < PagesCount; i++)
        {
            if (!out.text.empty()) out.text += "\n";
            out.text += "        " + std::to_string(i) + " -> "
                      + ((pages[i] != nullptr) ? pages[i]->name : std::string("-"))
                      + ((i == page) ? "  <- current" : "");
        }
        return true;
    }

    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_page_mapper(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new PageMapper(im, cd);
}
