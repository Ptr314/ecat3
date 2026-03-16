// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: GMD70 (Электроника-ГМД70) FDC device

#include "gmd70.h"

#define GMD70_FILL_BUFFER               0
#define GMD70_GET_BUFFER                1
#define GMD70_WRITE_SECTOR              2
#define GMD70_READ_SECTOR               3
#define GMD70_SPECIFIC                  4   // Format track for СМ5631, set density for ГМД7012
#define GMD70_READ_STATUS               5
#define GMD70_WRITE_SECTOR_WITH_MARK    6
#define GMD70_READ_ERROR                7

#define GMD70_ERROR_SECTOR_NOT_FOUND    7


GMD70::GMD70(InterfaceManager *im, EmulatorConfigDevice *cd) :
      FDC(im, cd)
    , i_select(this, im, 1, "select", MODE_W)
{
    std::fill_n(m_buffer, 128, 0);
}

void GMD70::reset(const bool cold)
{
    FDC::reset(cold);
    reset_fdc();
}

void GMD70::reset_fdc()
{
    m_ints_en = false;
    m_selected_drive = 0;
    m_reserved = false;
    m_command = 0;
    m_trq = false;
    m_error = -1;
    m_done = false;
    i_select.change(m_selected_drive);
    auto fdd = m_drives[m_selected_drive];
    if (fdd->get_loaded())
    {
        fdd->SeekSector(1, 1);
        for (unsigned int i = 0; i < 128; i++) m_buffer[i] = fdd->ReadNextByte();
    }
    m_counter = 0;
}

void GMD70::load_config(SystemData *sd)
{
    FDC::load_config(sd);
    QString s;
    try {
        s = cd->get_parameter("drives").value;
    } catch (QException &e) {
        QMessageBox::critical(0, GMD70::tr("Error"), GMD70::tr("Incorrect fdd list for '%1'").arg(name));
        throw QException();
    }

    QStringList parts = s.split('|', skip_empty_parts);
    m_drives_count = parts.size();
    for (unsigned int i = 0; i < m_drives_count; i++)
        m_drives[i] = dynamic_cast<FDD*>(im->dm->get_device_by_name(parts[i]));

    reset_fdc();
}

bool GMD70::get_busy()
{
    return m_busy;
}

unsigned int GMD70::get_selected_drive()
{
    return m_selected_drive;
}

unsigned GMD70::get_value(const unsigned address)
{
    if ((address & 1) == 0) {
        // Status register
        // std::cout << "GMD70::status" << std::endl;
        return    (m_trq          ? 0x80 : 0)
                | (m_ints_en      ? 0x40 : 0)
                | ((m_error >= 0) ? 0x20 : 0)
                | (m_done         ? 0x10 : 0)
                | (m_error & 0xF);
    }
    // Data register
    // std::cout << "GMD70::read data" << std::endl;
    if (m_command == GMD70_GET_BUFFER) {
        m_data = m_buffer[m_counter];
        m_counter = (m_counter + 1) % 128;
    }
    return m_data;
}

void GMD70::set_value(const unsigned  address, const unsigned  value, bool force)
{
    if ((address & 1) == 0) {
        // Command register
        std::cout << "GMD70::command " << std::hex << (value & 0xFF) << std::endl;
        if (value & 0x80) {
            reset_fdc();
            m_done = true;
            return;
        }
        m_ints_en = (value & 0x40) != 0;
        m_reserved = (value & 0x20) != 0;   // TODO: check if this bit should do something
        m_selected_drive = (value >> 4) & 1;
        m_command = (value >> 1) & 0x07;
        bool start = (value & 1) != 0;

        i_select.change(m_selected_drive);
        m_done = false;
        if (start) do_command();
    } else {
        // Data register
        std::cout << "GMD70::write data " << std::hex << (value & 0xFF) << std::endl;
        m_data = value & 0xFF;
        if (m_command == GMD70_READ_SECTOR) {
            if (m_command_counter < 2) {
                m_command_buffer[m_command_counter++] = m_data;
                if (m_command_counter == 2) {
                    m_done = true;
                    m_trq = false;
                    auto fdd = m_drives[m_selected_drive];
                    if (fdd->get_loaded())
                    {
                        fdd->SeekSector(m_command_buffer[1], m_command_buffer[0]);
                        for (unsigned int i = 0; i < 128; i++) m_buffer[i] = fdd->ReadNextByte();
                        m_counter = 0;
                    }
                }
            }
        }
    }
}

void GMD70::do_command()
{
    if (m_command == GMD70_GET_BUFFER) {
        m_counter = 0;
        m_trq = true;
        m_done = true;
        if (!m_drives[m_selected_drive]->get_loaded()) m_error = GMD70_ERROR_SECTOR_NOT_FOUND;
    }
    if (m_command == GMD70_READ_SECTOR) {
        m_command_counter = 0;
        m_trq = true;
    }
}

ComputerDevice * create_GMD70(InterfaceManager *im, EmulatorConfigDevice *cd){
    return new GMD70(im, cd);
}
