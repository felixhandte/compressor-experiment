#ifndef VARINT_H
#define VARINT_H

#include "compressor.h"

/**
 * Varints are a variable length encoding of a uint64_t, intended to be
 * compatible with the protobuf varint implementation. That is, they are a
 * little-endian encoding where the low 7 bits of each byte encode the next 7
 * bits of the value and the high bit indicates whether to continue to the
 * next byte.
 */

/**
 * Encode a uint64_t into the beginning of the provided buffer. Advance the
 * buffer pointer to the first byte past the encoded value. Returns whether
 * successful.
 */
int varint_encode(byte_t** buf, size_t size, uint64_t val);

/**
 * Decode a uint64_t from the beginning of the provided buffer. Advance the
 * buffer pointer to the first byte past the encoded value. Returns whether
 * successful.
 */
int varint_decode(const byte_t** buf, size_t size, uint64_t* val);

#endif
