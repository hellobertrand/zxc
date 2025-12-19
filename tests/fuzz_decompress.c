/*
 * Copyright (c) 2025, Bertrand Lebonnois
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../include/zxc_stream.h"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    FILE* f_in = fmemopen((void*)data, size, "rb");
    if (!f_in) return 0;

    char* out_buf = NULL;
    size_t out_size = 0;
    FILE* f_out = open_memstream(&out_buf, &out_size);

    if (!f_out) {
        fclose(f_in);
        return 0;
    }

    zxc_stream_decompress(f_in, f_out, 1, 0);

    fclose(f_in);
    fclose(f_out);
    free(out_buf);

    return 0;
}