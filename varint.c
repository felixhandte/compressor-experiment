#include "varint.h"

int varint_encode(byte_t** buf, size_t size, uint64_t val) {
  byte_t *bufp = *buf;
  byte_t *bufend = *buf + size;

  // keep encoding the low 7 bits until val is 0
  do {
    if (bufp == bufend) {
      // not enough room!
      return 0;
    }
    *(bufp++) = (val & 0x7F) | ((val > 0x7F) << 7);
    val >>= 7;
  } while (val);

  *buf = bufp;
  return 1;
}

int varint_decode(const byte_t** buf, size_t size, uint64_t* val) {
  const byte_t *bufp = *buf;
  const byte_t *bufend = *buf + size;

  if (bufp == bufend) {
    // varints are always at least one byte
    return 0;
  }

  uint64_t tmpval = 0;
  int exponent = 0;
  uint64_t c;
  while ((c = *(bufp++)) & 0x80ul) {
    if (bufp == bufend) {
      // continuation bit is set on last byte in buf???
      return 0;
    }
    tmpval |= (c & 0x7Ful) << exponent;
    exponent += 7;
  }

  // high bit isn't set, no need to mask
  tmpval |= c << exponent;

  // advance caller buf pointer to first byte after varint
  *buf = bufp;
  *val = tmpval;
  return 1;
}
