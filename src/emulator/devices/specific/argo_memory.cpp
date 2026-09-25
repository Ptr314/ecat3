// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Арго ФВ-6511: диспетчер памяти и пересылки ПДП

#include <cstring>

#include "emulator/utils.h"
#include "emulator/devices/common/i8257.h"
#include "argo_memory.h"

// Столько тактов ВТ57 тратит на байт: два цикла шины на пересылку, по четыре
// такта каждый - как у Юниора, машины того же семейства. Процессор на это время
// снят с шины, и эмулятору важно не столько точное число, сколько то, что время
// идет: иначе пересылка 16 Кбайт обходилась бы машине в ноль
#define ARGO_DMA_CYCLES_PER_BYTE 8

ArgoMemory::ArgoMemory(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd)
    , i_cpu(this, im, 8, "cpu", MODE_R)
    , i_dma0(this, im, 8, "dma0", MODE_R)
    , i_dma1(this, im, 8, "dma1", MODE_R)
{
    m_clocked = true;               //clock() is overridden here
    addresable_size = 0x10000;
    memset(&m_log, 0, sizeof(m_log));
}

// Список разрядов через запятую, младший первым. Разряды - не числа машины,
// поэтому читаются десятичными независимо от radix
emulator::Result ArgoMemory::read_bits(EmulatorConfigDevice *cd, const std::string &name,
                                       unsigned int *bits, unsigned int count)
{
    const std::string s = read_confg_value(cd, name, false, std::string(""));
    if (s.empty()) return emulator::Result::ok();

    const std::vector<std::string> parts = split_string(s, ',', true);
    if (parts.size() != count)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{ArgoMemory|" + std::string(QT_TRANSLATE_NOOP("ArgoMemory", "Wrong number of bits in")) + "} " + name);

    for (unsigned int i = 0; i < count; i++)
        bits[i] = parse_numeric_value(str_trim(parts[i]), 10) & 7;

    return emulator::Result::ok();
}

emulator::Result ArgoMemory::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    Memory = dynamic_cast<AddressableDevice*>(im->dm->get_device_by_name(cd->get_parameter("memory").value));
    Map    = dynamic_cast<AddressableDevice*>(im->dm->get_device_by_name(cd->get_parameter("map").value));
    DMA    = dynamic_cast<I8257*>(im->dm->get_device_by_name(cd->get_parameter("dma").value));

    if (Memory == nullptr || Map == nullptr || DMA == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{ArgoMemory|" + std::string(QT_TRANSLATE_NOOP("ArgoMemory", "Memory, map and DMA devices are expected")) + "} " + name);

    res = read_bits(cd, "cfg_bits", m_cfg_bits, 3);
    if (!res) return res;
    res = read_bits(cd, "page_bits", m_page_bits, 2);
    if (!res) return res;

    m_bank_bit = read_confg_value(cd, "bank_bit", false, (unsigned int)7) & 7;
    m_bank_invert = read_confg_value(cd, "bank_invert", false, true);

    m_block_mask = read_confg_value(cd, "block_mask", false, (unsigned int)0x0E);
    if (m_block_mask == 0) m_block_mask = 0x0E;
    m_block_shift = 0;
    while (((m_block_mask >> m_block_shift) & 1) == 0) m_block_shift++;

    m_blocks = read_confg_value(cd, "blocks", false, (unsigned int)8);
    if (m_blocks < 1) m_blocks = 1;
    m_ext_base = read_confg_value(cd, "ext_base", false, (unsigned int)0x20000);

    return emulator::Result::ok();
}

void ArgoMemory::reset(MAYBE_UNUSED bool cold)
{
    m_transfers = 0;
    m_bytes = 0;
    m_last_count = 0;
    m_log_count = 0;
    m_cpu_valid = false;
}

unsigned int ArgoMemory::config_index(unsigned int value) const
{
    unsigned int idx = 0;
    for (unsigned int i = 0; i < 3; i++)
        if ((value >> m_cfg_bits[i]) & 1) idx |= (1u << i);
    return idx;
}

// Ячейку, в которую никто не писал, надо читать нулевым блоком: до первой
// записи линия несет единицы, и маскировать их номером блока нельзя
unsigned int ArgoMemory::block_of(const Interface &i) const
{
    if (i.linked == 0) return 0;
    const unsigned int b = (i.value & m_block_mask) >> m_block_shift;
    return (b < m_blocks)?b:0;
}

// Страничное отображение: два старших разряда адреса и три разряда значения
// адресуют прошивку, в ответе - номер страницы и банки
unsigned int ArgoMemory::translate(unsigned int address, unsigned int value) const
{
    const unsigned int idx = ((address >> 14) & 0x03) | (config_index(value) << 2);
    const unsigned int v = Map->get_direct(idx);

    unsigned int page = 0;
    for (unsigned int i = 0; i < 2; i++)
        if ((v >> m_page_bits[i]) & 1) page |= (1u << i);

    unsigned int bank = (v >> m_bank_bit) & 1;
    if (m_bank_invert) bank ^= 1;

    return (bank << 16) | (page << 14) | (address & 0x3FFF);
}

// То же для процессора, но по заранее посчитанным страницам: это делается на
// каждое обращение к памяти
unsigned int ArgoMemory::translate_cpu(unsigned int address) const
{
    if (!m_cpu_valid || m_cpu_value != i_cpu.value)
    {
        m_cpu_value = i_cpu.value;
        for (unsigned int p = 0; p < 4; p++)
            m_cpu_pages[p] = translate(p << 14, m_cpu_value) & ~0x3FFFu;
        m_cpu_valid = true;
    }
    return m_cpu_pages[(address >> 14) & 3] | (address & 0x3FFF);
}

// Отображение канала ПДП: номер блока берет банку расширения целиком. Нулевой
// номер - основная память, и это те же разряды значения, что дают нулевую
// строку прошивки, так что канал получает ровно то отображение, что и $61
unsigned int ArgoMemory::translate_dma(unsigned int address, unsigned int block) const
{
    if (block == 0) return translate(address, 0);
    return m_ext_base + ((block - 1) << 16) + (address & 0xFFFF);
}

unsigned int ArgoMemory::get_value(unsigned int address)
{
    return Memory->get_value(translate_cpu(address));
}

// Отладочные чтения идут мимо кеша страниц, и это не мелочь: get_direct зовут
// окно дампа, LOG mem.value и отладчик - то есть поток GUI, - а кеш
// перестраивает поток эмуляции на каждой записи в регистр конфигурации (TCP/M
// делает это на каждую пересылку квазидиска). Два потока, считающие в одни и
// те же mutable поля, разъезжаются так: один прочитал старое значение, второй
// пересчитал все четыре страницы и пометил кеш свежим, первый дописал в него
// страницу по старому отображению. Процессор после этого молча ходит в чужую
// банку до следующей смены конфигурации
unsigned int ArgoMemory::get_direct(unsigned int address)
{
    return Memory->get_direct(translate(address, i_cpu.value));
}

// Разряд 0 действующей ячейки - защита первых 16 Кбайт от записи, и включает
// ее НОЛЬ. Полярность видна по самим значениям, которые пишут программы:
// монитор и TCP/M ставят $61, а система обязана писать в TPA с $0100 - значит
// единица разрешает; ZX.COM последним действием пишет $D8, и по $0000-$3FFF в
// этот момент лежит копия ПЗУ Спектрума, которой полагается быть ПЗУ. Для того
// разряд и нужен: без него первая же программа, записавшая что-нибудь по
// младшим адресам - а на Спектруме так делают, - убила бы образ ПЗУ вместе с
// собственной машиной.
//
// Проверяется адрес до отображения: на схеме это разряды A14 и A15, то есть
// первая страница адресного пространства, куда бы прошивка ее ни отправила.
//
// Пересылки ПДП здесь не проверяются. Гасит ли этот разряд запись и им, со
// схемы не выяснить, а увидеть разницу не на чем: во всех значениях, которые
// программы кладут в ячейки каналов ($61, $63, $CB), он стоит.
void ArgoMemory::set_value(unsigned int address, unsigned int value, bool force)
{
    //force - это внутренняя запись мимо шины (COMMAND mem.set, редактор дампа,
    //загрузка образа), и защита шины ее не касается: иначе правка ПЗУ Спектрума
    //из скрипта молча пропадала бы, не сказав почему
    if (!force && (address & 0xC000) == 0 && (i_cpu.value & 1) == 0) return;
    Memory->set_value(translate_cpu(address), value);
}

// Пересылка парой каналов: у каждого своя ячейка регистрового файла, то есть
// своя память. Что канал 0 читает, а канал 1 пишет, задано словами счета
// ($8000 у нулевого, $4000 у первого, $FA52-$FA62), но МОНИТОР других сочетаний
// не строит, и направление здесь взято таким же
void ArgoMemory::run_transfer()
{
    const unsigned int addr0 = DMA->RgA[0] | (DMA->RgA[1] << 8);
    const unsigned int addr1 = DMA->RgA[2] | (DMA->RgA[3] << 8);
    //Длина - по более короткому из двух каналов: конечный счет останавливает
    //оба (TC STOP, разряд 6 режима), и первый досчитавший обрывает пересылку.
    //МОНИТОР строит одинаковые, так что сегодня это ничего не меняет
    const unsigned int c0 = (DMA->RgC[0] | (DMA->RgC[1] << 8)) & 0x3FFF;
    const unsigned int c1 = (DMA->RgC[2] | (DMA->RgC[3] << 8)) & 0x3FFF;
    const unsigned int count = ((c0 < c1)?c0:c1) + 1;

    const unsigned int blk0 = block_of(i_dma0);
    const unsigned int blk1 = block_of(i_dma1);

    for (unsigned int i = 0; i < count; i++)
    {
        const unsigned int a0 = translate_dma((addr0 + i) & 0xFFFF, blk0);
        const unsigned int a1 = translate_dma((addr1 + i) & 0xFFFF, blk1);
        Memory->set_value(a1, Memory->get_value(a0));
    }

    // Адреса каналов остаются на конце пересылки, счетчики обнулены, сами
    // каналы выключены по конечному счету: разряд 6 регистра режима (TC STOP),
    // который МОНИТОР ставит словом $E3
    for (unsigned int ch = 0; ch < 2; ch++)
    {
        const unsigned int a = ((ch == 0)?addr0:addr1) + count;
        DMA->RgA[ch*2]     = (uint8_t)(a & 0xFF);
        DMA->RgA[ch*2 + 1] = (uint8_t)((a >> 8) & 0xFF);
        //Счетчик на конечном счете переходит через ноль в $3FFF - так его
        //кончает dma_next() у самого ВТ57, и одна микросхема должна кончать
        //счет одинаково, чьей бы рукой пересылка ни была сделана
        DMA->RgC[ch*2]     = 0xFF;
        DMA->RgC[ch*2 + 1] = (uint8_t)((DMA->RgC[ch*2 + 1] & 0xC0) | 0x3F);
    }
    DMA->RgMode &= ~0x03;
    DMA->RgState |= 0x03;

    //Журнал кольцевой: разбирают в нем ту пересылку, которая только что была,
    //а не первые шестнадцать со сброса - после загрузки TCP/M с квазидиска их
    //набирается на порядок больше
    {
        LogEntry &e = m_log[m_log_count % ARGO_LOG_SIZE];
        m_log_count++;
        e.blk0 = blk0; e.blk1 = blk1;
        e.addr0 = addr0; e.addr1 = addr1; e.count = count;
        e.phys0 = translate_dma(addr0, blk0); e.phys1 = translate_dma(addr1, blk1);
    }

    m_last_count = count;
    m_transfers++;
    m_bytes += count;

    if (cpu != nullptr) cpu->hold(count * ARGO_DMA_CYCLES_PER_BYTE);
}

void ArgoMemory::clock(MAYBE_UNUSED unsigned int counter)
{
    // Включение каналов 0 и 1 вместе - это и есть запуск пересылки: иначе они
    // на этой машине не используются, экран выдает канал 2
    if (DMA != nullptr && DMA->channel_enabled(0) && DMA->channel_enabled(1))
        run_transfer();
}

std::vector<DeviceFieldInfo> ArgoMemory::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"config",    "Configuration registers: CPU, DMA0, DMA1",     false});
    r.push_back({"pages",     "Physical pages the CPU sees right now",        false});
    r.push_back({"blocks",    "Blocks the two DMA channels address",          false});
    r.push_back({"last",      "Length of the last transfer",                  false});
    r.push_back({"log",       "Last transfers: blocks, from, to, count, physical from and to", false});
    r.push_back({"transfers", "Transfers since reset",                        false});
    r.push_back({"bytes",     "Bytes moved since reset",                      false});
    return r;
}

bool ArgoMemory::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "config")
    {
        out.numeric = true; out.width = 8;
        out.values.push_back(i_cpu.value & 0xFF);
        out.values.push_back(i_dma0.value & 0xFF);
        out.values.push_back(i_dma1.value & 0xFF);
        return true;
    }
    if (field == "pages")
    {
        out.numeric = true; out.width = 8;
        for (unsigned int p = 0; p < 4; p++)
            out.values.push_back(translate(p << 14, i_cpu.value) >> 14);
        return true;
    }
    if (field == "blocks")
    {
        out.numeric = true; out.width = 8;
        out.values.push_back(block_of(i_dma0));
        out.values.push_back(block_of(i_dma1));
        return true;
    }
    if (field == "log")
    {
        out.numeric = true; out.width = 32;
        //Кольцо отдается по порядку, от самой старой из сохранившихся
        const unsigned int n = (m_log_count < ARGO_LOG_SIZE)?m_log_count:ARGO_LOG_SIZE;
        const unsigned int first = m_log_count - n;
        for (unsigned int i = 0; i < n; i++)
        {
            const LogEntry &e = m_log[(first + i) % ARGO_LOG_SIZE];
            out.values.push_back(e.blk0);  out.values.push_back(e.blk1);
            out.values.push_back(e.addr0); out.values.push_back(e.addr1);
            out.values.push_back(e.count);
            out.values.push_back(e.phys0); out.values.push_back(e.phys1);
        }
        return true;
    }
    if (field == "last")      { out.numeric = true; out.width = 16; out.values.push_back(m_last_count); return true; }
    if (field == "transfers") { out.numeric = true; out.width = 32; out.values.push_back((unsigned int)m_transfers); return true; }
    if (field == "bytes")     { out.numeric = true; out.width = 32; out.values.push_back((unsigned int)m_bytes); return true; }

    return AddressableDevice::get_field(field, from, to, out);
}

// Посчитанные страницы выведены из линии, а линии восстанавливаются без
// оповещения - значит, считать их заново
void ArgoMemory::state_restored()
{
    m_cpu_valid = false;
}

void ArgoMemory::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.n("last_count", m_last_count);
    w.n64("transfers", m_transfers);
    w.n64("bytes", m_bytes);
}

emulator::Result ArgoMemory::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.u("last_count", m_last_count);
    r.n64("transfers", m_transfers);
    r.n64("bytes", m_bytes);
    return emulator::Result::ok();
}

ComputerDevice * create_argo_memory(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new ArgoMemory(im, cd);
}
