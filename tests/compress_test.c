#include <assert.h>
#include <string.h>

#include "compressor.h"
#include "compressor_utils.h"

const char* TEST_STRING = "THIS IS A TEST THIS IS THIS IS A TEST";
const size_t BUF_LEN = 128;

const char* LONG_TEST_STRING = "#include \"compressor.h\"\n\n#include <stdlib.h>\n#include <string.h>\n\n#include \"compressor_utils.h\"\n#include \"varint.h\"\n\ncctx_t* make_cctx(void) {\n  cctx_t* cctx = malloc(sizeof(cctx_t));\n  CHECK(cctx, \"couldn\'t allocate cctx\");\n  cctx->tablesize = 1 << TABLE_SIZE_LOG;\n  cctx->table = calloc(cctx->tablesize, sizeof(size_t));\n  CHECK(cctx->table, \"couldn\'t allocate cctx table\");\n  // start offset at 1 so we can distinguish table lookup misses from valid\n  // references to the first byte of the source\n  cctx->tableoffset = 1;\n  return cctx;\n}\n\nint free_cctx(cctx_t* cctx) {\n  free(cctx->table);\n  free(cctx);\n  return 1;\n}\n\nstatic inline hash_t hash_position(const byte_t* srcp) {\n  // constant stolen from LZ4\n  return (*((uint32_t*) srcp) * 2654435761u) >> (sizeof(uint32_t) * 8 - TABLE_SIZE_LOG);\n}\n\nstatic inline void put_match_for_hash(\n    cctx_t* cctx, const byte_t* pos, const byte_t* base, hash_t hash) {\n  cctx->table[hash] = pos - base + cctx->tableoffset;\n}\n\nstatic inline const byte_t* get_match_for_hash(cctx_t* cctx, const byte_t* base, hash_t hash) {\n  size_t val = cctx->table[hash];\n  if (val < cctx->tableoffset) {\n    return NULL;\n  }\n  return base + val - cctx->tableoffset;\n}\n\nsize_t compressed_size_bound(size_t srcsize) {\n  // wild guess\n  return srcsize * 4 + 8;\n}\n\nsize_t decompressed_size(const byte_t* src, size_t srcsize) {\n  uint64_t val;\n  CHECK(varint_decode(&src, srcsize, &val), \"couldn\'t decode decompressed size\");\n  return val;\n}\n\nsize_t compress(\n    cctx_t* cctx,\n    byte_t* dst, size_t dstsize,\n    const byte_t* src, size_t srcsize) {\n  const byte_t* srcp = src;\n  const byte_t* srcend = src + srcsize;\n  byte_t* dstend = dst + dstsize;\n  byte_t* dstp = dst;\n\n  // write uncompressed size header\n  CHECK(varint_encode(&dstp, dstend - dstp, srcsize), \"counldn\'t encode decompressed size\");\n\n  // srclitstart keeps track of the point in the stream we\'ve actually encoded\n  // up to in the compressed stream. That is, it is the point at which we will\n  // start the next literal block once we\'ve found a match or reached the end\n  // of the stream.\n  const byte_t* srclitstart = srcp;\n\n  // hash_position reads 4 bytes, make sure we don\'t run off the end of the\n  // buffer\n  for (; srcp < srcend - 4; srcp++) {\n    // hash the bytes at the current position\n    hash_t hash = hash_position(srcp);\n    // check whether a previous location in the stream had the same hash\n    const byte_t* srcmatch = get_match_for_hash(cctx, src, hash);\n    if (srcmatch >= src) {\n      // we found a hash match\n      // check that the bytes actually match, and expand the match forward\n      // until they don\'t\n      size_t matchlen = 0;\n      while (srcp + matchlen < srcend && srcmatch + matchlen < srcp && srcmatch[matchlen] == srcp[matchlen]) {\n        matchlen++;\n      }\n      // expand the match backward\n      const byte_t* oldsrcp = srcp;\n      while (srcp > srclitstart && srcp > srcmatch + matchlen && srcmatch > src && *(srcmatch - 1) == *(srcp - 1)) {\n        srcp--;\n        srcmatch--;\n        matchlen++;\n      }\n      if (matchlen > MIN_MATCH) {\n        print_match_with_context(stderr, src, srcend, srcp, srcmatch, matchlen);\n\n        // if the match is long enough, use it\n        size_t litlen = srcp - srclitstart;\n        CHECK(varint_encode(&dstp, dstend - dstp, litlen), \"couldn\'t encode litlen\");\n        CHECK(dstp + litlen <= dstend, \"literal too big for destination buffer\");\n        memcpy(dstp, srclitstart, litlen);\n        dstp += litlen;\n        size_t matchoff = srcp - srcmatch - matchlen;\n        CHECK(varint_encode(&dstp, dstend - dstp, matchoff), \"couldn\'t encode matchoff\");\n        CHECK(varint_encode(&dstp, dstend - dstp, matchlen), \"couldn\'t encode matchlen\");\n        srcp += matchlen - 1;\n        srclitstart = srcp + 1;\n      } else {\n        // otherwise, abandon it (rewind may have moved srcp backwards)\n        srcp = oldsrcp;\n      }\n    }\n\n    // record this position\'s hash\n    put_match_for_hash(cctx, srcp, src, hash);\n  }\n\n  CHECK(srcp <= srcend, \"ran past end of source buffer\");\n\n  // encode final literals\n  if (srclitstart != srcend) {\n    size_t litlen = srcend - srclitstart;\n    CHECK(varint_encode(&dstp, dstend - dstp, litlen), \"couldn\'t encode litlen\");\n    CHECK(dstp + litlen <= dstend, \"literal too big for destination buffer\");\n    memcpy(dstp, srclitstart, litlen);\n    dstp += litlen;\n  }\n\n  // allows re-using the cctx without memsetting the table\n  cctx->tableoffset += dstp - dst;\n\n  // return the size of the compressed blob\n  return dstp - dst;\n}\n\n/**\n * Decompression is very simple. In a loop, we:\n * 1. decode literal length\n * 2. copy litlen bytes from the current position in src to the current\n *    position in dst\n * 3. advance their cursors by litlen bytes\n * 4. decode match offset and length\n * 5. go back offset+length bytes in /dst/ and copy length bytes to the head\n *    of dst\n * 6. advance dst\'s cursor by length bytes\n */\nsize_t decompress(\n    byte_t* dst, size_t dstsize,\n    const byte_t* src, size_t srcsize) {\n  const byte_t* srcp = src;\n  const byte_t* srcend = src + srcsize;\n  byte_t* dstp = dst;\n  byte_t* dstend = dst + dstsize;\n\n  uint64_t decompressed_size;\n  CHECK(varint_decode(&srcp, srcend - srcp, &decompressed_size), \"couldn\'t decode decompressed size\");\n\n  while (srcp < srcend) {\n    size_t litlen;\n    CHECK(varint_decode(&srcp, srcend - srcp, &litlen), \"couldn\'t decode litlen\");\n    CHECK(srcp + litlen <= srcend, \"literal extends past end of source buffer\");\n    CHECK(dstp + litlen <= dstend, \"literal too big for destination buffer\");\n    memcpy(dstp, srcp, litlen);\n    srcp += litlen;\n    dstp += litlen;\n    if (srcp >= srcend) {\n      // allow eliding the final match\n      break;\n    }\n    size_t matchoff;\n    size_t matchlen;\n    CHECK(varint_decode(&srcp, srcend - srcp, &matchoff), \"couldn\'t decode match offset\");\n    CHECK(varint_decode(&srcp, srcend - srcp, &matchlen), \"couldn\'t decode match length\");\n    const byte_t *match = dstp - matchoff - matchlen;\n    CHECK(match >= dst, \"illegal match: match start is before beginning of input\");\n    CHECK(dstp + matchlen <= dstend, \"match too big for destination buffer\");\n    memcpy(dstp, match, matchlen);\n    dstp += matchlen;\n  }\n  CHECK(srcp == srcend, \"ran past end of source buffer\");\n  CHECK(dstp - dst == (ptrdiff_t) decompressed_size, \"decompressed to size other than promised\");\n  return dstp - dst;\n}\n";
const size_t LONG_BUF_LEN = 128 * 1024;

typedef struct {
  const litandmatch_t* seq;
  const size_t seqlen;
  const char *expectedstr;
  const size_t expectedlen;
} seqtestcase_t;

const litandmatch_t seq1[] = {
  {0, "", 0, 0},
  {0, "", 0, 0},
  {0, "", 0, 0},
};
const litandmatch_t seq2[] = {
  {1, "1", 0, 0},
  {1, "2", 0, 0},
  {1, "3", 0, 0},
};
const litandmatch_t seq3[] = {
  {1, "1", 0, 1},
  {1, "2", 1, 1},
  {1, "3", 0, 2},
};
const litandmatch_t seq4[] = {
  {1, "1", 0, 1},
  {0, "", 0, 2},
  {0, "", 0, 4},
};
const litandmatch_t seq5[] = {
  {4, "1234", 1, 2},
};

const seqtestcase_t tests[] = {
  {seq1, 0, "", 0},
  {seq1, 1, "", 0},
  {seq1, 2, "", 0},
  {seq1, 3, "", 0},
  {seq2, 1, "1", 1},
  {seq2, 2, "12", 2},
  {seq2, 3, "123", 3},
  {seq3, 3, "1121313", 7},
  {seq4, 3, "11111111", 8},
  {seq5, 1, "123423", 6},
};

void test_manual_seq(
    const litandmatch_t* seq, size_t seqlen,
    const byte_t* expected, size_t expectedlen) {
  byte_t cbuf[BUF_LEN], dbuf[BUF_LEN];
  size_t csize, dsize;

  csize = encode_literals_and_matches(cbuf, sizeof(cbuf), seq, seqlen);

  dsize = decompress(dbuf, sizeof(dbuf), cbuf, csize);
  assert(dsize == expectedlen);
  assert(!memcmp(dbuf, expected, expectedlen));
}

void test_manual_seqs(void) {
  const seqtestcase_t* testsend = tests + (sizeof(tests) / sizeof(tests[0]));
  for (const seqtestcase_t* test = tests; test < testsend; test++) {
    test_manual_seq(test->seq, test->seqlen, test->expectedstr, test->expectedlen);
  }
}

void test_noop_roundtrip(void) {
  byte_t buf1[BUF_LEN], buf2[BUF_LEN], buf3[BUF_LEN];
  size_t size1 = strlen(TEST_STRING);
  size_t size2;
  size_t size3;

  memcpy(buf1, TEST_STRING, size1);

  size2 = noop_compress(buf2, BUF_LEN, buf1, size1);
  assert(size2);

  size3 = decompress(buf3, BUF_LEN, buf2, size2);
  assert(size3);
  assert(size3 == size1);
  assert(!memcmp(buf1, buf3, size1));
}

void test_simple_roundtrip(void) {
  byte_t buf1[BUF_LEN], buf2[BUF_LEN], buf3[BUF_LEN];
  size_t size1 = strlen(TEST_STRING);
  size_t size2;
  size_t size3;

  cctx_t* cctx = make_cctx();
  assert(cctx);

  memcpy(buf1, TEST_STRING, size1);

  size2 = compress(cctx, buf2, BUF_LEN, buf1, size1);
  assert(size2);

  size3 = decompress(buf3, BUF_LEN, buf2, size2);
  assert(size3);
  assert(size3 == size1);
  assert(!memcmp(buf1, buf3, size1));

  free_cctx(cctx);
}

void test_long_roundtrip(void) {
  byte_t buf1[LONG_BUF_LEN], buf2[LONG_BUF_LEN], buf3[LONG_BUF_LEN];
  size_t size1 = strlen(LONG_TEST_STRING);
  size_t size2;
  size_t size3;

  cctx_t* cctx = make_cctx();
  assert(cctx);

  memcpy(buf1, LONG_TEST_STRING, size1);

  size2 = compress(cctx, buf2, LONG_BUF_LEN, buf1, size1);
  assert(size2);

  size3 = decompress(buf3, LONG_BUF_LEN, buf2, size2);
  assert(size3);
  assert(size3 == size1);
  assert(!memcmp(buf1, buf3, size1));

  free_cctx(cctx);
}

void test_multiple_roundtrip(void) {
  byte_t buf1[BUF_LEN], buf2[BUF_LEN], buf3[BUF_LEN];
  size_t size1 = strlen(TEST_STRING);
  size_t size2;
  size_t size3;

  cctx_t* cctx = make_cctx();
  assert(cctx);

  memcpy(buf1, TEST_STRING, size1);

  for (int i = 0; i < 100; i++) {
    size2 = compress(cctx, buf2, BUF_LEN, buf1, size1);
    assert(size2);

    size3 = decompress(buf3, BUF_LEN, buf2, size2);
    assert(size3);
    assert(size3 == size1);
    assert(!memcmp(buf1, buf3, size1));
  }



  free_cctx(cctx);
}

int main() {
  test_simple_roundtrip();
  test_long_roundtrip();
  test_noop_roundtrip();
  test_multiple_roundtrip();
  test_manual_seqs();

  return 0;
}
