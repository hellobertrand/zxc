#!/bin/bash -eu

$CC $CFLAGS -I include \
    src/lib/zxc_common.c \
    src/lib/zxc_compress.c \
    src/lib/zxc_decompress.c \
    src/lib/zxc_driver.c \
    tests/fuzz.c \
    -o $OUT/zxc_fuzzer \
    $LIB_FUZZING_ENGINE \
    -lm -pthread
