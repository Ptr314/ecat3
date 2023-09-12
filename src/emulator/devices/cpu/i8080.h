#ifndef I8080_H
#define I8080_H

#include "emulator/core.h"

struct I8080Context {
    //TODO: Implement
};

class I8080: public CPU
{
private:
    Interface * inmi;
    Interface * iint;
    Interface * iinte;
    Interface * im1;
protected:
    virtual unsigned int get_pc();
public:
    I8080Context context;

    I8080(InterfaceManager *im, EmulatorConfigDevice *cd);
    virtual unsigned int execute();
};

ComputerDevice * create_i8080(InterfaceManager *im, EmulatorConfigDevice *cd);


#endif // I8080_H
