// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Контроллер НМД Электроники МС 0585 (плата 3.858.232, ТО5)

#include "e85_knmd.h"
#include "emulator/utils.h"

E85Knmd::E85Knmd(InterfaceManager *im, EmulatorConfigDevice *cd):
      DWController(im, cd, 0200)
    , i_irqa(this, im, 1, "irqa", MODE_W)
    , i_irqb(this, im, 1, "irqb", MODE_W)
{
    // Образ может быть короче накопителя: чистый диск - файл хотя бы в один сектор
    // и заданное число цилиндров
    m_grows_image = true;
}

// От 1 до 8 головок: РАП - 3 разряда; секторов - сколько вмещает РАССЗ
bool E85Knmd::geometry_ok() const
{
    return m_heads >= 1 && m_heads <= 8 && m_sectors >= 1 && m_sectors <= 32;
}

// Накопители, которые узнаются по размеру образа: таблица Xhomer (накопители DEC Pro, по
// 16 и 17 секторов; программы 17-й сектор не трогают), два RD52 других изготовителей из
// таблицы DEVCHR драйвера DW (RT-11 V5.4A-V5.7) и 32 МБ на 8 x 511, куда тоже ставится
// ПРОС. Чистый винчестер - файл другого размера, он дорастает до геометрии из параметров
bool E85Knmd::known_geometry(uint64_t size, unsigned int &heads, unsigned int &cylinders, unsigned int &sectors) const
{
    static const struct { unsigned int heads, cylinders; } drives[] = {
        {4, 153},   // RD50, 5 МБ
        {4, 306},   // RD51 и МС5401, 10 МБ
        {4, 615},   // RD31, 21 МБ
        {5, 697},   // RD52, тип 52C у DW
        {8, 511},   // 32 МБ у ПРОС
        {8, 512},   // RD52, 36 МБ (52Q у DW)
        {7, 645},   // RD52, тип 52A у DW
        {6, 820},   // RD32, 43 МБ
        {8, 1024},  // RD53, 71 МБ
    };
    for (unsigned int s = 16; s <= 17; s++)
        for (unsigned int i = 0; i < sizeof(drives) / sizeof(drives[0]); i++)
            if (size == (uint64_t)drives[i].heads * drives[i].cylinders * s * HddImage::SECTOR_SIZE) {
                heads = drives[i].heads;
                cylinders = drives[i].cylinders;
                sectors = s;
                return true;
            }
    return false;
}

emulator::Result E85Knmd::load_config(SystemData *sd)
{
    emulator::Result res = DWController::load_config(sd);
    if (!res) return res;
    return load_image();
}

//--------------------------- Шина ------------------------------------------//

// Регистр выбирается разрядами 1-3 адреса, РСНУ - разрядом 4 (п. 4.6.1)
unsigned int E85Knmd::reg_of(unsigned int address) const
{
    return (address & 020) ? (unsigned int)DW_STATUS : ((address & 016) >> 1);
}

// ЗОА и ЗОБ - линии А и Б позиции, уровнем
void E85Knmd::request_changed(unsigned int request, bool up)
{
    Interface &line = (request == REQ_A) ? i_irqa : i_irqb;
    const unsigned int v = up ? 1 : 0;
    if (line.value != v) line.change(v);
}

// Начальная установка снимает оба запроса. Линии - уровнем, а до первой установки на
// них не 0: запрос, поднятый после, иначе не дал бы фронта
void E85Knmd::bus_init()
{
    if (i_irqa.value != 0) i_irqa.change(0);
    if (i_irqb.value != 0) i_irqb.change(0);
}

ComputerDevice * create_e85_knmd(InterfaceManager *im, EmulatorConfigDevice *cd)
{
    return new E85Knmd(im, cd);
}
