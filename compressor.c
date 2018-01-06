#include "compressor.h"

#include <stdlib.h>
#include <string.h>

#include "compressor_utils.h"
#include "varint.h"

cctx_t* make_cctx(void) {
  cctx_t* cctx = malloc(sizeof(cctx_t));
  CHECK(cctx, "couldn't allocate cctx");
  cctx->tablesize = 1 << TABLE_SIZE_LOG;
  cctx->table = calloc(cctx->tablesize, sizeof(size_t));
  CHECK(cctx->table, "couldn't allocate cctx table");
  // start offset at 1 so we can distinguish table lookup misses from valid
  // references to the first byte of the source
  cctx->tableoffset = 1;
  return cctx;
}

int free_cctx(cctx_t* cctx) {
  free(cctx->table);
  free(cctx);
  return 1;
}

size_t compressed_size_bound(size_t srcsize) {
  // wild guess
  return srcsize * 4 + 8;
}

size_t decompressed_size(const byte_t* src, size_t srcsize) {
  uint64_t val;
  CHECK(varint_decode(&src, srcsize, &val), "couldn't decode decompressed size");
  return val;
}

static inline hash_t hash_position(const byte_t* srcp) {
  // Multiply by large prime (stolen from LZ4) to permute 32 bit input to
  // "random" 32 bit intermediate value. Right shift to map value into
  // hashtable's domain.
  return (*((uint32_t*) srcp) * 2654435761u) >> (sizeof(uint32_t) * 8 - TABLE_SIZE_LOG);
}

static inline void put_match_for_hash(
    cctx_t* cctx, const byte_t* pos, const byte_t* base, hash_t hash) {
  cctx->table[hash] = pos - base + cctx->tableoffset;
}

static inline const byte_t* get_match_for_hash(cctx_t* cctx, const byte_t* base, hash_t hash) {
  size_t offset = cctx->table[hash];
  if (offset < cctx->tableoffset) {
    return NULL;
  }
  return base + offset - cctx->tableoffset;
}

size_t compress(
    cctx_t* cctx,
    byte_t* dst, size_t dstsize,
    const byte_t* src, size_t srcsize) {
  const byte_t* srcp = src;
  const byte_t* srcend = src + srcsize;
  byte_t* dstend = dst + dstsize;
  byte_t* dstp = dst;

  // write uncompressed size header
  CHECK(varint_encode(&dstp, dstend - dstp, srcsize), "counldn't encode decompressed size");

  // srclitstart keeps track of the point in the stream we've actually encoded
  // up to in the compressed stream. That is, it is the point at which we will
  // start the next literal block once we've found a match or reached the end
  // of the stream.
  const byte_t* srclitstart = srcp;

  // hash_position reads 4 bytes, make sure we don't run off the end of the
  // buffer
  for (; srcp < srcend - 4; srcp++) {
    // hash the bytes at the current position
    hash_t hash = hash_position(srcp);
    // check whether a previous location in the stream had the same hash
    const byte_t* srcmatch = get_match_for_hash(cctx, src, hash);
    if (srcmatch >= src) {
      // we found a hash match

      // check that the bytes actually match, and expand the match forward
      // until they don't
      size_t matchlen = 0;
      while (srcp + matchlen < srcend && srcmatch + matchlen < srcp && srcmatch[matchlen] == srcp[matchlen]) {
        matchlen++;
      }
      // expand the match backward
      const byte_t* oldsrcp = srcp;
      while (srcp > srclitstart && srcp > srcmatch + matchlen && srcmatch > src && *(srcmatch - 1) == *(srcp - 1)) {
        srcp--;
        srcmatch--;
        matchlen++;
      }
      if (matchlen > MIN_MATCH) {
        // print_match_with_context(stderr, src, srcend, srcp, srcmatch, matchlen);

        // if the match is long enough, use it
        size_t litlen = srcp - srclitstart;
        CHECK(varint_encode(&dstp, dstend - dstp, litlen), "couldn't encode litlen");
        CHECK(dstp + litlen <= dstend, "literal too big for destination buffer");
        memcpy(dstp, srclitstart, litlen);
        dstp += litlen;
        size_t matchoff = srcp - srcmatch - matchlen;
        CHECK(varint_encode(&dstp, dstend - dstp, matchoff), "couldn't encode matchoff");
        CHECK(varint_encode(&dstp, dstend - dstp, matchlen), "couldn't encode matchlen");
        srcp += matchlen - 1;
        srclitstart = srcp + 1;
      } else {
        // otherwise, abandon it (rewind may have moved srcp backwards)
        srcp = oldsrcp;
      }
    }

    // record this position's hash
    put_match_for_hash(cctx, srcp, src, hash);
  }

  CHECK(srcp <= srcend, "ran past end of source buffer");

  if (srclitstart != srcend) {
    // encode final literals
    size_t litlen = srcend - srclitstart;
    CHECK(varint_encode(&dstp, dstend - dstp, litlen), "couldn't encode litlen");
    CHECK(dstp + litlen <= dstend, "literal too big for destination buffer");
    memcpy(dstp, srclitstart, litlen);
    dstp += litlen;
  }

  // allows re-using the cctx without memsetting the table
  cctx->tableoffset += dstp - dst;

  // return the size of the compressed blob
  return dstp - dst;
}

/**
 * Decompression is very simple. In a loop, we:
 * 1. decode literal length
 * 2. copy litlen bytes from the current position in src to the current
 *    position in dst
 * 3. advance their cursors by litlen bytes
 * 4. decode match offset and length
 * 5. go back offset+length bytes in /dst/ and copy length bytes to the head
 *    of dst
 * 6. advance dst's cursor by length bytes
 */
size_t decompress(
    byte_t* dst, size_t dstsize,
    const byte_t* src, size_t srcsize) {
  const byte_t* srcp = src;
  const byte_t* srcend = src + srcsize;
  byte_t* dstp = dst;
  byte_t* dstend = dst + dstsize;

  uint64_t decompressed_size;
  CHECK(varint_decode(&srcp, srcend - srcp, &decompressed_size), "couldn't decode decompressed size");

  while (srcp < srcend) {
    size_t litlen;
    CHECK(varint_decode(&srcp, srcend - srcp, &litlen), "couldn't decode litlen");
    CHECK(srcp + litlen <= srcend, "literal extends past end of source buffer");
    CHECK(dstp + litlen <= dstend, "literal too big for destination buffer");
    memcpy(dstp, srcp, litlen);
    srcp += litlen;
    dstp += litlen;
    if (srcp >= srcend) {
      // allow eliding the final match
      break;
    }
    size_t matchoff;
    size_t matchlen;
    CHECK(varint_decode(&srcp, srcend - srcp, &matchoff), "couldn't decode match offset");
    CHECK(varint_decode(&srcp, srcend - srcp, &matchlen), "couldn't decode match length");
    const byte_t *match = dstp - matchoff - matchlen;
    CHECK(match >= dst, "illegal match: match start is before beginning of input");
    CHECK(dstp + matchlen <= dstend, "match too big for destination buffer");
    memcpy(dstp, match, matchlen);
    dstp += matchlen;
  }

  CHECK(srcp == srcend, "ran past end of source buffer");
  CHECK(dstp - dst == (ptrdiff_t) decompressed_size, "decompressed to size other than promised");

  return dstp - dst;
}
