#!/bin/bash -eu

#  Copyright (c) 2025, Bertrand Lebonnois
#  All rights reserved.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree.

if [ -z "${FUZZER_TARGET:-}" ] || [ "${FUZZER_TARGET}" == "decompress" ]; then
    $CC $CFLAGS -I include \
        src/lib/zxc_common.c \
        src/lib/zxc_compress.c \
        src/lib/zxc_decompress.c \
        src/lib/zxc_driver.c \
        tests/fuzz_decompress.c \
        -o $OUT/zxc_fuzzer_decompress \
        $LIB_FUZZING_ENGINE \
        -lm -pthread
fi

if [ -z "${FUZZER_TARGET:-}" ] || [ "${FUZZER_TARGET}" == "roundtrip" ]; then
    $CC $CFLAGS -I include \
        src/lib/zxc_common.c \
        src/lib/zxc_compress.c \
        src/lib/zxc_decompress.c \
        src/lib/zxc_driver.c \
        tests/fuzz_roundtrip.c \
        -o $OUT/zxc_fuzzer_roundtrip \
        $LIB_FUZZING_ENGINE \
        -lm -pthread
fi