/* Tiny formatting helpers, so neither image needs printf. */
#include "board.h"

void console_write(const char *s)
{
    while (*s) {
        console_putc(*s++);
    }
}

void console_dec(uint32_t v)
{
    char buf[11];
    int i = 0;

    do {
        buf[i++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (i--) {
        console_putc(buf[i]);
    }
}

void console_hex(uint32_t v)
{
    static const char digits[] = "0123456789abcdef";

    console_write("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        console_putc(digits[(v >> shift) & 0xFu]);
    }
}

void console_version(uint32_t packed)
{
    console_dec(packed >> 24);
    console_putc('.');
    console_dec((packed >> 16) & 0xFFu);
    console_putc('.');
    console_dec(packed & 0xFFFFu);
}
