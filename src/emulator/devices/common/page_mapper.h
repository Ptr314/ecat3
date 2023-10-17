#ifndef PAGE_MAPPER_H
#define PAGE_MAPPER_H

#include "emulator/core.h"

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
        //TODO: page mapper
    }

    virtual unsigned int get_value(unsigned int address) override
    {
        return 0;
    }
    virtual void set_value(unsigned int address, unsigned int value) override
    {

    }


};


ComputerDevice * create_page_mapper(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new PageMapper(im, cd);
}


#endif // PAGE_MAPPER_H
