#include "compressor.h"
#include "compressor_utils.h"

#include <stdlib.h>
#include <string.h>

void usage(void) {
  fprintf(stderr,
      "Incorrect usage!\n"
      "Compresses / decompresses stdin -> stdout.\n"
      "No args to compress, -d to decompress.\n"
  );
  exit(1);
}

int main(int argc, char *argv[]) {
  int should_decompress = 0;
  int should_debug = 0;
  if (argc == 1) {
  } else if (argc == 2 && !strcmp("-d", argv[1])) {
    should_decompress = 1;
  } else if (argc == 2 && !strcmp("-D", argv[1])) {
    should_decompress = 1;
    should_debug = 1;
  } else {
    usage();
  }

  char *ibuf;
  size_t isize = 16 * 1024;
  ibuf = malloc(isize);
  CHECK1(ibuf, "failed to allocate input buffer");

  size_t ipos = 0;
  size_t bytes_read;
  while ((bytes_read = fread(ibuf + ipos, 1, isize - ipos, stdin))) {
    ipos += bytes_read;
    if (ipos == isize) {
      isize *= 2;
      ibuf = realloc(ibuf, isize);
      CHECK1(ibuf, "failed to grow input buffer");
    }
  }

  char *obuf;
  size_t osize;
  size_t opos;

  if (should_debug) {
    litandmatch_t* lams;
    size_t lamssize = 1024; // a bunch
    lams = malloc(lamssize * sizeof(litandmatch_t));
    CHECK1(lams, "failed to allocate buffer for LAMs");
    size_t numlams = decode_literals_and_matches(ibuf, ipos, lams, lamssize);
    for (size_t i = 0; i < numlams; i++) {
      print_literal_and_match(stderr, lams + i);
    }
    return 0;
  } else if (should_decompress) {
    osize = decompressed_size(ibuf, isize);
    obuf = malloc(osize);
    CHECK1(obuf, "failed to allocate output buffer");

    opos = decompress(obuf, osize, ibuf, ipos);
    CHECK1(opos, "decompression failed");
  } else {
    osize = compressed_size_bound(isize);
    obuf = malloc(osize);
    CHECK1(obuf, "failed to allocate output buffer");

    cctx_t* cctx = make_cctx();
    CHECK1(cctx, "failed to allocate compression context");

    opos = compress(cctx, obuf, osize, ibuf, ipos);
    CHECK1(opos, "compression failed");

    free_cctx(cctx);
  }

  free(ibuf);

  size_t owritten = 0;

  size_t bytes_written;
  while (opos - owritten && (bytes_written = fwrite(obuf + owritten, 1, opos - owritten, stdout))) {
    owritten += bytes_written;
  }
  CHECK1(owritten == opos, "failed to write all of the output");

  free(obuf);

  fprintf(
      stderr,
      "%s %lu bytes into %lu bytes (%.3lfx).\n",
      should_decompress ? "Decompressed" : "Compressed",
      ipos,
      opos,
      should_decompress ? ((double) opos) / ipos : ((double) ipos) / opos
  );

  return 0;
}
