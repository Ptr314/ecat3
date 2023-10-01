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
