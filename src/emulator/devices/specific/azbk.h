// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: AZBK controller of the БК (MAXIOL): memory mapper and board registers, header

#pragma once

#include <string>
#include <vector>

#include "emulator/core.h"

// Контроллер AZBK (MAXIOL) в разъеме МПИ БК0011М: маппер его 32 МБ памяти,
// регистры платы и пуск. Экран и блиттер (azbk-vga), звук (azbk-sound) и
// дисковый контроллер AZ (azbk-disk) - отдельные устройства.
//
// Все по описанию MAXIOL «Модель управления памятью в AZ V2» (forum.maxiol.com,
// тема 5556, правки 2023-04-13, с кусками Verilog его ПЛИС) и исходникам
// стартового ПЗУ AZBOOT (docs-external/BK/AZBK).
//
// Память AZ - 8192 страницы по 4 КБ (номер 13 бит): 0-037 тень памяти БК11М
// (страница БК c - страницы 4c..4c+3), 040-077 служебная (040 логотип),
// 0100-0177 «ПЗУ» - слоты R00..R63, их пишет только сам контроллер,
// 0200-0377 эмуляция СМК-512, дальше свободная. Адресное пространство БК
// поделено на 16 окон по 4 КБ (окно 15 кончается на 176777): 177300+2n -
// страница окна n, 177340 - окна, которые отвечают (RPLY), 177342 - только
// чтение, 177344 - тень: окно не отвечает, но запись в память БК копирует
// себе. 177340 старше 177344.
//
// Устройство стоит в диспетчере одним диапазоном 000000-176777 с routed = 1
// и по каждому окну решает (route()): ответить само, ответить вместе с
// памятью БК (на шине - ИЛИ, конфликт; запись получают оба), оставить адрес
// БК, только подслушать запись (ROUTE_SNOOP: ответ и время цикла - памяти БК)
// или оставить магистраль без ответа (память БК отключена доработкой 037).
// Регистры отвечают по своим адресам (azbk[177300] и т.д.); запись в 177716 и
// 177130 устройство перехватывает, не мешая плате БК, а чтение 177716
// складывает с ее регистром (or_read).
//
// Параметры:
//   ram          - устройство ram на 32 МБ (state_pages = 1), память AZ
//   machine      - bk11: какая память БК0011М стоит под окнами
//   modification - none: серийная БК (окна 0-7 только в тени); vp037 -
//                  доработка с управлением 037 (разряд 14 177346 = 1, разряд 9
//                  отключает память БК 000000-137777)
//   fpga_version - что отвечает 177370 (версия прошивки ПЛИС)
//   rom_NN       - файл слота ПЗУ NN (десятичный, как R00..R63 в AZ.INI
//                  MAXIOL): страница 0100+NN; файл длиннее 4 КБ занимает
//                  следующие слоты
//   logo         - файл логотипа, страница 040
//   ~bk_sys      - разряды 0-4 системного регистра БК (port-sys.alt[0-4]):
//                  что плата БК ставит в окно 100000-137777
class AZBK: public AddressableDevice
{
public:
    // Не PAGE_SIZE: так называется макрос limits.h у Emscripten (и у других libc)
    static const unsigned int PAGE_BYTES = 4096;
    static const unsigned int PAGES = 8192;             // 32 МБ
    static const unsigned int ROM_PAGE = 0100;          // слот R00
    static const unsigned int ROM_PAGES = 0100;         // R00..R63
    static const unsigned int LOGO_PAGE = 040;

    // Разряды 177346
    static const unsigned int CTRL_50HZ    = 1u << 2;
    static const unsigned int CTRL_V100    = 1u << 3;
    static const unsigned int CTRL_ROM11   = 1u << 5;
    static const unsigned int CTRL_CPU     = 7u << 6;
    static const unsigned int CTRL_037OFF  = 1u << 9;
    static const unsigned int CTRL_014OFF  = 1u << 10;
    static const unsigned int CTRL_BK11EMU = 1u << 11;
    static const unsigned int CTRL_BK10    = 1u << 12;
    static const unsigned int CTRL_WIN1MOD = 1u << 13;
    static const unsigned int CTRL_REV037  = 1u << 14;
    static const unsigned int CTRL_WIN1OFF = 1u << 15;

    // Слот ПЗУ: номер R00..R63, файл и сколько слотов он занимает
    struct Slot { unsigned int slot; std::string file; unsigned int pages; };

private:
    Memory * m_ram = nullptr;
    uint8_t * m_buf = nullptr;          // память AZ, прямо в буфере azbk-ram
    unsigned int m_ram_size = 0;
    MemoryMapper * m_mapper = nullptr;

    Interface i_bk_sys;                 // разряды 0-4 системного регистра БК

    bool m_vp037 = false;               // доработка с управлением 037
    unsigned int m_fpga_version = 0;

    // Регистры маппера
    unsigned int m_win[16]{};           // 177300-177336, 13 бит
    unsigned int m_active = 0;          // 177340
    unsigned int m_ro = 0;              // 177342
    unsigned int m_shadow = 0;          // 177344
    unsigned int m_ctrl = 0;            // 177346
    unsigned int m_copy130 = 0;         // 177350
    unsigned int m_copy716 = 0;         // 177352 (reg_az_716)
    bool m_start = false;               // ответ 170000 на стартовое чтение 177716

    // Эмуляция СМК (177130): флаг после записи 6 и «предрегистры» окон
    // 8-15, которые режимы, не называющие окно, оставляют как были
    bool m_smk_flag = false;
    unsigned int m_pre_smk_win[8]{};
    unsigned int m_pre_smk_active = 0;  // разряды 0-7 - окна 8-15
    unsigned int m_pre_smk_ro = 0;

    // То же от 177716 для окна 100000-137777: его берет и запись в 177130,
    // когда отдает окно обратно БК11М
    unsigned int m_pre_b11m_win[4]{};
    bool m_pre_b11m_active = false;
    bool m_pre_b11m_shadow = false;

    unsigned int m_last716 = 0;         // последнее слово в 177716: к нему пишутся байты

    // Счетчик записей в каждую страницу: экран перерисовывает строку, только
    // если ее страницы менялись
    std::vector<uint32_t> m_page_gen;
    unsigned int m_halts = 0;           // низкий HALT: включение, сброс, команда 037

    // Слоты ПЗУ и логотип: файлы и что из них вышло
    std::vector<Slot> m_slots;
    std::string m_logo;

    // Классы окон, по которым route() отвечает; при их смене диспетчер
    // забывает разобранные страницы. BIOS переписывает окна на каждом TRAP, а
    // класс при этом обычно тот же
    uint32_t m_route_key[16]{};

    bool window_ro(unsigned int n) const;
    bool bk_memory_off(unsigned int n) const;     // доработка 037 отключила память БК
    bool bk_ram_under(unsigned int n) const;      // под окном ОЗУ БК, и оно не отключено
    uint32_t route_key(unsigned int n) const;
    void routes_changed();

    void write_716(unsigned int value);
    void write_130(unsigned int value);
    void apply_smk_high();
    void halt1();

    unsigned int read_register(unsigned int address, bool direct);
    void write_register(unsigned int address, unsigned int value);
    void load_slots();

public:
    AZBK(InterfaceManager *im, EmulatorConfigDevice *cd);
    emulator::Result load_config(SystemData *sd) override;
    void reset(bool cold) override;

    // Команда 037 дискового контроллера: сброс AZBK и БК - то же, что после
    // низкого HALT (слоты ПЗУ микроконтроллер грузит заново)
    void restart();

    unsigned int route(unsigned int address, unsigned int mode) override;
    unsigned int get_value(unsigned int address) override;
    unsigned int get_direct(unsigned int address) override;
    unsigned int get_value_word(unsigned int address) override;
    void set_value(unsigned int address, unsigned int value, bool force=false) override;
    void set_value_word(unsigned int address, unsigned int value, bool force=false) override;
    bool debug_writable(unsigned int address) const override;

    // Для экрана, блиттера, звука и диска: память AZ и регистры платы
    uint8_t * memory() const { return m_buf; }
    unsigned int memory_size() const { return m_ram_size; }
    uint32_t page_generation(unsigned int page) const { return page < PAGES ? m_page_gen[page] : 0; }
    // Запись в память AZ мимо процессора (блиттер, диск)
    void touch_page(unsigned int page) { if (page < PAGES) m_page_gen[page]++; }
    void touch_all() { for (uint32_t &g : m_page_gen) g++; }
    unsigned int control() const { return m_ctrl; }
    unsigned int halts() const { return m_halts; }
    unsigned int window_page(unsigned int n) const { return m_win[n & 15]; }

    // Для отладочного окна: регистры маппера и что стоит в памяти AZ. Ответ
    // окна на чтение и запись окно берет у route(), как диспетчер
    Memory * board_ram() const { return m_ram; }
    unsigned int active() const { return m_active; }
    unsigned int readonly() const { return m_ro; }
    unsigned int shadow() const { return m_shadow; }
    unsigned int copy130() const { return m_copy130; }
    unsigned int copy716() const { return m_copy716; }
    bool starting() const { return m_start; }
    bool smk_flag() const { return m_smk_flag; }
    bool window_readonly(unsigned int n) const { return window_ro(n & 15); }
    const std::vector<Slot> & rom_slots() const { return m_slots; }
    const std::string & logo_file() const { return m_logo; }

    void save_state(StateWriter &w) override;
    emulator::Result load_state(const StateReader &r) override;

    std::vector<DeviceFieldInfo> get_device_fields() override;
    bool get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out) override;
};

ComputerDevice * create_azbk(InterfaceManager *im, EmulatorConfigDevice *cd);
