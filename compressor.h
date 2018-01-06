#ifndef COMPRESSOR_H
#define COMPRESSOR_H

#include <stdint.h>
#include <stddef.h>

/**
 * This implements a toy Lempel-Ziv-style compression algorithm. There is
 * currently no entropy-coding, only back-references.
 *
 * The wire format of the compressed data is as follows.
 *
 * Every message begins with a header. Currently this is just a varint
 * encoding the decompressed size of the message.
 *
 * After the header, the compressed stream is composed of a series of
 * alternating literal blocks and match instructions. The stream must start
 * with a literal. The stream may end in either a literal or a match. Other
 * than the final instance, where the match can be elided, the stream may be
 * thought of as a series of literal+match pairs. These are encoded in the
 * following way:
 *
 *   varint litlen,
 *   byte[litlen] litbytes,
 *   varint matchoff,
 *   varint matchlen
 *
 * Note that the match offset is the distance, in bytes, between the current
 * position in the stream and the /end/ of the matched block. The beginning of
 * the match block is matchoff + matchlen bytes back from the current position.
 *
 * See varint.h for a description of their encoding
 */

#ifndef TABLE_SIZE_LOG
#define TABLE_SIZE_LOG 14
#endif

#define MIN_MATCH 4

typedef unsigned char byte_t;

typedef unsigned int hash_t;

typedef struct {
  size_t* table;
  size_t tablesize; // size in entries, not bytes
  size_t tableoffset;
} cctx_t;

/**
 * Allocates a compression context.
 */
cctx_t* make_cctx(void);

/**
 * Frees a compression context.
 */
int free_cctx(cctx_t* cctx);

/**
 * Returns an upper bound on how much space it could take to compress a
 * srcsize-sized input.
 */
size_t compressed_size_bound(size_t srcsize);

/**
 * Reads decompressed size from the compressed blob's header
 */
size_t decompressed_size(const byte_t* src, size_t srcsize);

/**
 * Compresses src into dst.
 * Returns 0 on failure.
 */
size_t compress(
    cctx_t* cctx,
    byte_t* dst, size_t dstsize,
    const byte_t* src, size_t srcsize);

/**
 * Decompresses src into dst.
 * Returns 0 on failure.
 */
size_t decompress(
    byte_t* dst, size_t dstsize,
    const byte_t* src, size_t srcsize);

#endif
