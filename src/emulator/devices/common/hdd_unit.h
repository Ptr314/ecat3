// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: A block device on an image file, for a controller that keeps several of them

#pragma once

#include <string>

#include "emulator/core.h"
#include "emulator/devices/common/hdd_image.h"

// Накопитель-файл: образ из блоков по 512 байт, без геометрии. Его берет
// контроллер, у которого таких несколько (AZ - до 32 псевдодисков); у DW и
// контроллера винчестера УК-НЦ образ встроен в сам контроллер. Окно и
// веб-страница находят его как устройство класса hdd: кнопка, вставить,
// вынуть, запретить запись.
//
// Параметры:
//   image         - образ (не обязателен: гнездо может быть пустым)
//   files         - фильтр файлов в окне выбора
//   title         - подпись кнопки
//   write_protect - запись запрещена
//   volatile      - запись в память, файл не меняется (образ из поставки)
class HddUnit: public ComputerDevice, public HddImageOwner
{
private:
    HddImage m_image;
    std::string m_files;
    unsigned int m_reads = 0;
    unsigned int m_writes = 0;
    bool m_volatile_config = false;     // volatile из конфигурации

    emulator::Result load_image(const std::string &file_name);

public:
    static const unsigned int BLOCK_SIZE = HddImage::SECTOR_SIZE;

    HddUnit(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;

    HddImage & hdd_image() override { return m_image; }
    const std::string & hdd_files() const override { return m_files; }

    bool attached() const { return m_image.attached(); }
    // Запись не примется: запрет, или файл только на чтение без записи в память
    bool is_protected() const
        { return m_image.write_protect() || (m_image.is_protected() && !m_image.is_volatile()); }
    // Блоков в образе: неполный последний тоже считается
    uint64_t blocks() const { return m_image.size() / BLOCK_SIZE; }
    bool read_block(uint64_t block, uint8_t * buffer);
    bool write_block(uint64_t block, const uint8_t * buffer);

    // Монтирование командой контроллера (AZ: 004 и 014). Файл компьютера с
    // записью в него (writable) или только в память; или данные - копия
    // файла, которого на диске нет, записи в память. name - имя в окне
    bool attach_file(const std::string &host_path, bool writable);
    bool attach_data(const std::string &name, const std::vector<uint8_t> &data);
    unsigned int writes() const { return m_writes; }
    // Монтирование ставит свою запись в память; снятие, «вынуть» и
    // «вставить» возвращают ту, что в конфигурации
    void detach();

    ConfigFields get_config_fields() override;
    // Образ - дело самого накопителя: снимок несет то, что в нем сейчас, с
    // записанным машиной, а пустое гнездо - пустым
    bool state_owns_file(const std::string &parameter) const override
        { return parameter == "image"; }
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    std::vector<DeviceCommandInfo> get_device_commands() override;
    emulator::Result send_command(const std::string &command, const std::string &parameters) override;
};

ComputerDevice * create_hdd_unit(InterfaceManager *im, EmulatorConfigDevice *cd);
