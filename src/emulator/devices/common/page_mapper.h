#ifndef PAGE_MAPPER_H
#define PAGE_MAPPER_H

#include <QException>

#include "emulator/core.h"
#include "emulator/utils.h"

class PageMapper:public AddressableDevice
{
private:
    Interface * i_page;
    Interface * i_segment;

    unsigned int PagesCount;
    Memory * pages[16];
    unsigned int Frame;
    unsigned int PageMask;
    unsigned int SegmentMask;

public:
    PageMapper(InterfaceManager *im, EmulatorConfigDevice *cd):
        AddressableDevice(im, cd),
        PagesCount(0)
    {
        i_page = create_interface(8, "page", MODE_R);
        i_segment = create_interface(8, "segment", MODE_R);
        memset(&pages, 0, sizeof(pages));
    }

    virtual void load_config(SystemData *sd) override
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
                pages[page_id] =dynamic_cast<Memory*>(im->dm->get_device_by_name(cd->parameters[i].value));
            }
        }

        PageMask = create_mask(round(log2(PagesCount+1)), 0);

        try {
            Frame = parse_numeric_value(cd->get_parameter("frame").value);
        } catch (QException &e) {
            Frame = pages[0]->get_size();
        }

        SegmentMask = create_mask(round(log2(pages[0]->get_size() / Frame)), 0);
    }

    virtual unsigned int get_value(unsigned int address) override
    {
        if (Frame == pages[0]->get_size())
            return pages[i_page->value & PageMask]->get_value(address);
        else
            return pages[i_page->value & PageMask]->get_value((i_segment->value & SegmentMask)*Frame +  address);
    }

    virtual void set_value(unsigned int address, unsigned int value, bool force=false) override
    {
        if (Frame == pages[0]->get_size())
            pages[i_page->value & PageMask]->set_value(address, value);
        else
            pages[i_page->value & PageMask]->set_value((i_segment->value & SegmentMask)*Frame + address, value);
    }
};


ComputerDevice * create_page_mapper(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new PageMapper(im, cd);
}


#endif // PAGE_MAPPER_H
