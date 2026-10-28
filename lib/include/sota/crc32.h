#ifndef SOTA_CRC32_H
#define SOTA_CRC32_H

#include <stddef.h>
#include <stdint.h>

/* CRC-32/ISO-HDLC (zlib, Ethernet). Pass 0 as crc to start a new checksum. */
uint32_t sota_crc32(uint32_t crc, const void *data, size_t len);

#endif /* SOTA_CRC32_H */
