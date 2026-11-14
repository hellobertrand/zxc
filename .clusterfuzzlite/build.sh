#!/bin/bash -eu
# SPDX-License-Identifier: BSD-3-Clause
#  Copyright (c) Bertrand Lebonnois and contributors.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree.

FUZZERS="decompress roundtrip seekable pstream dict stream inplace block"
SCALAR_FUZZERS="roundtrip"

LIB_SOURCES="src/lib/zxc_common.c src/lib/zxc_compress.c src/lib/zxc_decompress.c src/lib/zxc_dict.c src/lib/zxc_driver.c src/lib/zxc_dispatch.c src/lib/zxc_huffman.c src/lib/zxc_pivco_tables.c src/lib/zxc_pstream.c src/lib/zxc_seekable.c"

COMMON_CFLAGS="-I include -I src/lib/vendors \
    -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE"
ZXC_CFLAGS="$COMMON_CFLAGS -DZXC_FUNCTION_SUFFIX=_default -DZXC_ONLY_DEFAULT"

# build <target suffix> <fuzzers> [extra flags...]
build() {
    local suffix=$1 fuzzers=$2 dir=obj$1 src fuzzer
    shift 2
    mkdir -p "$dir"
    for src in $LIB_SOURCES; do
        $CC $CFLAGS $ZXC_CFLAGS "$@" -c "$src" -o "$dir/$(basename "${src%.c}").o"
    done
    for fuzzer in $fuzzers; do
        $CC $CFLAGS $ZXC_CFLAGS "$@" -c tests/fuzz/fuzz_${fuzzer}.c -o "$dir/fuzz_${fuzzer}.o"
        $CXX $CXXFLAGS "$dir/fuzz_${fuzzer}.o" "$dir"/zxc_*.o -o $OUT/zxc_fuzzer_${fuzzer}${suffix} \
            $LIB_FUZZING_ENGINE -lm -pthread
    done
}

build "" "$FUZZERS"
build _scalar "$SCALAR_FUZZERS" -DZXC_DISABLE_SIMD

# variant <suffix> <ISA flags...>: the codec objects of one ISA variant.
variant() {
    local suffix=$1 src
    shift
    for src in compress decompress huffman; do
        $CC $CFLAGS $COMMON_CFLAGS -DZXC_FUNCTION_SUFFIX=$suffix "$@" \
            -c src/lib/zxc_$src.c -o "objvariants/zxc_${src}${suffix}.o"
    done
}

# The variants target calls each ISA variant directly and picks at run time
# among those the CPU supports; elsewhere than x86_64 it has `_default` only.
mkdir -p objvariants
VARIANT_DEFS=""
if [ "${ARCHITECTURE:-$(uname -m)}" = "x86_64" ]; then
    variant _avx2 -mavx2 -mbmi -mbmi2 -mlzcnt
    variant _avx512 -mavx512f -mavx512bw -mavx512vbmi -mavx512vbmi2 -mbmi -mbmi2 -mlzcnt
    VARIANT_DEFS="-DFUZZ_X86_VARIANTS"
fi
$CC $CFLAGS $ZXC_CFLAGS $VARIANT_DEFS -c tests/fuzz/fuzz_variants.c -o objvariants/fuzz_variants.o
$CXX $CXXFLAGS objvariants/*.o obj/zxc_*.o -o $OUT/zxc_fuzzer_variants \
    $LIB_FUZZING_ENGINE -lm -pthread
