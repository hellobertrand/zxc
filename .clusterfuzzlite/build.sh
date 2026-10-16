#!/bin/bash -eu
# SPDX-License-Identifier: BSD-3-Clause
#  Copyright (c) Bertrand Lebonnois and contributors.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree.

FUZZERS="decompress roundtrip seekable pstream dict stream inplace block"
SCALAR_FUZZERS="roundtrip"

LIB_SOURCES="src/lib/zxc_common.c src/lib/zxc_compress.c src/lib/zxc_decompress.c src/lib/zxc_dict.c src/lib/zxc_driver.c src/lib/zxc_dispatch.c src/lib/zxc_huffman.c src/lib/zxc_pivco_tables.c src/lib/zxc_pstream.c src/lib/zxc_seekable.c"

ZXC_CFLAGS="-I include -I src/lib/vendors \
    -DZXC_FUNCTION_SUFFIX=_default -DZXC_ONLY_DEFAULT \
    -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE"

# build <target suffix> <fuzzers> [extra flags...]
build() {
    local suffix=$1 fuzzers=$2 dir=obj$1 src fuzzer
    shift 2
    mkdir -p "$dir"
    for src in $LIB_SOURCES; do
        $CC $CFLAGS $ZXC_CFLAGS "$@" -c "$src" -o "$dir/$(basename "${src%.c}").o"
    done
    for fuzzer in $fuzzers; do
        $CC $CFLAGS $ZXC_CFLAGS "$@" -c tests/fuzz_${fuzzer}.c -o "$dir/fuzz_${fuzzer}.o"
        $CXX $CXXFLAGS "$dir/fuzz_${fuzzer}.o" "$dir"/zxc_*.o -o $OUT/zxc_fuzzer_${fuzzer}${suffix} \
            $LIB_FUZZING_ENGINE -lm -pthread
    done
}

build "" "$FUZZERS"
build _scalar "$SCALAR_FUZZERS" -DZXC_DISABLE_SIMD
