/*
ZXC - High-performance lossless compression

Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
SPDX-License-Identifier: BSD-3-Clause
*/

package zxc

/*
#include <stdio.h>
#include "zxc_stream.h"
*/
import "C"
import (
	"fmt"
	"os"
	"runtime"
)

// ============================================================================
// Streaming API (file-based)
// ============================================================================

// CompressFile compresses src to dst using multi-threaded streaming.
//
// This is the recommended method for large files. It uses an asynchronous
// pipeline with separate reader, worker, and writer threads.
//
// Options: [WithLevel], [WithChecksum], [WithThreads].
//
//	n, err := zxc.CompressFile("input.bin", "output.zxc",
//	    zxc.WithLevel(zxc.LevelCompact), zxc.WithThreads(4))
func CompressFile(input, output string, opts ...Option) (int64, error) {
	o := applyOptions(opts)

	fIn, err := os.Open(input)
	if err != nil {
		return 0, fmt.Errorf("zxc: open input: %w", err)
	}
	defer fIn.Close()

	fOut, err := os.Create(output)
	if err != nil {
		return 0, fmt.Errorf("zxc: create output: %w", err)
	}
	defer fOut.Close()

	// Duplicate file descriptors so the C FILE* owns its own fd.
	cIn, err := dupFileRead(fIn)
	if err != nil {
		return 0, err
	}
	defer C.fclose(cIn)

	cOut, err := dupFileWrite(fOut)
	if err != nil {
		return 0, err
	}
	outClosed := false
	defer func() {
		if !outClosed {
			C.fclose(cOut)
		}
	}()

	var copts C.zxc_compress_opts_t
	copts.n_threads = C.int(o.threads)
	copts.level = C.int(o.level)
	if o.checksum {
		copts.checksum_enabled = 1
	}
	if o.seekable {
		copts.seekable = 1
	}
	var pinner runtime.Pinner
	defer pinner.Unpin()
	if err := setCompressDict(&copts, o, &pinner); err != nil {
		return 0, err
	}

	result := C.zxc_stream_compress(cIn, cOut, &copts)

	// fclose flushes the last stdio buffer of compressed output; a failure
	// here (ENOSPC, EIO) means the archive tail never reached the OS, so it
	// must be reported instead of being swallowed by a deferred close.
	outClosed = true
	closeRC := C.fclose(cOut)
	if result < 0 {
		return 0, errorFromCode(result)
	}
	if closeRC != 0 {
		return 0, ErrIO
	}
	return int64(result), nil
}

// FileDecompressedSize reads the original uncompressed size from the footer
// of a ZXC compressed file without performing decompression. The file is
// opened in "rb" mode; the underlying file position is restored internally.
func FileDecompressedSize(path string) (int64, error) {
	f, err := os.Open(path)
	if err != nil {
		return 0, fmt.Errorf("zxc: open input: %w", err)
	}
	defer f.Close()

	cf, err := dupFileRead(f)
	if err != nil {
		return 0, err
	}
	defer C.fclose(cf)

	result := C.zxc_stream_get_decompressed_size(cf)
	if result < 0 {
		return 0, errorFromCode(result)
	}
	return int64(result), nil
}

// DecompressFile decompresses src to dst using multi-threaded streaming.
//
// Options: [WithChecksum], [WithThreads].
//
//	n, err := zxc.DecompressFile("compressed.zxc", "output.bin")
func DecompressFile(input, output string, opts ...Option) (int64, error) {
	o := applyOptions(opts)

	fIn, err := os.Open(input)
	if err != nil {
		return 0, fmt.Errorf("zxc: open input: %w", err)
	}
	defer fIn.Close()

	fOut, err := os.Create(output)
	if err != nil {
		return 0, fmt.Errorf("zxc: create output: %w", err)
	}
	defer fOut.Close()

	cIn, err := dupFileRead(fIn)
	if err != nil {
		return 0, err
	}
	defer C.fclose(cIn)

	cOut, err := dupFileWrite(fOut)
	if err != nil {
		return 0, err
	}
	outClosed := false
	defer func() {
		if !outClosed {
			C.fclose(cOut)
		}
	}()

	var dopts C.zxc_decompress_opts_t
	dopts.n_threads = C.int(o.threads)
	if o.checksum {
		dopts.checksum_enabled = 1
	}
	var pinner runtime.Pinner
	defer pinner.Unpin()
	if err := setDecompressDict(&dopts, o, &pinner); err != nil {
		return 0, err
	}

	result := C.zxc_stream_decompress(cIn, cOut, &dopts)

	// See CompressFile: the fclose flush result is part of the success path.
	outClosed = true
	closeRC := C.fclose(cOut)
	if result < 0 {
		return 0, errorFromCode(result)
	}
	if closeRC != 0 {
		return 0, ErrIO
	}
	return int64(result), nil
}
