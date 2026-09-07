// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Inter 8275 (КР580ВГ75) CRT controller device

#include <cstring>

#include "i8275.h"
#include "i8257.h"
#include "emulator/utils.h"

//Burst space codes of the Start Display command, in character clocks
static const unsigned int I8275_BURST_SPACE[8] = {0, 8, 16, 24, 32, 40, 48, 55};

//The 8275 blinks the cursor every 16 frames and blinking characters every 32
#define I8275_CURSOR_BLINK  16
#define I8275_CHAR_BLINK    32

I8275::I8275(InterfaceManager *im, EmulatorConfigDevice *cd):
      AddressableDevice(im, cd)
    , i_address(this, im, 1, "address", MODE_R)
    , i_data(this, im, 8, "data", MODE_R)
    , i_irq(this, im, 1, "irq", MODE_W)
    , Mode(0)
    , RegIndex(0)
    , Status(0)
    , Blinker(false)
    , BlinkerChar(false)
{
    m_clocked = true;   //clock() is overridden here
    memset(&RegMode, 0, sizeof(RegMode));
    memset(&RegCursor, 0, sizeof(RegCursor));
    memset(&m_row_buf, 0, sizeof(m_row_buf));
    m_dma_cycle = 4;
    recalc_geometry();
    reset_raster();
}

void I8275::attach(I8275RowSink * sink, I8257 * dma, unsigned int channel, AddressableDevice * memory)
{
    m_sink = sink;
    m_dma = dma;
    m_channel = channel;
    m_memory = memory;
}

//Everything the raster depends on comes out of the four Reset parameters, so it
//is worked out once per programming rather than per character clock
void I8275::recalc_geometry()
{
    m_cpl        = (RegMode[0] & 0x7F) + 1;
    m_lps        = (RegMode[1] & 0x3F) + 1;
    m_height     = (RegMode[2] & 0x0F) + 1;
    m_hrtc       = ((RegMode[3] & 0x0F) + 1) * 2;
    m_vrtc_rows  = ((RegMode[1] >> 6) & 0x03) + 1;
    m_transparent = (RegMode[3] & 0x40) == 0;

    if (m_cpl > I8275_MAX_ROW) m_cpl = I8275_MAX_ROW;

    m_chars_per_line = m_cpl + m_hrtc;
    m_rows_total     = m_lps + m_vrtc_rows;
    m_row_len        = m_chars_per_line * m_height;
}

bool I8275::dma_ready() const
{
    return m_dma != nullptr && m_memory != nullptr && m_dma->channel_enabled(m_channel);
}

//A frame begins at the first line of the vertical retrace, where the row that
//will be displayed first is fetched before it is needed. Whether the frame is
//fetched at all is decided here and once: a row started in the middle of a
//frame would take its bytes from wherever the address counter happened to be,
//and since the counter is the only thing tying the memory to the screen, the
//whole picture would stay shifted by that much for as long as the machine runs
void I8275::frame_start()
{
    m_frame_dma = m_display_on && dma_ready();
    if (m_frame_dma)
        start_row_fill();
    else
    if (m_display_on)
        //Started, but with nothing to show: the frame underruns from its first
        //character, which is what a ВГ75 reports while the tape routines keep
        //the DMA channel off
        Status |= I8275_ST_DU;
}

void I8275::reset_raster()
{
    m_row_pos = 0;
    m_row = m_lps;
    m_filling = false;
    m_frame_dma = false;
    m_fill_buf = 0;
    m_fill_len = 0;
    m_fill_cols = 0;
    m_show_len = 0;
    m_burst_left = 0;
    m_dma_delay = 0;
    m_pending = 0;

    frame_start();
}

void I8275::reset(bool cold)
{
    AddressableDevice::reset(cold);
    Mode = 0;
    RegIndex = 0;
    Status = 0;
    m_display_on = false;
    m_frame_dma = false;
    m_burst_count = 1;
    m_burst_space = 0;
    m_frames = 0;
    m_dma_bytes = 0;
    Blinker = false;
    BlinkerChar = false;
    memset(&RegMode, 0, sizeof(RegMode));
    memset(&RegCursor, 0, sizeof(RegCursor));
    recalc_geometry();
    reset_raster();
    i_irq.change(0);
}

emulator::Result I8275::load_config(SystemData *sd)
{
    //Without the base call the ~ connections, sd and the reset behaviour of
    //this device are silently ignored
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    //System clocks one DMA cycle of the ВТ57 takes the bus away for. The same
    //number in character clocks paces the burst, so that a burst of eight is as
    //long on the screen as it is on the processor
    m_dma_cycle = read_confg_value(cd, "dma_cycle", false, (unsigned int)4);
    if (m_dma_cycle < 1) m_dma_cycle = 1;

    m_dma_cycle_chars = m_dma_cycle * clock_miltiplier / clock_divider;
    if (m_dma_cycle_chars < 1) m_dma_cycle_chars = 1;

    return emulator::Result::ok();
}

unsigned int I8275::get_value(unsigned int address)
{
    if ((address & 1) == 0)
    {
        //Data: the light pen registers, which nothing here uses
        return 0;
    }

    //Status. Everything but the two level flags is cleared by the read, which
    //is how the РК86 monitor waits for a frame: one read to clear, then a loop
    unsigned int r = Status;
    Status &= (I8275_ST_IE | I8275_ST_VE);
    if ((r & I8275_ST_IR) != 0) i_irq.change(0);
    return r;
}

unsigned int I8275::get_direct(unsigned int address)
{
    //An inspection must not clear what the guest has not read yet
    return ((address & 1) == 0)?0:Status;
}

void I8275::set_value(unsigned int address, unsigned int value, bool force)
{
    unsigned int a = address & 1;
    if (a == 0)
    {
        //Parameter byte of the command in progress
        switch (Mode & 0xE0)
        {
        case 0x00:
            //Reset: four parameters describing the raster
            if (RegIndex < 4)
            {
                RegMode[RegIndex] = value & 0xFF;
                RegIndex++;
                if (RegIndex == 4) recalc_geometry();
            }
            break;
        case 0x80:
            //Load cursor: column, then row
            if (RegIndex < 2)
            {
                RegCursor[RegIndex] = value & 0xFF;
                RegIndex++;
            }
            break;
        default:
            break;
        }
        return;
    }

    Mode = value & 0xFF;
    RegIndex = 0;

    switch (Mode & 0xE0)
    {
    case 0x00:
        //Reset: the display and the DMA stop until a Start Display arrives
        m_display_on = false;
        Status &= ~I8275_ST_VE;
        reset_raster();
        if (m_sink != nullptr) m_sink->display_blanked();
        break;

    case 0x20:
        //Start Display. The burst count is a power of two in the low two bits,
        //the space between bursts a code in the next three
        m_burst_count = 1u << (Mode & 0x03);
        m_burst_space = I8275_BURST_SPACE[(Mode >> 2) & 0x07];
        m_display_on = true;
        Status |= I8275_ST_VE;
        Status &= ~I8275_ST_DU;
        break;

    case 0x40:
        //Stop Display. No ROM of the four machines uses it - they blank the
        //screen by disabling the DMA channel instead - but the chip has it
        m_display_on = false;
        Status &= ~(I8275_ST_VE | I8275_ST_IE);
        if (m_sink != nullptr) m_sink->display_blanked();
        break;

    case 0xA0:
        Status |= I8275_ST_IE;
        break;

    case 0xC0:
        Status &= ~I8275_ST_IE;
        i_irq.change(0);
        break;

    case 0xE0:
        //Preset counters: the raster starts again from the top left. Every ROM
        //here sends it right after Start Display to line the picture up with
        //the DMA channel it is about to enable, so it must not blank anything
        reset_raster();
        break;

    default:
        //Read light pen: the registers are not modelled, the command is not an
        //error either
        break;
    }
}

//Begins fetching the row that will be displayed next. The controller asks for
//as many bytes as it takes to cover a row of display positions, which is more
//than one byte per position when field attributes are transparent
void I8275::start_row_fill()
{
    m_fill_buf ^= 1;
    m_fill_len = 0;
    m_fill_cols = 0;
    m_filling = true;
    m_burst_left = m_burst_count;
    m_dma_delay = 0;
    m_pending = 0;
}

//One transfer, called by clock() only when the next one is due
void I8275::dma_tick()
{
    if (!dma_ready())
    {
        //The channel was switched off in the middle of a frame - which is how
        //the tape routines blank the screen. The rest of the row is lost, and
        //nothing more is fetched until a frame starts with the channel live
        Status |= I8275_ST_DU;
        m_filling = false;
        m_frame_dma = false;
        return;
    }

    unsigned int addr = m_dma->dma_next(m_channel);
    uint8_t v = static_cast<uint8_t>(m_memory->get_value(addr) & 0xFF);

    if (m_fill_len < I8275_MAX_ROW) m_row_buf[m_fill_buf][m_fill_len++] = v;
    m_dma_bytes++;

    //The processor is off the bus for the length of the transfer
    if (cpu != nullptr) cpu->hold(m_dma_cycle);

    //How many display positions the bytes fetched so far cover. This has to
    //match how I8275Display walks the same bytes, or the row would be fetched
    //to a different length than it is drawn: a byte with bit 7 set pulls in the
    //character after it when field attributes are transparent, and the special
    //code $F1 pulls in one more byte and blanks the rest of the row
    switch (m_pending)
    {
    case 1:                             //Character that followed an attribute
        m_pending = 0;
        m_fill_cols++;
        break;
    case 2:                             //Byte that followed the end-of-row code
        m_pending = 0;
        m_fill_cols = m_cpl;
        break;
    default:
        if (v == 0xF1) { m_pending = 2; m_fill_cols++; }
        else if ((v & 0x80) != 0 && m_transparent) m_pending = 1;
        else m_fill_cols++;
        break;
    }

    if (m_fill_len >= I8275_MAX_ROW || (m_pending == 0 && m_fill_cols >= m_cpl))
    {
        m_filling = false;
        return;
    }

    //Character clocks until the next transfer. clock() counts them down before
    //calling here again, so this is the whole period, not one less
    m_dma_delay = m_dma_cycle_chars;
    if (--m_burst_left == 0)
    {
        m_burst_left = m_burst_count;
        m_dma_delay += m_burst_space;
    }
}

//Called when the beam reaches the first character clock of a character row
void I8275::row_start()
{
    if (m_row < m_lps)
    {
        //The buffer filled during the previous row is what this one shows
        m_show_len = m_fill_len;
        if (m_sink != nullptr)
            m_sink->row_complete(m_row, m_row_buf[m_fill_buf], m_show_len);
    } else
    if (m_row == m_lps)
    {
        //Start of the vertical retrace: the frame is whole and the interrupt,
        //which is what the monitors poll, goes up
        Status |= I8275_ST_IR;
        if ((Status & I8275_ST_IE) != 0) i_irq.change(1);
        if (m_sink != nullptr) m_sink->frame_complete();

        m_frames++;
        Blinker     = ((m_frames / I8275_CURSOR_BLINK) & 1) != 0;
        BlinkerChar = ((m_frames / I8275_CHAR_BLINK) & 1) != 0;

        //And the next frame begins: its first row is fetched here
        frame_start();
        return;
    }

    //The row after this one is fetched while this one is on the screen
    if (m_row + 1 < m_lps && m_frame_dma) start_row_fill();
}

void I8275::clock(unsigned int counter)
{
    //Before the guest has programmed the raster there is nothing to count
    if (m_cpl < 2 || m_lps < 2 || m_row_len < 2) return;

    while (counter > 0)
    {
        //A transfer that is due happens before the beam moves on
        if (m_filling && m_dma_delay == 0) dma_tick();

        //Only two things can happen inside a character row - the next transfer
        //and the end of the row - so the beam jumps straight to the nearer of
        //them. Stepping a character at a time would run this loop at the
        //character clock, over a million times per emulated second
        unsigned int step = m_row_len - m_row_pos;
        if (step > counter) step = counter;

        if (m_filling)
        {
            if (m_dma_delay == 0) {
                //A transfer every character clock: nothing to skip over
                if (step > 1) step = 1;
            } else {
                if (m_dma_delay < step) step = m_dma_delay;
                m_dma_delay -= step;
            }
        }

        m_row_pos += step;
        counter -= step;

        if (m_row_pos >= m_row_len)
        {
            m_row_pos = 0;
            if (++m_row >= m_rows_total) m_row = 0;
            row_start();
        }
    }
}

std::vector<DeviceFieldInfo> I8275::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"mode",        "The four parameter bytes of the Reset command",    false});
    r.push_back({"cursor",      "Cursor column and row",                            false});
    r.push_back({"command",     "Last byte written to the command port",            false});
    r.push_back({"geometry",    "Chars per row, rows, lines per row, HRTC, VRTC",    false});
    r.push_back({"status",      "Status register, without clearing it",             false});
    r.push_back({"display",     "1 while the display is started",                   false});
    r.push_back({"burst",       "DMA burst count and the space between bursts",     false});
    r.push_back({"frame_rate",  "Frames per second the programmed raster gives",    false});
    r.push_back({"frames",      "Frames since reset",                               false});
    r.push_back({"dma_bytes",   "DMA cycles the controller has taken since reset",  false});
    r.push_back({"row",         "Character row the beam is on",                     false});
    return r;
}

bool I8275::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "mode" || field == "cursor" || field == "command" || field == "status")
    {
        out.numeric = true;
        out.width = 8;
        if (field == "mode")
            for (unsigned int i = 0; i < 4; i++) out.values.push_back(RegMode[i]);
        else if (field == "cursor")
            for (unsigned int i = 0; i < 2; i++) out.values.push_back(RegCursor[i]);
        else if (field == "command")
            out.values.push_back(Mode);
        else
            out.values.push_back(Status);
        return true;
    }

    //The raster the guest asked for, in the order a datasheet lists it:
    //characters per row, rows per frame, lines per row, then the two retrace
    //counts that decide the frame rate
    if (field == "geometry")
    {
        out.numeric = true;
        out.values.push_back(m_cpl);
        out.values.push_back(m_lps);
        out.values.push_back(m_height);
        out.values.push_back(m_hrtc);
        out.values.push_back(m_vrtc_rows);
        return true;
    }

    if (field == "burst")
    {
        out.numeric = true;
        out.values.push_back(m_burst_count);
        out.values.push_back(m_burst_space);
        return true;
    }

    if (field == "frame_rate")
    {
        //Character clocks, not system clocks: the controller is normally
        //divided down from the processor with the clock= parameter
        uint64_t frame = (uint64_t)m_chars_per_line * m_rows_total * m_height;
        uint64_t cclk = (uint64_t)m_system_clock * clock_miltiplier / clock_divider;
        out.numeric = true;
        out.width = 16;
        out.values.push_back((frame > 0)?(unsigned int)(cclk / frame):0);
        return true;
    }

    if (field == "frames" || field == "dma_bytes" || field == "display" || field == "row")
    {
        out.numeric = true;
        if (field == "frames")         { out.width = 32; out.values.push_back(m_frames); }
        else if (field == "dma_bytes") { out.width = 32; out.values.push_back((unsigned int)m_dma_bytes); }
        else if (field == "display")   out.values.push_back(m_display_on?1:0);
        else                           out.values.push_back(m_row);
        return true;
    }

    return AddressableDevice::get_field(field, from, to, out);
}

ComputerDevice * create_i8275(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new I8275(im, cd);
}
