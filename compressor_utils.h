#ifndef COMPRESSOR_UTILS_H
#define COMPRESSOR_UTILS_H

#include <stdio.h>

#include "compressor.h"

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#define CHECKR(EXPR, MSG, RET) \
  if (unlikely(!(EXPR))) { \
    fprintf(stderr, "Error: %s\n", MSG); \
    return RET; \
  }

#define CHECK(EXPR, MSG) CHECKR(EXPR, MSG, 0)

#define CHECK1(EXPR, MSG) CHECKR(EXPR, MSG, 1)

#define MAX(A, B) ({ \
  __typeof__(A) _a = (A); \
  __typeof__(B) _b = (B); \
  _a > _b ? _a : _b; \
})

#define MIN(A, B) ({ \
  __typeof__(A) _a = (A); \
  __typeof__(B) _b = (B); \
  _a < _b ? _a : _b; \
})

/**
 *
 */
typedef struct {
  uint64_t literal_length;
  const byte_t* literals;
  uint64_t match_offset;
  uint64_t match_length;
} litandmatch_t;

size_t encode_literals_and_matches(
    byte_t* dst, size_t dstsize,
    const litandmatch_t* lams, size_t numlams);

size_t decode_literals_and_matches(
    const byte_t* src, size_t srcsize,
    litandmatch_t* lams, size_t numlams);

void print_literal_and_match(FILE* f, const litandmatch_t* lam);

void print_match_with_context(
    FILE* f,
    const byte_t* srcbuf, const byte_t* srcend,
    const byte_t* srcpos, const byte_t* matchpos,
    size_t matchlen);

size_t noop_compress(
    byte_t* dst, size_t dstsize,
    const byte_t* src, size_t srcsize);

#endif
