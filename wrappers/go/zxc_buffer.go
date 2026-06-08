/*
ZXC - High-performance lossless compression

Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
SPDX-License-Identifier: BSD-3-Clause
*/

package zxc

/*
#include "zxc.h"
*/
import "C"
import (
	"runtime"
	"unsafe"
)

// ============================================================================
// Buffer API
// ============================================================================

// CompressBound returns the maximum compressed size for an input of the given
// size. Use this to pre-allocate output buffers.
func CompressBound(inputSize int) uint64 {
	return uint64(C.zxc_compress_bound(C.size_t(inputSize)))
}

// Compress compresses data using the ZXC algorithm.
//
// Options: [WithLevel], [WithChecksum].
//
//	out, err := zxc.Compress(data, zxc.WithLevel(zxc.LevelCompact))
func Compress(data []byte, opts ...Option) ([]byte, error) {
	o := applyOptions(opts)
	bound := CompressBound(len(data))
	dst := make([]byte, bound)

	var copts C.zxc_compress_opts_t
	copts.level = C.int(o.level)
	if o.checksum {
		copts.checksum_enabled = 1
	}
	var pinner runtime.Pinner
	defer pinner.Unpin()
	setCompressDict(&copts, o, &pinner)

	// &data[0] panics on an empty slice; pass a valid non-nil pointer instead
	// (the C side reads 0 bytes) so empty input yields a minimal 36-byte frame.
	var dummy [1]byte
	srcPtr := unsafe.Pointer(&dummy[0])
	if len(data) > 0 {
		srcPtr = unsafe.Pointer(&data[0])
	}

	written := C.zxc_compress(
		srcPtr,
		C.size_t(len(data)),
		unsafe.Pointer(&dst[0]),
		C.size_t(bound),
		&copts,
	)

	if written < 0 {
		return nil, errorFromCode(written)
	}
	if written == 0 {
		return nil, ErrInvalidData
	}

	return dst[:int(written)], nil
}

// CompressTo compresses data into a pre-allocated output buffer.
// Returns the number of bytes written.
func CompressTo(data []byte, output []byte, opts ...Option) (int, error) {
	if len(data) == 0 {
		return 0, ErrSrcTooSmall
	}
	if len(output) == 0 {
		return 0, ErrDstTooSmall
	}

	o := applyOptions(opts)

	var copts C.zxc_compress_opts_t
	copts.level = C.int(o.level)
	if o.checksum {
		copts.checksum_enabled = 1
	}
	var pinner runtime.Pinner
	defer pinner.Unpin()
	setCompressDict(&copts, o, &pinner)

	written := C.zxc_compress(
		unsafe.Pointer(&data[0]),
		C.size_t(len(data)),
		unsafe.Pointer(&output[0]),
		C.size_t(len(output)),
		&copts,
	)

	if written < 0 {
		return 0, errorFromCode(written)
	}
	if written == 0 {
		return 0, ErrInvalidData
	}

	return int(written), nil
}

// DecompressedSize returns the original uncompressed size stored in the
// compressed data footer. Returns 0, ErrInvalidData if the data is too small
// or invalid.
func DecompressedSize(data []byte) (uint64, error) {
	if len(data) == 0 {
		return 0, ErrInvalidData
	}

	size := C.zxc_get_decompressed_size(
		unsafe.Pointer(&data[0]),
		C.size_t(len(data)),
	)

	if size == 0 {
		return 0, ErrInvalidData
	}
	return uint64(size), nil
}

// Decompress decompresses ZXC-compressed data.
//
// The output size is read from the compressed data footer. For pre-allocated
// buffers, use [DecompressTo].
//
// Options: [WithChecksum].
func Decompress(data []byte, opts ...Option) ([]byte, error) {
	if len(data) == 0 {
		return nil, ErrInvalidData
	}

	o := applyOptions(opts)
	var dopts C.zxc_decompress_opts_t
	if o.checksum {
		dopts.checksum_enabled = 1
	}
	var pinner runtime.Pinner
	defer pinner.Unpin()
	setDecompressDict(&dopts, o, &pinner)

	size := uint64(C.zxc_get_decompressed_size(
		unsafe.Pointer(&data[0]),
		C.size_t(len(data)),
	))

	if size == 0 {
		// Either a valid empty-payload archive or invalid input. Let the C
		// decoder decide: it returns 0 for a valid empty archive, or a negative
		// error code for invalid input. Decode into a zero-length buffer.
		var dummy [1]byte
		written := C.zxc_decompress(
			unsafe.Pointer(&data[0]),
			C.size_t(len(data)),
			unsafe.Pointer(&dummy[0]),
			C.size_t(0),
			&dopts,
		)
		if written < 0 {
			return nil, errorFromCode(written)
		}
		if written != 0 {
			return nil, ErrInvalidData
		}
		return []byte{}, nil
	}

	dst := make([]byte, size)
	written := C.zxc_decompress(
		unsafe.Pointer(&data[0]),
		C.size_t(len(data)),
		unsafe.Pointer(&dst[0]),
		C.size_t(size),
		&dopts,
	)

	if written < 0 {
		return nil, errorFromCode(written)
	}
	if uint64(written) != size {
		return nil, ErrInvalidData
	}

	return dst[:int(written)], nil
}

// DecompressTo decompresses data into a pre-allocated output buffer.
// Returns the number of bytes written.
func DecompressTo(data []byte, output []byte, opts ...Option) (int, error) {
	if len(data) == 0 {
		return 0, ErrInvalidData
	}
	if len(output) == 0 {
		return 0, ErrDstTooSmall
	}

	o := applyOptions(opts)

	var dopts C.zxc_decompress_opts_t
	if o.checksum {
		dopts.checksum_enabled = 1
	}
	var pinner runtime.Pinner
	defer pinner.Unpin()
	setDecompressDict(&dopts, o, &pinner)

	written := C.zxc_decompress(
		unsafe.Pointer(&data[0]),
		C.size_t(len(data)),
		unsafe.Pointer(&output[0]),
		C.size_t(len(output)),
		&dopts,
	)

	if written < 0 {
		return 0, errorFromCode(written)
	}

	return int(written), nil
}
