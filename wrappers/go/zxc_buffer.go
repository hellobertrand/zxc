// SPDX-License-Identifier: BSD-3-Clause
// ZXC - High-performance lossless compression
//
// Copyright (c) Bertrand Lebonnois and contributors.

package zxc

/*
#include "zxc.h"
*/
import "C"
import (
	"math"
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
// Options: [WithLevel], [WithChecksum], [WithSeekable], [WithDict].
//
//	out, err := zxc.Compress(data, zxc.WithLevel(zxc.LevelCompact))
func Compress(data []byte, opts ...Option) ([]byte, error) {
	dst := make([]byte, CompressBound(len(data)))
	n, err := CompressTo(data, dst, opts...)
	if err != nil {
		return nil, err
	}
	return dst[:n], nil
}

// CompressTo compresses data into a pre-allocated output buffer.
// Returns the number of bytes written. Empty input yields a minimal frame,
// as with [Compress].
//
// Options: same as [Compress].
func CompressTo(data []byte, output []byte, opts ...Option) (int, error) {
	if len(output) == 0 {
		return 0, ErrDstTooSmall
	}

	o := applyOptions(opts)

	var copts C.zxc_compress_opts_t
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

	// &data[0] panics on an empty slice; pass a valid non-nil pointer instead
	// (the C side reads 0 bytes) so empty input yields a minimal 32-byte frame.
	var dummy [1]byte
	srcPtr := unsafe.Pointer(&dummy[0])
	if len(data) > 0 {
		srcPtr = unsafe.Pointer(&data[0])
	}

	written := C.zxc_compress(
		srcPtr,
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

// DecompressedSize returns the original uncompressed size the footers store,
// summed over concatenated archives. Returns 0, ErrInvalidData if the data is
// too small or invalid.
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

// FrameInfo is what a frame's header and footer declare.
type FrameInfo struct {
	DecompressedSize uint64 // source bytes the frame decodes to
	CompressedSize   uint64 // compressed bytes of the frame, footer included
	Digest           uint64 // archive digest; 0 when HasChecksum is false
	BlockSize        int
	DictID           uint32 // dictionary the frame needs; 0 for none
	FormatVersion    uint8
	HasChecksum      bool
	HasSeekTable     bool
}

// GetFrameInfo reads a frame's header and footer, without decoding. The frame
// must span all of data: concatenated archives are refused, [DecompressedSize]
// gives their total.
func GetFrameInfo(data []byte) (FrameInfo, error) {
	if len(data) == 0 {
		return FrameInfo{}, ErrInvalidData
	}
	var fi C.zxc_frame_info_t
	rc := C.zxc_get_frame_info(unsafe.Pointer(&data[0]), C.size_t(len(data)), &fi,
		C.size_t(unsafe.Sizeof(fi)))
	if rc != C.ZXC_OK {
		return FrameInfo{}, errorFromCode(C.int64_t(rc))
	}
	return FrameInfo{
		DecompressedSize: uint64(fi.decompressed_size),
		CompressedSize:   uint64(fi.compressed_size),
		Digest:           uint64(fi.digest),
		BlockSize:        int(fi.block_size),
		DictID:           uint32(fi.dict_id),
		FormatVersion:    uint8(fi.format_version),
		HasChecksum:      fi.has_checksum != 0,
		HasSeekTable:     fi.has_seek_table != 0,
	}, nil
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
	if err := setDecompressDict(&dopts, o, &pinner); err != nil {
		return nil, err
	}

	size := uint64(C.zxc_get_decompressed_size(
		unsafe.Pointer(&data[0]),
		C.size_t(len(data)),
	))

	// The footer value is plausibility-checked in C (a forged size returns 0);
	// only guard what Go's make() cannot represent.
	if size > math.MaxInt {
		return nil, ErrInvalidData
	}

	if size == 0 {
		// Ambiguous: a valid empty-payload archive, or input the C envelope
		// rejected. Decoding into a zero-length buffer settles it. No
		// destination was supplied, so DST_TOO_SMALL cannot be about the
		// caller's buffer: blocks contradict the empty size the footer claims,
		// which reads as invalid data.
		var dummy [1]byte
		written := C.zxc_decompress(
			unsafe.Pointer(&data[0]),
			C.size_t(len(data)),
			unsafe.Pointer(&dummy[0]),
			C.size_t(0),
			&dopts,
		)
		if written < 0 {
			if int(written) == int(C.ZXC_ERROR_DST_TOO_SMALL) {
				return nil, ErrInvalidData
			}
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
	if err := setDecompressDict(&dopts, o, &pinner); err != nil {
		return 0, err
	}

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
