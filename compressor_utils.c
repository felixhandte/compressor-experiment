#include "compressor_utils.h"

#include <string.h>

#include "varint.h"

size_t encode_literals_and_matches(
    byte_t* dst, size_t dstsize,
    const litandmatch_t* lams, size_t numlams) {
  const litandmatch_t* lamsend = lams + numlams;
  byte_t* dstend = dst + dstsize;
  byte_t* dstp = dst;
  size_t decompressed_size = 0;
  for (const litandmatch_t* lam = lams; lam < lamsend; lam++) {
    decompressed_size += lam->literal_length + lam->match_length;
  }
  CHECK(varint_encode(&dstp, dstend - dstp, decompressed_size), "couldn't encode decompressed size");
  for (const litandmatch_t* lam = lams; lam < lamsend; lam++) {
    uint64_t litlen = lam->literal_length;
    CHECK(varint_encode(&dstp, dstend - dstp, litlen), "couldn't encode litlen");
    memcpy(dstp, lam->literals, litlen);
    dstp += litlen;
    uint64_t matchoff = lam->match_offset;
    uint64_t matchlen = lam->match_length;
    CHECK(varint_encode(&dstp, dstend - dstp, matchoff), "couldn't encode matchoff");
    CHECK(varint_encode(&dstp, dstend - dstp, matchlen), "couldn't encode matchlen");
  }
  return dstp - dst;
}

size_t decode_literals_and_matches(
    const byte_t* src, size_t srcsize,
    litandmatch_t* lams, size_t numlams) {
  const byte_t* srcend = src + srcsize;
  const byte_t* srcp = src;
  litandmatch_t* lamsend = lams + numlams;
  litandmatch_t* lam = lams;
  uint64_t decompressed_size;
  CHECK(varint_decode(&srcp, srcend - srcp, &decompressed_size), "couldn't decode decompressed size");
  for (; srcp < srcend && lam < lamsend; lam++) {
    CHECK(varint_decode(&srcp, srcend - srcp, &(lam->literal_length)), "couldn't decode litlen");
    lam->literals = srcp;
    srcp += lam->literal_length;
    if (srcp >= srcend) {
      // allow eliding the final match
      lam->match_offset = 0;
      lam->match_length = 0;
      break;
    }
    CHECK(varint_decode(&srcp, srcend - srcp, &(lam->match_offset)), "couldn't decode match offset");
    CHECK(varint_decode(&srcp, srcend - srcp, &(lam->match_length)), "couldn't decode match length");
  }
  return lam - lams;
}

void print_literal_and_match(FILE* f, const litandmatch_t* lam) {
  fprintf(f, "Match:\n");
  fprintf(f, "  litlen  : %lu\n", lam->literal_length);
  fprintf(f, "  literals: '");
  for (size_t i = 0; i < lam->literal_length; i++) {
    byte_t c = lam->literals[i];
    if (c >= 0x20 && c < 0x7F) {
      putc(c, f);
    } else {
      putc('.', f);
    }
  }
  fprintf(f, "'\n");
  fprintf(f, "  matchoff: %lu\n", lam->match_offset);
  fprintf(f, "  matchlen: %lu\n", lam->match_length);
}

static inline void safe_print_char(FILE* f, byte_t c) {
  if (c >= 0x20 && c < 0x7F) {
    putc(c, f);
  } else {
    putc('.', f);
  }
}

void print_match_with_context(
    FILE* f,
    const byte_t* srcbuf, const byte_t* srcend,
    const byte_t* srcpos, const byte_t* matchpos,
    size_t matchlen) {
  static const size_t max_ext_ctx = 10;
  static const size_t max_match_ctx = 40;

  const size_t ss = srcend - srcbuf;
  const size_t sidx = srcpos - srcbuf;
  const size_t midx = matchpos - srcbuf;
  const size_t ml = matchlen;

  fprintf(f, "Match:\n  match idx = %ld, cur idx = %ld, match len = %lu\n", midx, sidx, ml);

  size_t m_start_idx = MAX(midx, max_ext_ctx) - max_ext_ctx;
  size_t m_start_end_idx = midx + MIN(matchlen / 2, max_match_ctx);
  size_t m_end_start_idx = midx + matchlen - MIN(matchlen - (matchlen / 2), max_match_ctx);
  size_t m_end_idx = MIN(midx + matchlen + max_ext_ctx, sidx);

  size_t s_start_idx = MAX(MAX(sidx, max_ext_ctx) - max_ext_ctx, m_end_idx);
  size_t s_start_end_idx = sidx + MIN(matchlen / 2, max_match_ctx);
  size_t s_end_start_idx = sidx + matchlen - MIN(matchlen - (matchlen / 2), max_match_ctx);
  size_t s_end_idx = MIN(sidx + matchlen + max_ext_ctx, ss);
  
  size_t cur_col = 0;

  size_t m_und_start_col;
  size_t m_und_end_col;
  size_t s_und_start_col;
  size_t s_und_end_col;

  
  cur_col += fprintf(f, "  src: ");
  if (m_start_idx) {
    cur_col += fprintf(f, "[%ld bytes]", m_start_idx);
  }
  cur_col += fprintf(f, "'");

  m_und_start_col = cur_col + midx - m_start_idx;
  for (size_t i = m_start_idx; i < m_start_end_idx; i++) {
    safe_print_char(f, srcbuf[i]);
  }
  cur_col += m_start_end_idx - m_start_idx;

  if (m_start_end_idx != m_end_start_idx) {
    cur_col += fprintf(f, "'[%ld bytes]'", m_end_start_idx - m_start_end_idx);
  }

  m_und_end_col = cur_col + midx + ml - m_end_start_idx;
  for (size_t i = m_end_start_idx; i < m_end_idx; i++) {
    safe_print_char(f, srcbuf[i]);
  }
  cur_col += m_end_idx - m_end_start_idx;

  if (m_end_idx != s_start_idx) {
    cur_col += fprintf(f, "'[%ld bytes]'", s_start_idx - m_end_idx);
  }

  s_und_start_col = cur_col + sidx - s_start_idx;
  for (size_t i = s_start_idx; i < s_start_end_idx; i++) {
    safe_print_char(f, srcbuf[i]);
  }
  cur_col += s_start_end_idx - s_start_idx;

  if (s_start_end_idx != s_end_start_idx) {
    cur_col += fprintf(f, "'[%ld bytes]'", s_end_start_idx - s_start_end_idx);
  }

  s_und_end_col = cur_col + sidx + ml - s_end_start_idx;
  for (size_t i = s_end_start_idx; i < s_end_idx; i++) {
    safe_print_char(f, srcbuf[i]);
  }
  cur_col += s_end_idx - s_end_start_idx;

  cur_col += fprintf(f, "'");
  fprintf(f, "\n");

  size_t und_col = 0;
  for (; und_col < m_und_start_col; und_col++) safe_print_char(f, ' ');
  for (; und_col < m_und_end_col; und_col++) safe_print_char(f, 'v');
  for (; und_col < s_und_start_col; und_col++) safe_print_char(f, ' ');
  for (; und_col < s_und_end_col; und_col++) safe_print_char(f, '^');

  fprintf(f, "\n");
}


// trivial compressor: encode the whole thing as one literal chunk
size_t noop_compress(
    byte_t* dst, size_t dstsize,
    const byte_t* src, size_t srcsize) {
  const byte_t* srcp = src;
  byte_t* dstend = dst + dstsize;
  byte_t* dstp = dst;
  CHECK(varint_encode(&dstp, dstend - dstp, srcsize), "couldn't encode decompressed size");
  CHECK(varint_encode(&dstp, dstend - dstp, srcsize), "couldn't encode litlen");
  CHECK(dstend - dstp >= (ptrdiff_t)srcsize, "input too large for destination buffer");
  memcpy(dstp, srcp, srcsize);
  dstp += srcsize;
  return dstp - dst;
}
