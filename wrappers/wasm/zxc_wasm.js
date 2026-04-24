/**
 * ZXC WebAssembly Wrapper
 *
 * High-level JavaScript API for ZXC compression/decompression via WASM.
 *
 * @example
 * import createZXC from './zxc_wasm.js';
 * const zxc = await createZXC();
 *
 * const compressed = zxc.compress(inputUint8Array, { level: 3 });
 * const decompressed = zxc.decompress(compressed);
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * Initialise the ZXC WASM module and return an ergonomic API object.
 *
 * @param {object} [moduleOverrides] - Optional Emscripten module overrides
 *        (e.g. { locateFile: ... } for custom .wasm path resolution).
 * @param {Function} [factory] - Optional Emscripten module factory. If
 *        omitted, the default sibling `./zxc.js` is dynamically imported.
 * @returns {Promise<ZXC>} Resolved API object.
 */
export default async function createZXC(moduleOverrides, factory) {
    const ZXCModule = factory || (await import('./zxc.js')).default;
    const Module = await ZXCModule(moduleOverrides || {});

    // --- Wrap C functions via cwrap ------------------------------------------
    const _compress = Module.cwrap('zxc_compress', 'number',
        ['number', 'number', 'number', 'number', 'number']);
    const _decompress = Module.cwrap('zxc_decompress', 'number',
        ['number', 'number', 'number', 'number', 'number']);
    const _compress_bound = Module.cwrap('zxc_compress_bound', 'number', ['number']);
    const _get_decompressed_size = Module.cwrap('zxc_get_decompressed_size', 'number',
        ['number', 'number']);

    const _create_cctx = Module.cwrap('zxc_create_cctx', 'number', ['number']);
    const _free_cctx   = Module.cwrap('zxc_free_cctx', 'void', ['number']);
    const _compress_cctx = Module.cwrap('zxc_compress_cctx', 'number',
        ['number', 'number', 'number', 'number', 'number', 'number']);

    const _create_dctx = Module.cwrap('zxc_create_dctx', 'number', []);
    const _free_dctx   = Module.cwrap('zxc_free_dctx', 'void', ['number']);
    const _decompress_dctx = Module.cwrap('zxc_decompress_dctx', 'number',
        ['number', 'number', 'number', 'number', 'number', 'number']);

    // Push streaming API
    const _cstream_create   = Module.cwrap('zxc_cstream_create', 'number', ['number']);
    const _cstream_free     = Module.cwrap('zxc_cstream_free', 'void', ['number']);
    const _cstream_compress = Module.cwrap('zxc_cstream_compress', 'number',
        ['number', 'number', 'number']);
    const _cstream_end      = Module.cwrap('zxc_cstream_end', 'number', ['number', 'number']);
    const _cstream_in_size  = Module.cwrap('zxc_cstream_in_size', 'number', ['number']);
    const _cstream_out_size = Module.cwrap('zxc_cstream_out_size', 'number', ['number']);

    const _dstream_create     = Module.cwrap('zxc_dstream_create', 'number', ['number']);
    const _dstream_free       = Module.cwrap('zxc_dstream_free', 'void', ['number']);
    const _dstream_decompress = Module.cwrap('zxc_dstream_decompress', 'number',
        ['number', 'number', 'number']);
    const _dstream_finished   = Module.cwrap('zxc_dstream_finished', 'number', ['number']);
    const _dstream_in_size    = Module.cwrap('zxc_dstream_in_size', 'number', ['number']);
    const _dstream_out_size   = Module.cwrap('zxc_dstream_out_size', 'number', ['number']);

    const _version_string = Module.cwrap('zxc_version_string', 'string', []);
    const _error_name     = Module.cwrap('zxc_error_name', 'string', ['number']);
    const _min_level      = Module.cwrap('zxc_min_level', 'number', []);
    const _max_level      = Module.cwrap('zxc_max_level', 'number', []);
    const _default_level  = Module.cwrap('zxc_default_level', 'number', []);

    const _malloc = Module._malloc;
    const _free   = Module._free;

    // --- Options struct layout -----------------------------------------------
    // zxc_compress_opts_t (WASM32 layout):
    //   int n_threads (4)  | int level (4)  | size_t block_size (4 in wasm32)
    //   int checksum_enabled (4) | int seekable (4)
    //   ptr progress_cb (4) | ptr user_data (4)
    // Total: 28 bytes in WASM32
    const COMPRESS_OPTS_SIZE = 28;

    // zxc_decompress_opts_t:
    //   int n_threads (4) | int checksum_enabled (4) | ptr progress_cb (4) | ptr user_data (4)
    // Total: 16 bytes in WASM32
    const DECOMPRESS_OPTS_SIZE = 16;

    // zxc_inbuf_t / zxc_outbuf_t (WASM32 layout):
    //   ptr src/dst (4) | size_t size (4) | size_t pos (4)
    // Total: 12 bytes in WASM32
    const IO_BUF_SIZE = 12;

    /**
     * Write a zxc_compress_opts_t struct into WASM memory.
     * @returns {number} Pointer to the struct (caller must free).
     */
    function _writeCompressOpts(level, checksum, seekable) {
        const ptr = _malloc(COMPRESS_OPTS_SIZE);
        // Zero-fill first
        for (let i = 0; i < COMPRESS_OPTS_SIZE; i++) {
            Module.HEAPU8[ptr + i] = 0;
        }
        // n_threads = 0 (offset 0)
        Module.HEAP32[(ptr >> 2) + 0] = 0;
        // level (offset 4)
        Module.HEAP32[(ptr >> 2) + 1] = level;
        // block_size = 0 (default, offset 8)
        Module.HEAPU32[(ptr >> 2) + 2] = 0;
        // checksum_enabled (offset 12)
        Module.HEAP32[(ptr >> 2) + 3] = checksum ? 1 : 0;
        // seekable (offset 16)
        Module.HEAP32[(ptr >> 2) + 4] = seekable ? 1 : 0;
        // progress_cb = NULL (offset 20), user_data = NULL (offset 24)
        return ptr;
    }

    /**
     * Write a zxc_decompress_opts_t struct into WASM memory.
     * @returns {number} Pointer to the struct (caller must free).
     */
    function _writeDecompressOpts(checksum) {
        const ptr = _malloc(DECOMPRESS_OPTS_SIZE);
        for (let i = 0; i < DECOMPRESS_OPTS_SIZE; i++) {
            Module.HEAPU8[ptr + i] = 0;
        }
        // n_threads = 0 (offset 0)
        Module.HEAP32[(ptr >> 2) + 0] = 0;
        // checksum_enabled (offset 4)
        Module.HEAP32[(ptr >> 2) + 1] = checksum ? 1 : 0;
        // progress_cb = NULL (offset 8), user_data = NULL (offset 12)
        return ptr;
    }

    // --- Public API ----------------------------------------------------------

    /**
     * Compress a Uint8Array.
     *
     * @param {Uint8Array} data - Input data to compress.
     * @param {object} [opts] - Options.
     * @param {number} [opts.level=3] - Compression level (1-5).
     * @param {boolean} [opts.checksum=false] - Enable checksums.
     * @param {boolean} [opts.seekable=false] - Append seek table for random-access.
     * @returns {Uint8Array} Compressed data.
     * @throws {Error} On compression failure.
     */
    function compress(data, opts) {
        const level    = (opts && opts.level)    || _default_level();
        const checksum = (opts && opts.checksum) || false;
        const seekable = (opts && opts.seekable) || false;

        const bound = _compress_bound(data.length);
        if (bound === 0) throw new Error('ZXC: compress_bound returned 0');

        const srcPtr = _malloc(data.length);
        const dstPtr = _malloc(bound);
        const optsPtr = _writeCompressOpts(level, checksum, seekable);

        try {
            Module.HEAPU8.set(data, srcPtr);
            const result = _compress(srcPtr, data.length, dstPtr, bound, optsPtr);
            if (result < 0) {
                throw new Error(`ZXC compress error: ${_error_name(result)} (${result})`);
            }
            return new Uint8Array(Module.HEAPU8.buffer, dstPtr, result).slice();
        } finally {
            _free(srcPtr);
            _free(dstPtr);
            _free(optsPtr);
        }
    }

    /**
     * Decompress a Uint8Array.
     *
     * @param {Uint8Array} data - Compressed data.
     * @param {object} [opts] - Options.
     * @param {boolean} [opts.checksum=false] - Verify checksums.
     * @returns {Uint8Array} Decompressed data.
     * @throws {Error} On decompression failure.
     */
    function decompress(data, opts) {
        const checksum = (opts && opts.checksum) || false;

        // Read decompressed size from footer
        const srcPtr = _malloc(data.length);
        Module.HEAPU8.set(data, srcPtr);

        const origSize = _get_decompressed_size(srcPtr, data.length);
        if (origSize === 0) {
            _free(srcPtr);
            throw new Error('ZXC: cannot read decompressed size (invalid archive?)');
        }

        const dstPtr = _malloc(origSize);
        const optsPtr = _writeDecompressOpts(checksum);

        try {
            const result = _decompress(srcPtr, data.length, dstPtr, origSize, optsPtr);
            if (result < 0) {
                throw new Error(`ZXC decompress error: ${_error_name(result)} (${result})`);
            }
            return new Uint8Array(Module.HEAPU8.buffer, dstPtr, result).slice();
        } finally {
            _free(srcPtr);
            _free(dstPtr);
            _free(optsPtr);
        }
    }

    /**
     * Returns the maximum compressed output size for a given input size.
     * @param {number} inputSize
     * @returns {number}
     */
    function compressBound(inputSize) {
        return _compress_bound(inputSize);
    }

    /**
     * Reads the decompressed size from a compressed buffer without decompressing.
     * @param {Uint8Array} data - Compressed data.
     * @returns {number} Original uncompressed size, or 0 if invalid.
     */
    function getDecompressedSize(data) {
        const ptr = _malloc(data.length);
        Module.HEAPU8.set(data, ptr);
        const size = _get_decompressed_size(ptr, data.length);
        _free(ptr);
        return size;
    }

    /**
     * Create a reusable compression context for high-frequency usage.
     * Call .free() when done to release WASM memory.
     *
     * @param {object} [opts] - Default options.
     * @param {number} [opts.level=3] - Default compression level.
     * @param {boolean} [opts.checksum=false] - Default checksum setting.
     * @returns {{ compress: Function, free: Function }}
     */
    function createCompressContext(opts) {
        const level    = (opts && opts.level)    || _default_level();
        const checksum = (opts && opts.checksum) || false;
        const seekable = (opts && opts.seekable) || false;

        const optsPtr = _writeCompressOpts(level, checksum, seekable);
        const cctx = _create_cctx(optsPtr);
        _free(optsPtr);

        if (cctx === 0) throw new Error('ZXC: failed to create compression context');

        return {
            /**
             * Compress data using this reusable context.
             * @param {Uint8Array} data
             * @returns {Uint8Array}
             */
            compress(data) {
                const bound = _compress_bound(data.length);
                const srcPtr = _malloc(data.length);
                const dstPtr = _malloc(bound);
                try {
                    Module.HEAPU8.set(data, srcPtr);
                    const result = _compress_cctx(cctx, srcPtr, data.length, dstPtr, bound, 0);
                    if (result < 0) {
                        throw new Error(`ZXC cctx compress error: ${_error_name(result)} (${result})`);
                    }
                    return new Uint8Array(Module.HEAPU8.buffer, dstPtr, result).slice();
                } finally {
                    _free(srcPtr);
                    _free(dstPtr);
                }
            },
            /** Free the context and release WASM memory. */
            free() {
                _free_cctx(cctx);
            }
        };
    }

    /**
     * Create a reusable decompression context for high-frequency usage.
     * Call .free() when done to release WASM memory.
     *
     * @returns {{ decompress: Function, free: Function }}
     */
    function createDecompressContext() {
        const dctx = _create_dctx();
        if (dctx === 0) throw new Error('ZXC: failed to create decompression context');

        return {
            /**
             * Decompress data using this reusable context.
             * @param {Uint8Array} data
             * @returns {Uint8Array}
             */
            decompress(data) {
                const srcPtr = _malloc(data.length);
                Module.HEAPU8.set(data, srcPtr);
                const origSize = _get_decompressed_size(srcPtr, data.length);
                if (origSize === 0) {
                    _free(srcPtr);
                    throw new Error('ZXC: cannot read decompressed size');
                }
                const dstPtr = _malloc(origSize);
                try {
                    const result = _decompress_dctx(dctx, srcPtr, data.length, dstPtr, origSize, 0);
                    if (result < 0) {
                        throw new Error(`ZXC dctx decompress error: ${_error_name(result)} (${result})`);
                    }
                    return new Uint8Array(Module.HEAPU8.buffer, dstPtr, result).slice();
                } finally {
                    _free(srcPtr);
                    _free(dstPtr);
                }
            },
            /** Free the context and release WASM memory. */
            free() {
                _free_dctx(dctx);
            }
        };
    }

    // --- Push streaming helpers ----------------------------------------------

    /** Write a zxc_inbuf_t at `bufPtr` pointing to `srcPtr` (size bytes, pos=0). */
    function _writeInbuf(bufPtr, srcPtr, size) {
        Module.HEAP32[(bufPtr >> 2) + 0] = srcPtr;
        Module.HEAPU32[(bufPtr >> 2) + 1] = size;
        Module.HEAPU32[(bufPtr >> 2) + 2] = 0;
    }
    /** Write a zxc_outbuf_t at `bufPtr` pointing to `dstPtr` (size bytes, pos=0). */
    function _writeOutbuf(bufPtr, dstPtr, size) {
        Module.HEAP32[(bufPtr >> 2) + 0] = dstPtr;
        Module.HEAPU32[(bufPtr >> 2) + 1] = size;
        Module.HEAPU32[(bufPtr >> 2) + 2] = 0;
    }
    function _readPos(bufPtr) {
        return Module.HEAPU32[(bufPtr >> 2) + 2];
    }

    /* Concatenate JS-side slices into a single Uint8Array. */
    function _concatChunks(chunks, totalLen) {
        const out = new Uint8Array(totalLen);
        let off = 0;
        for (const c of chunks) {
            out.set(c, off);
            off += c.length;
        }
        return out;
    }

    /**
     * Create a push-based, single-threaded compression stream.
     * @param {object} [opts]
     * @param {number} [opts.level=3]
     * @param {boolean} [opts.checksum=false]
     * @returns {{ compress(Uint8Array): Uint8Array, end(): Uint8Array, free(): void, inSize(): number, outSize(): number }}
     */
    function createCStream(opts) {
        const level    = (opts && opts.level)    || _default_level();
        const checksum = (opts && opts.checksum) || false;

        const optsPtr = _writeCompressOpts(level, checksum, false);
        const cs = _cstream_create(optsPtr);
        _free(optsPtr);
        if (cs === 0) throw new Error('ZXC: failed to create cstream');

        // Reusable scratch buffers in the WASM heap. The compress/end calls
        // never reallocate, so these pointers stay valid for the stream's
        // lifetime even if the heap grows (cwrap re-reads HEAPU8 internally).
        const inDescPtr  = _malloc(IO_BUF_SIZE);
        const outDescPtr = _malloc(IO_BUF_SIZE);
        const stageCap   = Math.max(_cstream_out_size(cs), 64 * 1024);
        const stagePtr   = _malloc(stageCap);

        function drainCompress(srcPtr, srcLen) {
            const chunks = [];
            let total = 0;
            _writeInbuf(inDescPtr, srcPtr, srcLen);
            for (;;) {
                _writeOutbuf(outDescPtr, stagePtr, stageCap);
                const r = _cstream_compress(cs, outDescPtr, inDescPtr);
                if (r < 0) {
                    throw new Error(`ZXC cstream compress error: ${_error_name(r)} (${r})`);
                }
                const produced = _readPos(outDescPtr);
                if (produced > 0) {
                    chunks.push(new Uint8Array(Module.HEAPU8.buffer, stagePtr, produced).slice());
                    total += produced;
                }
                if (r === 0 && _readPos(inDescPtr) === srcLen) break;
            }
            return _concatChunks(chunks, total);
        }

        function drainEnd() {
            const chunks = [];
            let total = 0;
            for (;;) {
                _writeOutbuf(outDescPtr, stagePtr, stageCap);
                const r = _cstream_end(cs, outDescPtr);
                if (r < 0) {
                    throw new Error(`ZXC cstream end error: ${_error_name(r)} (${r})`);
                }
                const produced = _readPos(outDescPtr);
                if (produced > 0) {
                    chunks.push(new Uint8Array(Module.HEAPU8.buffer, stagePtr, produced).slice());
                    total += produced;
                }
                if (r === 0) break;
            }
            return _concatChunks(chunks, total);
        }

        return {
            /** Push input and return any compressed bytes produced. */
            compress(data) {
                if (data.length === 0) return drainCompress(stagePtr, 0);
                const srcPtr = _malloc(data.length);
                try {
                    Module.HEAPU8.set(data, srcPtr);
                    return drainCompress(srcPtr, data.length);
                } finally {
                    _free(srcPtr);
                }
            },
            /** Finalise: residual block + EOF + footer. */
            end() {
                return drainEnd();
            },
            /** Free the stream and its scratch buffers. */
            free() {
                _cstream_free(cs);
                _free(inDescPtr);
                _free(outDescPtr);
                _free(stagePtr);
            },
            inSize()  { return _cstream_in_size(cs); },
            outSize() { return _cstream_out_size(cs); }
        };
    }

    /**
     * Create a push-based, single-threaded decompression stream.
     * @param {object} [opts]
     * @param {boolean} [opts.checksum=false]
     * @returns {{ decompress(Uint8Array): Uint8Array, finished(): boolean, free(): void, inSize(): number, outSize(): number }}
     */
    function createDStream(opts) {
        const checksum = (opts && opts.checksum) || false;
        const optsPtr  = _writeDecompressOpts(checksum);
        const ds = _dstream_create(optsPtr);
        _free(optsPtr);
        if (ds === 0) throw new Error('ZXC: failed to create dstream');

        const inDescPtr  = _malloc(IO_BUF_SIZE);
        const outDescPtr = _malloc(IO_BUF_SIZE);
        const stageCap   = Math.max(_dstream_out_size(ds), 4096);
        const stagePtr   = _malloc(stageCap);

        return {
            /** Push compressed bytes; return any decompressed bytes produced. */
            decompress(data) {
                const srcPtr = data.length > 0 ? _malloc(data.length) : 0;
                try {
                    if (srcPtr) Module.HEAPU8.set(data, srcPtr);
                    _writeInbuf(inDescPtr, srcPtr, data.length);

                    const chunks = [];
                    let total = 0;
                    let exhausted = data.length === 0;
                    for (;;) {
                        const beforeIn = _readPos(inDescPtr);
                        _writeOutbuf(outDescPtr, stagePtr, stageCap);
                        const r = _dstream_decompress(ds, outDescPtr, inDescPtr);
                        if (r < 0) {
                            throw new Error(`ZXC dstream error: ${_error_name(r)} (${r})`);
                        }
                        const produced = _readPos(outDescPtr);
                        if (produced > 0) {
                            chunks.push(new Uint8Array(Module.HEAPU8.buffer, stagePtr, produced).slice());
                            total += produced;
                        }
                        const afterIn = _readPos(inDescPtr);
                        // Stop only when the call made no progress at all
                        // (no input consumed AND no output produced).
                        if (afterIn === beforeIn && produced === 0) break;
                        if (!exhausted && afterIn === data.length) {
                            _writeInbuf(inDescPtr, 0, 0);
                            exhausted = true;
                        }
                    }
                    return _concatChunks(chunks, total);
                } finally {
                    if (srcPtr) _free(srcPtr);
                }
            },
            /** True iff the file footer has been consumed and validated. */
            finished() { return _dstream_finished(ds) !== 0; },
            free() {
                _dstream_free(ds);
                _free(inDescPtr);
                _free(outDescPtr);
                _free(stagePtr);
            },
            inSize()  { return _dstream_in_size(ds); },
            outSize() { return _dstream_out_size(ds); }
        };
    }

    // --- Exposed API object --------------------------------------------------
    return Object.freeze({
        compress,
        decompress,
        compressBound,
        getDecompressedSize,
        createCompressContext,
        createDecompressContext,
        createCStream,
        createDStream,

        /** Library version string (e.g. "0.10.1"). */
        version: _version_string(),
        /** Minimum compression level. */
        minLevel: _min_level(),
        /** Maximum compression level. */
        maxLevel: _max_level(),
        /** Default compression level. */
        defaultLevel: _default_level(),

        /** Raw Emscripten Module (for advanced use). */
        _module: Module,
    });
}
