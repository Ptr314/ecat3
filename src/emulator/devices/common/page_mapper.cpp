#include <QException>

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

void PageMapper::load_config(SystemData *sd)
{
    ComputerDevice::load_config(sd);

    QString parameter_name, range;
    unsigned int page_id;

    for (unsigned int i = 0; i < cd->parameters_count; i++)
    {
        parameter_name = cd->parameters[i].name;
        if (parameter_name == "@page")
        {
            range = cd->parameters[i].left_range;
            if (range.isEmpty()) {
                QMessageBox::critical(0, MemoryMapper::tr("Error"), MemoryMapper::tr("Incorrect range for '%1'").arg(parameter_name));
                return;
            }
            page_id = parse_numeric_value(range.mid(1, range.length()-2));
            if (page_id >= PagesCount) PagesCount = page_id + 1;
            pages[page_id] = dynamic_cast<Memory*>(im->dm->get_device_by_name(cd->parameters[i].value));
        }
    }

    PageMask = create_mask(round(log2(PagesCount+1)), 0);

    try {
        Frame = parse_numeric_value(cd->get_parameter("frame").value);
    } catch (QException &e) {
        Frame = pages[0]->get_size();
    }
    SegmentMask = create_mask(round(log2(pages[0]->get_size() / Frame)), 0);
    address_mask = create_mask(round(log2(Frame)), 0);
}

unsigned int PageMapper::get_value(unsigned int address)
{
    if (Frame == pages[0]->get_size())
        return pages[i_page.value & PageMask]->get_value(address);
    else {
        unsigned int address_on_device = (i_segment.value & SegmentMask)*Frame + (address & address_mask);
        return pages[i_page.value & PageMask]->get_value(address_on_device);
    }
}

void PageMapper::set_value(unsigned int address, unsigned int value, bool force)
{
    if (Frame == pages[0]->get_size())
        pages[i_page.value & PageMask]->set_value(address, value);
    else {
        unsigned int address_on_device = (i_segment.value & SegmentMask)*Frame + (address & address_mask);
        pages[i_page.value & PageMask]->set_value(address_on_device, value);
#ifdef LOG_PAGE_MAPPER
        logs("W SEG: " + QString::number(i_segment->value & SegmentMask, 2) + ", " + QString::number(address, 16) + " -> " + QString::number(address_on_device, 16));
#endif
    }
}

ComputerDevice * create_page_mapper(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new PageMapper(im, cd);
}
