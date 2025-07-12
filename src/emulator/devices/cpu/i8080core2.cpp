// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2023-2025 Mikhail Revzin <p3.141592653589793238462643@gmail.com>
// Part of the eCat3 project: https://github.com/Ptr314/ecat3
// Description: Intel 8080 (КР580ВМ80) CPU core for an external library test adaptation

#include "i8080core2.h"

i8080_word_t mem_read(const struct i8080* context, i8080_addr_t addr)
{
    return static_cast<i8080core*>(context->udata)->read_mem(addr);
}

void mem_write(const struct i8080* context, i8080_addr_t addr, i8080_word_t word)
{
    static_cast<i8080core*>(context->udata)->write_mem(addr, word);
}

i8080_word_t io_read(const struct i8080* context, i8080_word_t port)
{
    return static_cast<i8080core*>(context->udata)->read_port(port);
}

void io_write(const struct i8080* context, i8080_word_t port, i8080_word_t word)
{
    static_cast<i8080core*>(context->udata)->write_port(port, word);
}
