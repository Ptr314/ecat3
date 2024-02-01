#ifndef PAGE_MAPPER_H
#define PAGE_MAPPER_H

#include "emulator/core.h"

class PageMapper:public AddressableDevice
{
private:
    Interface * i_page;
    Interface * i_segment;

    unsigned int PagesCount;
    Memory * pages[32];
    unsigned int Frame;
    unsigned int PageMask;
    unsigned int SegmentMask;
    unsigned int address_mask;

public:
    PageMapper(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual void load_config(SystemData *sd) override;
    virtual unsigned int get_value(unsigned int address) override;
    virtual void set_value(unsigned int address, unsigned int value, bool force=false) override;
};

ComputerDevice * create_page_mapper(InterfaceManager *im, EmulatorConfigDevice *cd);

#endif // PAGE_MAPPER_H
