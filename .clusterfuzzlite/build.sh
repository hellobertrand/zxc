#!/bin/bash -eu

#  Copyright (c) 2025-2026, Bertrand Lebonnois
#  All rights reserved.
#
#  This source code is licensed under the BSD-style license found in the
#  LICENSE file in the root directory of this source tree.

AVAILABLE_FUZZERS="decompress roundtrip"

LIB_SOURCES="src/lib/zxc_common.c src/lib/zxc_compress.c src/lib/zxc_decompress.c src/lib/zxc_driver.c src/lib/zxc_dispatch.c"

for fuzzer in $AVAILABLE_FUZZERS; do
    if [ -z "${FUZZER_TARGET:-}" ] || [ "${FUZZER_TARGET}" == "$fuzzer" ]; then
        $CC $CFLAGS -I include \
            -DZXC_FUNCTION_SUFFIX=_default -DZXC_ONLY_DEFAULT \
            $LIB_SOURCES \
            tests/fuzz_${fuzzer}.c \
            -o $OUT/zxc_fuzzer_${fuzzer} \
            $LIB_FUZZING_ENGINE \
            -lm -pthread
    fi
done