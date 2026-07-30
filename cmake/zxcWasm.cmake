# ZXC - High-performance lossless compression
#
# Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
# SPDX-License-Identifier: BSD-3-Clause
#
# WebAssembly (Emscripten) Target.

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # Exported C functions (with leading underscore for Emscripten convention)
    set(ZXC_WASM_EXPORTS
        "_zxc_compress"
        "_zxc_decompress"
        "_zxc_compress_bound"
        "_zxc_get_decompressed_size"
        "_zxc_decompress_inplace"
        "_zxc_decompress_inplace_bound"
        "_zxc_create_cctx"
        "_zxc_free_cctx"
        "_zxc_compress_cctx"
        "_zxc_create_dctx"
        "_zxc_free_dctx"
        "_zxc_decompress_dctx"
        # Push streaming API
        "_zxc_cstream_create"
        "_zxc_cstream_free"
        "_zxc_cstream_compress"
        "_zxc_cstream_end"
        "_zxc_cstream_in_size"
        "_zxc_cstream_out_size"
        "_zxc_dstream_create"
        "_zxc_dstream_free"
        "_zxc_dstream_decompress"
        "_zxc_dstream_finished"
        "_zxc_dstream_in_size"
        "_zxc_dstream_out_size"
        # Seekable API
        "_zxc_seekable_open"
        "_zxc_seekable_free"
        "_zxc_seekable_get_num_blocks"
        "_zxc_seekable_get_decompressed_size"
        "_zxc_seekable_get_block_comp_size"
        "_zxc_seekable_get_block_decomp_size"
        "_zxc_seekable_set_dict"
        # Dictionary API
        "_zxc_train_dict"
        "_zxc_train_dict_huf"
        "_zxc_dict_train"
        "_zxc_dict_id"
        "_zxc_get_dict_id"
        "_zxc_dict_get_id"
        "_zxc_dict_save"
        "_zxc_dict_save_bound"
        "_zxc_dict_load"
        "_zxc_dict_huf"
        # i32-offset shim for seekable_decompress_range (see wasm_entry.c);
        # avoids the i64 offset arg which cwrap cannot pass without BigInt.
        "_zxcw_seekable_decompress_range"
        "_zxc_write_seek_table"
        "_zxc_seek_table_size"
        "_zxc_min_level"
        "_zxc_max_level"
        "_zxc_default_level"
        "_zxc_version_string"
        "_zxc_error_name"
        # Options-struct layout guards
        "_zxc_compress_opts_size"
        "_zxc_decompress_opts_size"
        "_malloc"
        "_free"
    )
    # Join list with commas for Emscripten linker flag
    string(JOIN "," ZXC_WASM_EXPORTS_STR ${ZXC_WASM_EXPORTS})

    add_executable(zxc_wasm wrappers/wasm/wasm_entry.c)
    target_link_libraries(zxc_wasm PRIVATE zxc_lib)
    set_target_properties(zxc_wasm PROPERTIES
        OUTPUT_NAME "zxc"
        SUFFIX ".js"
    )
    target_link_options(zxc_wasm PRIVATE
        "-sEXPORTED_FUNCTIONS=[${ZXC_WASM_EXPORTS_STR}]"
        "-sEXPORTED_RUNTIME_METHODS=[ccall,cwrap,UTF8ToString,getTempRet0,HEAPU8,HEAP32,HEAPU32]"
        "-sMODULARIZE=1"
        "-sEXPORT_ES6=1"
        "-sEXPORT_NAME=ZXCModule"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sINITIAL_MEMORY=2097152"
        "-sSTACK_SIZE=131072"
        "-sENVIRONMENT=web,node"
        "-sNO_FILESYSTEM=1"
        "-sSTRICT=1"
        "-sWASM_BIGINT=0"
    )
    target_compile_options(zxc_wasm PRIVATE -O3)

    message(STATUS "  WASM Target:    zxc.js + zxc.wasm")
endif()
