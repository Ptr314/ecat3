// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Inter 8275 (КР580ВГ75) CRT controller device

#pragma once

#include "emulator/core.h"

class I8257;

//The widest row the 8275 can be programmed for is 80 characters, but the field
//is seven bits, so a configuration may ask for more and must not overrun
#define I8275_MAX_ROW 128

//Status register bits, in the order the datasheet lists them
#define I8275_ST_FO     0x01        //FIFO overrun
#define I8275_ST_DU     0x02        //DMA underrun
#define I8275_ST_VE     0x04        //Video enabled
#define I8275_ST_IC     0x08        //Improper command
#define I8275_ST_LP     0x10        //Light pen
#define I8275_ST_IR     0x20        //Interrupt request - the end of a frame
#define I8275_ST_IE     0x40        //Interrupt enabled

//What the controller hands a display once a character row has been fetched.
//Keeping it abstract is what lets the display include the controller and not
//the other way round
class I8275RowSink
{
public:
    virtual ~I8275RowSink() {}

    //One character row, as the bytes DMA actually delivered. A row that
    //underran arrives short, and the columns past the end stay blank
    virtual void row_complete(unsigned int row, const uint8_t * data, unsigned int count) = 0;

    //The beam has reached the vertical retrace: the frame is whole
    virtual void frame_complete() = 0;

    //The controller is not displaying anything (Stop Display, or a Reset that
    //was never followed by a Start Display)
    virtual void display_blanked() = 0;
};

class I8275: public AddressableDevice
{
private:
    Interface i_address;
    Interface i_data;
    Interface i_irq;

    uint8_t Mode;                   //Last byte written to the command port
    int RegIndex;                   //Parameter sequencer of the current command
    uint8_t Status;

    //Attached by the display, which is where the config names them
    I8275RowSink * m_sink = nullptr;
    I8257 * m_dma = nullptr;
    unsigned int m_channel = 2;
    AddressableDevice * m_memory = nullptr;

    //Geometry, recomputed whenever the four Reset parameters change
    unsigned int m_cpl;             //Characters per row
    unsigned int m_lps;             //Displayed rows per frame
    unsigned int m_height;          //Raster lines per character row
    unsigned int m_hrtc;            //Horizontal retrace, in character clocks
    unsigned int m_vrtc_rows;       //Vertical retrace, in character rows
    unsigned int m_chars_per_line;
    unsigned int m_rows_total;
    unsigned int m_row_len;         //Character clocks in one character row
    bool m_transparent;             //Field attributes take no display position

    //Where the beam is: the character row, and the position inside it counted
    //in character clocks
    unsigned int m_row_pos;
    unsigned int m_row;

    //Display and DMA state
    bool m_display_on;
    unsigned int m_burst_count;     //Characters per DMA burst
    unsigned int m_burst_space;     //Character clocks between bursts
    unsigned int m_dma_cycle;       //System clocks one DMA cycle costs the CPU
    unsigned int m_dma_cycle_chars; //The same, in character clocks

    uint8_t m_row_buf[2][I8275_MAX_ROW];
    unsigned int m_fill_buf;        //Buffer being filled by DMA
    unsigned int m_fill_len;        //Bytes fetched into it
    unsigned int m_fill_cols;       //Display positions they cover
    bool m_filling;
    bool m_frame_dma;               //DMA was live when this frame started
    unsigned int m_burst_left;
    unsigned int m_dma_delay;       //Character clocks until the next DMA cycle
    unsigned int m_pending;         //1: the next byte is a character after an attribute, 2: after $F1

    unsigned int m_show_len;        //Bytes in the buffer being displayed

    //Blink dividers, counted in frames the way the chip does it
    unsigned int m_frames;
    uint64_t m_dma_bytes;           //DMA cycles since reset, for scripts

    void recalc_geometry();
    bool dma_ready() const;
    void frame_start();
    void start_row_fill();
    void dma_tick();
    void row_start();
    void reset_raster();

public:
    uint8_t RegMode[4];
    uint8_t RegCursor[2];
    bool Blinker;                   //Cursor blink, 16 frames
    bool BlinkerChar;               //Character blink, 32 frames

    I8275(InterfaceManager *im, EmulatorConfigDevice *cd);

    //Called by the display, which owns the "dma", "channel" and "ram" names
    void attach(I8275RowSink * sink, I8257 * dma, unsigned int channel, AddressableDevice * memory);

    unsigned int get_value(unsigned int address) override;
    unsigned int get_direct(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;
    void clock(unsigned int counter) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;

    //Read by the display to lay out the picture
    unsigned int chars_per_row() const { return m_cpl; }
    unsigned int rows_per_screen() const { return m_lps; }
    unsigned int char_height() const { return m_height; }
    bool transparent_attributes() const { return m_transparent; }
    bool display_on() const { return m_display_on; }
};

ComputerDevice * create_i8275(InterfaceManager *im, EmulatorConfigDevice *cd);
