#include "varint.h"

#include <assert.h>
#include <stddef.h>

const size_t BUF_LEN = 32;

size_t expected_encoded_size(uint64_t val) {
  if (val == 0) {
    return 1;
  }
  return (63 - __builtin_clzll(val)) / 7 + 1;
}

void check_encode_decode(byte_t* buf, size_t size, uint64_t val) {
  ptrdiff_t expected_size = expected_encoded_size(val);

  byte_t *bufp1 = buf;
  assert(varint_encode(&bufp1, size, val));
  assert(bufp1 - buf == expected_size);

  const byte_t *bufp2 = buf;
  uint64_t out;
  assert(varint_decode(&bufp2, size, &out));
  assert(val == out);
  assert(bufp1 == bufp2);
}

void check_encode_decode_for_buf_sizes(byte_t* buf, size_t size, uint64_t val) {
  // check that this val works on the full buffer
  check_encode_decode(buf, size, val);

  size_t expected_size = expected_encoded_size(val);
  // buffers smaller than the expected size
  for (size_t pretend_size = 0; pretend_size < expected_size; pretend_size++) {
    // encoding should fail
    byte_t *bufp1 = buf;
    assert(!varint_encode(&bufp1, pretend_size, val));
    assert(bufp1 == buf);

    // encode with the full buffer
    byte_t *bufp2 = buf;
    assert(varint_encode(&bufp2, size, val));

    // decode should fail
    const byte_t *bufp3 = buf;
    size_t out;
    assert(!varint_decode(&bufp3, pretend_size, &out));
    assert(bufp3 == buf);
  }

  // buffer lengths >= expected size should succeed
  for (size_t pretend_size = expected_size; pretend_size <= expected_size + 4; pretend_size++) {
    check_encode_decode(buf, pretend_size, val);
  }
}

int main() {
  byte_t buf[BUF_LEN];

  // check all small vals
  for (size_t i = 0; i < (1ul << 16); i++) {
    check_encode_decode_for_buf_sizes(buf, BUF_LEN, i);
  }
  // check all very large vals
  for (size_t i = -1l; i > -(1ul << 16); i++) {
    check_encode_decode_for_buf_sizes(buf, BUF_LEN, i);
  }
  // check around powers of two
  for (size_t i = 1; i; i <<= 1) {
    check_encode_decode_for_buf_sizes(buf, BUF_LEN, i - 1);
    check_encode_decode_for_buf_sizes(buf, BUF_LEN, i    );
    check_encode_decode_for_buf_sizes(buf, BUF_LEN, i + 1);
  }

  return 0;
}
