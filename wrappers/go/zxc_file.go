// SPDX-License-Identifier: BSD-3-Clause
// ZXC - High-performance lossless compression
//
// Copyright (c) Bertrand Lebonnois and contributors.

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
// Options: [WithLevel], [WithChecksum], [WithThreads], [WithSeekable],
// [WithDict].
//
//	n, err := zxc.CompressFile("input.bin", "output.zxc",
//	    zxc.WithLevel(zxc.LevelCompact), zxc.WithThreads(4))
func CompressFile(input, output string, opts ...Option) (int64, error) {
	o := applyOptions(opts)

	// The pipeline reads its parameters from a configured context. Build it
	// before touching the files so a refused option never truncates the
	// output; the context is freed before the dictionary it points at is
	// unpinned.
	var pinner runtime.Pinner
	defer pinner.Unpin()
	cctx, err := createCctx(o, &pinner)
	if err != nil {
		return 0, err
	}
	defer C.zxc_free_cctx(cctx)

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

	result := C.zxc_stream_compress(cIn, cOut, cctx)

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
// Options: [WithChecksum], [WithThreads], [WithDict].
//
//	n, err := zxc.DecompressFile("compressed.zxc", "output.bin")
func DecompressFile(input, output string, opts ...Option) (int64, error) {
	o := applyOptions(opts)

	// See CompressFile: validate the options before creating the output.
	var pinner runtime.Pinner
	defer pinner.Unpin()
	dctx, err := createDctx(o, &pinner)
	if err != nil {
		return 0, err
	}
	defer C.zxc_free_dctx(dctx)

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

	result := C.zxc_stream_decompress(cIn, cOut, dctx)

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
