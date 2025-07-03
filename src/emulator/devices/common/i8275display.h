#ifndef I8275DISPLAY_H
#define I8275DISPLAY_H

#include "emulator/core.h"
#include "emulator/devices/common/i8275.h"
#include "emulator/devices/common/i8257.h"

static uint8_t VG75_8Colors[8][3] = { {0, 0, 0}, {  0,   0, 255}, {  0, 255,   0}, {  0, 255, 255},
                                      {255,   0,   0}, {255,   0, 255}, {255, 255,   0}, {255, 255, 255}
                                    };

class I8275Display:public GenericDisplay
{
private:
    RAM * Memory;
    ROM * Font;
    I8275 * VG75;
    I8257 * DMA;
    unsigned int Channel;
    Interface i_high;             //Font select
    bool AttrDelay;
    unsigned int RGB[3];
    bool RGBInv;

public:
    I8275Display(InterfaceManager *im, EmulatorConfigDevice *cd):
          GenericDisplay(im, cd)
        , i_high(this, im, 1, "high", MODE_R)
    {
        sx = 78*6;
        sy = 30*10;
    }

    virtual void load_config(SystemData *sd) override
    {
        GenericDisplay::load_config(sd);
        Memory = dynamic_cast<RAM*>(im->dm->get_device_by_name(cd->get_parameter("ram").value));
        Font = dynamic_cast<ROM*>(im->dm->get_device_by_name(cd->get_parameter("font").value));
        VG75 = dynamic_cast<I8275*>(im->dm->get_device_by_name(cd->get_parameter("i8275").value));
        DMA = dynamic_cast<I8257*>(im->dm->get_device_by_name(cd->get_parameter("dma").value));
        Channel = parse_numeric_value(cd->get_parameter("channel").value);
        AttrDelay = cd->get_parameter("attr_delay", false).value == "1";
        QString s = cd->get_parameter("rgb", false).value;
        if (s.isEmpty())
        {
            memset(&RGB, 0xFF, sizeof(RGB));
        } else {
            RGBInv = s.at(0) == '^';
            if (RGBInv) s = s.mid(1, s.length()-1);

            for (unsigned int i=0; i<3; i++)
                RGB[i] = QString(s.at(i)).toInt();
        }
    }

    virtual void get_screen_constraints(unsigned int * sx, unsigned int * sy) override
    {
        *sx = this->sx;
        *sy = this->sy;
    }

protected:
    virtual void render_all(bool force_render = false) override
    {
        if (!screen_valid || force_render)
        {
            screen_valid = false;
            was_updated = true;
            i8275_render();
        }
    }

    virtual void i8275_render()
    {
        bool FAReverse, FAUnder, FABlink, FAHigh;
        unsigned int FAColor;

        auto SetAttr = [this, &FAReverse, &FAUnder, &FABlink, &FAHigh, &FAColor](uint8_t v)
        {
            FAReverse = (v & 0x10) != 0;
            FAUnder = (v & 0x20) != 0;
            FABlink = (v & 0x02) != 0;
            FAHigh =  (v & 0x01) != 0;
            if (RGB[0] > 8) {
                FAColor = 7;
            } else {
                FAColor =  ((v >> RGB[2]) & 0x01) +         		//B
                          (((v >> RGB[1]) & 0x01) << 1) +			//G
                          (((v >> RGB[0]) & 0x01) << 2);			//R
                if (RGBInv) FAColor ^= 0x07;
            }
        };

        unsigned int SA = DMA->RgA[Channel*2] + DMA->RgA[Channel*2+1]*256;

        bool Invis = (VG75->RegMode[3] & 0x40) == 0;
        unsigned int CPL = (VG75->RegMode[0] & 0x7F) + 1;           //Chars per line
        unsigned int LPS = (VG75->RegMode[1] & 0x3F) + 1;           //Lines per screen
        unsigned int H = (VG75->RegMode[2] & 0x0F) + 1;             //Character height
        int AddMode = VG75->RegMode[3] >> 7;                        //Font line shift
        unsigned int Count = CPL * LPS;                             //Chars per screen

        if ((CPL < 10) || (LPS < 10))
        {
            //Not initilized yet
            renderer->fill(renderer->MapRGB(0, 0, 0));
            return;
        }

        if ((CPL*6 != sx) || (LPS*H != sy))
        {
            sx = CPL*6;
            sy = LPS*H;
            screen_valid = false;
            was_updated = true;
            return;
        };

        //Font select
        unsigned int FH;
        if (i_high.linked > 0)
            FH = (i_high.value & 1) << 10;
        else
            FH = 0;

        unsigned int P = 0;
        FAReverse = false;
        FAUnder = false;
        FABlink = false;
        FAHigh = false;
        uint8_t NextAttr = 0;
        FAColor = 7;

        uint8_t sign;
        for (unsigned int Lin = 0; Lin < LPS; Lin++)
        {
            bool Blk = false;                                       //Blacking symbols after a special code until line end
            for (unsigned int Col = 0; Col < CPL; Col++)
            {
                if (Blk || (P >= Count)) {
                    sign = 0x80;
                } else {
                    sign = Memory->get_value(SA + P++);
                    if ((sign & 0x80) != 0)
                    {
                        if (sign == 0xF1)
                        {
                            Blk = true;
                            sign = 0x80;
                            P++;
                        } else {
                            if ((sign & 0x40) == 0)
                            {
                                //Field Attributes
                                if (!AttrDelay)
                                {
                                    SetAttr(sign);
                                    NextAttr = 0;
                                } else {
                                    NextAttr = sign;
                                }
                                sign = 0x80;
                            }
                            if (Invis)
                            {
                                sign = Memory->get_value(SA + P++);
                            }
                        }
                    }
                }
                uint8_t C = sign & 0x7F;
                unsigned int Ofs = Col*24;
                for (int i = 0; i < H; i++)
                {
                    uint8_t V;
                    unsigned int Adr = Lin*H + i;
                    int ii = i - AddMode;
                    if (ii < 0) ii += H;
                    if (ii < 8) {
                        V = ~(Font->get_value(FH + C*8 + static_cast<unsigned int>(ii)));
                    } else {
                        V = 0;
                    }
                    if
                    (
                        (
                            (Col == VG75->RegCursor[0]) && (Lin == VG75->RegCursor[1])
                            &&
                            (
                                (((VG75->RegMode[3] & 0x30) == 0x00) && VG75->Blinker) ||			//Blinking block
                                (((VG75->RegMode[3] & 0x30) == 0x10) && (i>7) && VG75->Blinker) ||	//Blinking underline
                                ((VG75->RegMode[3] & 0x30) == 0x20) ||                              //Non-blinking block
                                (((VG75->RegMode[3] & 0x30) == 0x30) && (i>7))                      //Non-blinking underline
                            )
                        )
                        || (FAReverse && (sign != 0x80))
                        || (FABlink && VG75->Blinker && (sign != 0x80))								//Attributes are always black?
                        || (FAUnder && (i>7))
                    ) V = ~V;
                    for (unsigned int k = 0; k <6; k++)
                    {
                        uint8_t c1 = (V >> k) & 1;
                        unsigned int p1 = Ofs + (5-k)*4;
                        uint8_t * base = static_cast<uint8_t *>(render_pixels) + Adr*line_bytes + p1;
                        *(uint32_t*)base = renderer->MapRGB(c1 * VG75_8Colors[FAColor][0], c1 * VG75_8Colors[FAColor][1], c1 * VG75_8Colors[FAColor][2]);
                    }
                }
                if (AttrDelay && (NextAttr != 0))
                {
                    SetAttr(NextAttr);
                    NextAttr = 0;
                }
            }

        }
    }

};

ComputerDevice * create_i8275display(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new I8275Display(im, cd);
}


#endif // I8275DISPLAY_H
