// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Юниор ФВ-6506: дополнительная память и пересылки в нее через ПДП

#include "emulator/utils.h"
#include "emulator/devices/common/i8257.h"
#include "unior_memory.h"

// Столько тактов ВТ57 тратит на байт: два цикла шины на пересылку, по четыре
// такта каждый. Процессор на это время снимается с шины, и эмулятору важно не
// столько точное число, сколько то, что время идет: без этого пересылка
// половины памяти обходилась бы машине в ноль
#define UNIOR_DMA_CYCLES_PER_BYTE 8

UniorMemory::UniorMemory(InterfaceManager *im, EmulatorConfigDevice *cd):
    AddressableDevice(im, cd)
    , i_block(this, im, 8, "block", MODE_R)
    , i_block2(this, im, 8, "block2", MODE_R)
{
    m_clocked = true;   //clock() is overridden here
    addresable_size = 1;
    can_read = false;
    can_write = true;
}

emulator::Result UniorMemory::load_config(SystemData *sd)
{
    emulator::Result res = AddressableDevice::load_config(sd);
    if (!res) return res;

    Main = dynamic_cast<AddressableDevice*>(im->dm->get_device_by_name(cd->get_parameter("memory").value));
    Ext  = dynamic_cast<AddressableDevice*>(im->dm->get_device_by_name(cd->get_parameter("extension").value));
    DMA  = dynamic_cast<I8257*>(im->dm->get_device_by_name(cd->get_parameter("dma").value));

    if (Main == nullptr || Ext == nullptr || DMA == nullptr)
        return emulator::Result::error(emulator::ErrorCode::ConfigError,
            "{UniorMemory|" + std::string(QT_TRANSLATE_NOOP("UniorMemory", "Memory, extension and DMA devices are expected")) + "} " + name);

    m_blocks = read_confg_value(cd, "blocks", false, (unsigned int)1);
    if (m_blocks < 1) m_blocks = 1;
    if (m_blocks > 7) m_blocks = 7;

    m_block_mask = read_confg_value(cd, "block_mask", false, (unsigned int)0x07);
    if (m_block_mask == 0) m_block_mask = 0x07;
    m_block_shift = 0;
    while (((m_block_mask >> m_block_shift) & 1) == 0) m_block_shift++;
    m_block_invert = cd->get_parameter("block_invert", false).value != "0";

    return emulator::Result::ok();
}

void UniorMemory::reset(MAYBE_UNUSED bool cold)
{
    m_direction = 0;
    m_last_block = 0;
    m_last_count = 0;
}

// Разряды 0-2 порта C держат номер блока инверсно: МОНИТОР снимает выбор
// записью единиц (ORI $07 после пересылки), а выбирает блок, сбрасывая в ноль
// разряды его номера (AND с дополнением). Ноль в ответе - блок не выбран, и
// тогда обе стороны пересылки попадают в основное ОЗУ
unsigned int UniorMemory::block_of(const Interface &i) const
{
    if (i.linked == 0) return 0;
    const unsigned int v = (m_block_invert?(~i.value):i.value) & m_block_mask;
    return v >> m_block_shift;
}

unsigned int UniorMemory::selected_block() const
{
    if (i_block2.linked == 0) return block_of(i_block);
    const unsigned int b1 = block_of(i_block2);
    return (b1 != 0)?b1:block_of(i_block);
}

// Сторона, на которой стоит блок. У Юниора ее задает триггер порта $50, у Арго
// - то, в какой из двух регистров выбора машина положила номер: МОНИТОР пишет
// в один из них чистое $61, а в другой $61 с номером блока в разрядах 1-3
unsigned int UniorMemory::direction() const
{
    if (i_block2.linked == 0) return m_direction;
    return (block_of(i_block2) != 0)?1:0;
}

unsigned int UniorMemory::ext_read(unsigned int block, unsigned int address)
{
    if (block == 0 || block > m_blocks) return 0xFF;
    return Ext->get_value((block - 1) * 0x10000 + (address & 0xFFFF));
}

void UniorMemory::ext_write(unsigned int block, unsigned int address, unsigned int value)
{
    if (block == 0 || block > m_blocks) return;
    Ext->set_value((block - 1) * 0x10000 + (address & 0xFFFF), value);
}

// Пересылка парой каналов. Канал 0 всегда адресует основную память, канал 1 -
// блок расширения, а триггер $50 задает сторону: 0 - блок отдает (чтение в
// основную память), 1 - блок принимает (запись).
//
// Что именно так, видно по самому ПЗУ: две точки входа МОНИТОРа ($F818 и
// $F81B) снимают со стека адреса в обратном друг другу порядке (FA5D против
// FA90), то есть при одном и том же соглашении вызова кладут основной адрес в
// канал 0, а блочный - в канал 1. Сходится со всеми тремя местами, которые
// машина проходит сама: загрузка 16 Кбайт из блока 7 по адресу 0040 с пуском
// ($50 = 0), прокрутка экрана той же парой каналов вообще без блока ($50 = 1)
// и обращение к сектору квазидиска драйвером TCP/M, где направление выбирается
// точкой входа, а адреса меняются местами (XCHG перед вызовом).
void UniorMemory::run_transfer()
{
    const unsigned int addr_m = DMA->RgA[0] | (DMA->RgA[1] << 8);
    const unsigned int addr_b = DMA->RgA[2] | (DMA->RgA[3] << 8);
    const unsigned int count_reg = DMA->RgC[0] | (DMA->RgC[1] << 8);
    const unsigned int count = (count_reg & 0x3FFF) + 1;
    const unsigned int block = selected_block();
    const bool to_block = (direction() != 0);

    for (unsigned int i = 0; i < count; i++)
    {
        const unsigned int a_m = (addr_m + i) & 0xFFFF;
        const unsigned int a_b = (addr_b + i) & 0xFFFF;
        if (to_block)
        {
            const unsigned int v = Main->get_value(a_m);
            if (block == 0) Main->set_value(a_b, v); else ext_write(block, a_b, v);
        } else {
            const unsigned int v = (block == 0)?Main->get_value(a_b):ext_read(block, a_b);
            Main->set_value(a_m, v);
        }
    }

    // Адреса каналов остаются на конце пересылки, счетчики обнулены, сами
    // каналы выключены по конечному счету: бит 6 регистра режима (TC STOP),
    // который МОНИТОР ставит словом $E3
    for (unsigned int ch = 0; ch < 2; ch++)
    {
        const unsigned int a = ((ch == 0)?addr_m:addr_b) + count;
        DMA->RgA[ch*2]     = (uint8_t)(a & 0xFF);
        DMA->RgA[ch*2 + 1] = (uint8_t)((a >> 8) & 0xFF);
        DMA->RgC[ch*2]     = 0;
        DMA->RgC[ch*2 + 1] = (uint8_t)(DMA->RgC[ch*2 + 1] & 0xC0);
    }
    DMA->RgMode &= ~0x03;
    DMA->RgState |= 0x03;

    m_last_block = block;
    m_last_count = count;
    m_transfers++;
    m_bytes += count;

    if (cpu != nullptr) cpu->hold(count * UNIOR_DMA_CYCLES_PER_BYTE);
}

void UniorMemory::clock(MAYBE_UNUSED unsigned int counter)
{
    // Включение каналов 0 и 1 вместе - это и есть запуск пересылки: иначе они
    // на этой машине не используются, экран выдает канал 2
    if (DMA != nullptr && DMA->channel_enabled(0) && DMA->channel_enabled(1))
        run_transfer();
}

unsigned int UniorMemory::get_value(MAYBE_UNUSED unsigned int address)
{
    // Триггер только на запись: чтения порта $50 нет ни в ПЗУ, ни в программах
    return 0xFF;
}

void UniorMemory::set_value(MAYBE_UNUSED unsigned int address, unsigned int value, MAYBE_UNUSED bool force)
{
    m_direction = value & 0x01;
}

std::vector<DeviceFieldInfo> UniorMemory::get_device_fields()
{
    std::vector<DeviceFieldInfo> r = AddressableDevice::get_device_fields();
    r.push_back({"direction", "Triggers of the transfer direction, port $50",  false});
    r.push_back({"block",     "Extension block selected on port C right now",  false});
    r.push_back({"last",      "Block and length of the last transfer",         false});
    r.push_back({"transfers", "Transfers since reset",                         false});
    r.push_back({"bytes",     "Bytes moved since reset",                       false});
    return r;
}

bool UniorMemory::get_field(const std::string &field, unsigned int from, unsigned int to, DeviceFieldValue &out)
{
    if (field == "direction") { out.numeric = true; out.width = 1;  out.values.push_back(m_direction); return true; }
    if (field == "block")     { out.numeric = true; out.width = 8;  out.values.push_back(selected_block()); return true; }
    if (field == "last")
    {
        out.numeric = true; out.width = 16;
        out.values.push_back(m_last_block);
        out.values.push_back(m_last_count);
        return true;
    }
    if (field == "transfers") { out.numeric = true; out.width = 32; out.values.push_back((unsigned int)m_transfers); return true; }
    if (field == "bytes")     { out.numeric = true; out.width = 32; out.values.push_back((unsigned int)m_bytes); return true; }

    return AddressableDevice::get_field(field, from, to, out);
}

void UniorMemory::save_state(StateWriter &w)
{
    AddressableDevice::save_state(w);
    w.n("direction", m_direction);
    w.n("last_block", m_last_block);
    w.n("last_count", m_last_count);
    w.n64("transfers", m_transfers);
    w.n64("bytes", m_bytes);
}

emulator::Result UniorMemory::load_state(const StateReader &r)
{
    emulator::Result res = AddressableDevice::load_state(r);
    if (!res) return res;
    r.u("direction", m_direction);
    r.u("last_block", m_last_block);
    r.u("last_count", m_last_count);
    r.n64("transfers", m_transfers);
    r.n64("bytes", m_bytes);
    return emulator::Result::ok();
}

ComputerDevice * create_unior_memory(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new UniorMemory(im, cd);
}
