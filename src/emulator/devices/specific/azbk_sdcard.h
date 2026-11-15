// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: AZBK controller of the БК (MAXIOL): the microSD card of the AZ controller, header

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "emulator/core.h"

// Карта microSD контроллера AZ для его файловых команд (оглавление,
// монтирование, файлы, снимок экрана): каталог на диске компьютера.
//
// Имена - как у FatFs без длинных имен, которой карту читает STM32 платы:
// путь "0:/DISKS/A.IMG" (или без "0:"), компоненты через / или \, каждый -
// имя 8.3 из больших латинских букв, цифр и знаков, допустимых в коротком
// имени FAT; малые буквы переводятся в большие. Выйти за каталог карты нельзя:
// "." и ".." - не имена, символические ссылки и файлы компьютера, чьи имена не
// ложатся в 8.3, карта не видит, сравнение - без учета регистра. MAXIOL велит
// брать только буквы, цифры и подчеркивание; остальные знаки FAT здесь тоже
// проходят - на диске компьютера они безопасны.
//
// Запись на диск компьютера - только с sd_write = 1. Иначе записанный файл
// остается в слое в памяти: его видят оглавление и чтение, его несет снимок
// состояния, а каталог не меняется. Файл слоя - не больше 32 МБ, весь слой -
// не больше 64 МБ. Новых каталогов карта не заводит: таких команд у AZ нет.
//
// Сетевые диски MAXIOL (N:, R:, P:, S:) отвечают «не готов»: сети нет.
class AzSdCard
{
public:
    // Коды FatFs (вики MAXIOL «API контроллеров AZ», команда 051)
    enum {
        FR_OK = 0, FR_DISK_ERR = 1, FR_NOT_READY = 3, FR_NO_FILE = 4, FR_NO_PATH = 5,
        FR_INVALID_NAME = 6, FR_DENIED = 7, FR_INVALID_DRIVE = 11
    };
    static const uint32_t MAX_FILE = 32u << 20;
    static const uint64_t MAX_OVERLAY = 64ull << 20;

    struct Entry {
        std::string name;               // 8.3, большими буквами
        bool dir = false;
        uint32_t size = 0;
        uint16_t date = 0;              // FAT
        uint16_t time = 0;
    };

    // root - каталог карты (пусто - карты нет)
    void configure(const std::string &root, bool write, unsigned int size_mb);
    bool present() const { return !m_root.empty(); }
    bool writes_host() const { return m_write; }
    void set_write(bool on) { m_write = on; }
    // Файлы слоя - в каталог; что записать не удалось, остается. Сколько
    // осталось
    unsigned int flush();

    // Путь гостя - путь карты "DISKS/A.IMG" ("" - корень) или код ошибки FatFs
    static int parse(const std::string &guest, std::string &path);

    int list(const std::string &dir, std::vector<Entry> &out) const;
    int file_size(const std::string &path, uint32_t &size) const;
    bool read(const std::string &path, uint32_t offset, uint8_t * buffer, uint32_t count) const;
    // Создает файл (или обрезает существующий), как f_open с FA_CREATE_ALWAYS
    int create(const std::string &path, uint16_t date, uint16_t time);
    bool append(const std::string &path, const uint8_t * data, uint32_t count);
    // Что монтировать: файл компьютера (host) или файл слоя (data)
    bool mount_source(const std::string &path, std::string &host, const std::vector<uint8_t> * &data) const;
    // Образ, записанный машиной на смонтированном диске, - в слой целиком
    bool store(const std::string &path, const std::vector<uint8_t> &data, uint16_t date, uint16_t time);
    // Объем и свободное место, МБ (команда 056)
    void sizes(unsigned int &total_mb, unsigned int &free_mb) const;
    unsigned int overlay_files() const { return (unsigned int)m_overlay.size(); }

    void save_state(StateWriter &w) const;
    void load_state(const StateReader &r);

private:
    std::string m_root;                 // с разделителем в конце
    bool m_write = false;
    unsigned int m_size_mb = 4096;

    struct File {
        std::vector<uint8_t> data;
        uint16_t date = 0;
        uint16_t time = 0;
    };
    std::map<std::string, File> m_overlay;
    uint64_t m_overlay_bytes = 0;

    // Последний найденный файл компьютера: 052 и 055 идут по блоку, и каталоги
    // пути не листаются заново на каждые 512 байт
    mutable std::string m_cache_path;
    mutable std::string m_cache_host;

    bool host_dir(const std::string &dir, std::string &host) const;
    bool host_entry(const std::string &host_dir, const std::string &name, std::string &host_name, bool &is_dir, uint64_t &size, int64_t &modified) const;
    bool host_file(const std::string &path, std::string &host) const;
    int host_target(const std::string &path, std::string &host) const;
    uint64_t host_used(const std::string &host_dir, unsigned int depth, unsigned int &budget) const;
};

// Контроллеру, который берет карту: ее вставили или вынули (replaced), или
// сменилось, куда идет запись
class AzSdListener
{
public:
    virtual ~AzSdListener() {}
    virtual void card_changed(bool replaced) = 0;
};

// Гнездо карты microSD (устройство azbk-sdcard, класс sdcard). Окно показывает
// его кнопкой: выбрать каталог, писать ли в него, вынуть карту; выбор
// запоминается для машины, как образы дисков, и записывается в сценарий
// действий. Запись в каталог - это «снятая защита» носителя: protect 1 -
// записи машины остаются в памяти карты, protect 0 - идут в файлы каталога.
//
// Параметры:
//   folder - каталог карты (абсолютный или относительно расширения, файла
//            машины, сценария, рабочего каталога); нет - гнездо пустое
//   write  - 1: записи машины - в файлы каталога; по умолчанию 0, в память
//   size   - объем карты для команды 056, МБ (4096)
//   title  - подпись кнопки в окне
class AZBKSdCard : public ComputerDevice
{
private:
    AzSdCard m_card;
    std::string m_folder;               // полный путь каталога, "" - карты нет
    unsigned int m_size_mb = 4096;
    AzSdListener * m_listener = nullptr;
    void insert(const std::string &folder);

public:
    AZBKSdCard(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;

    AzSdCard & card() { return m_card; }
    void set_listener(AzSdListener * listener) { m_listener = listener; }

    ConfigFields get_config_fields() override;
    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
    std::vector<DeviceCommandInfo> get_device_commands() override;
    emulator::Result send_command(const std::string &command, const std::string &parameters) override;
};

ComputerDevice * create_azbk_sdcard(InterfaceManager *im, EmulatorConfigDevice *cd);
