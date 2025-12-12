/*
 * Copyright (c) 2025, Bertrand Lebonnois
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/zxc.h"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    FILE* f_in = fmemopen((void*)data, size, "rb");
    if (!f_in) return 0;

    char* comp_buf = NULL;
    size_t comp_size = 0;
    FILE* f_comp = open_memstream(&comp_buf, &comp_size);

    char* decomp_buf = NULL;
    size_t decomp_size = 0;
    FILE* f_decomp = open_memstream(&decomp_buf, &decomp_size);

    if (!f_comp || !f_decomp) {
        if (f_in) fclose(f_in);
        if (f_comp) fclose(f_comp);
        if (f_decomp) fclose(f_decomp);
        if (comp_buf) free(comp_buf);
        if (decomp_buf) free(decomp_buf);
        return 0;
    }

    if (zxc_stream_compress(f_in, f_comp, 1, 2, 0) == 0) {
        fflush(f_comp);

        FILE* f_comp_read = fmemopen(comp_buf, comp_size, "rb");
        if (f_comp_read) {
            zxc_stream_decompress(f_comp_read, f_decomp, 1, 0);
            fclose(f_comp_read);
        }
    }

    fclose(f_in);
    fclose(f_comp);
    free(comp_buf);
    fclose(f_decomp);
    free(decomp_buf);

    return 0;
}