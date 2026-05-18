/*
ZXC - High-performance lossless compression

Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
SPDX-License-Identifier: BSD-3-Clause
*/

package zxc

/*
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zxc_seekable.h"
*/
import "C"

import (
	"unsafe"
)

// ============================================================================
// Seekable API (random-access decompression, single-threaded)
// ============================================================================

// Seekable is a handle to a seekable ZXC archive.
//
// Build one with [Open] (file-backed) or [OpenBytes] (in-memory). Always
// defer a call to [Seekable.Close] to release the native resources.
//
// A Seekable handle is single-threaded: callers must not invoke its methods
// from more than one goroutine concurrently.
type Seekable struct {
	ptr  *C.zxc_seekable
	file *C.FILE        // set when opened via Open
	cbuf unsafe.Pointer // C-owned copy of source bytes (OpenBytes)
}

// Open opens a seekable archive from a file path.
//
// The file is opened in binary read mode through the C runtime and remains
// open for the lifetime of the returned handle. Close releases both the
// archive handle and the underlying FILE*.
func Open(path string) (*Seekable, error) {
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))
	mode := C.CString("rb")
	defer C.free(unsafe.Pointer(mode))

	f, err := C.fopen(cpath, mode)
	if f == nil {
		if err != nil {
			return nil, err
		}
		return nil, ErrIO
	}

	ptr := C.zxc_seekable_open_file(f)
	if ptr == nil {
		C.fclose(f)
		return nil, ErrInvalidData
	}
	return &Seekable{ptr: ptr, file: f}, nil
}

// OpenBytes opens a seekable archive from an in-memory buffer.
//
// The library requires the source buffer to remain valid for the lifetime
// of the handle. To satisfy CGO pointer rules and avoid keeping a Go slice
// pinned, OpenBytes copies the buffer into C-allocated memory. The copy is
// freed by [Seekable.Close].
func OpenBytes(data []byte) (*Seekable, error) {
	if len(data) == 0 {
		return nil, ErrSrcTooSmall
	}

	cbuf := C.malloc(C.size_t(len(data)))
	if cbuf == nil {
		return nil, ErrMemory
	}
	C.memcpy(cbuf, unsafe.Pointer(&data[0]), C.size_t(len(data)))

	ptr := C.zxc_seekable_open(cbuf, C.size_t(len(data)))
	if ptr == nil {
		C.free(cbuf)
		return nil, ErrInvalidData
	}
	return &Seekable{ptr: ptr, cbuf: cbuf}, nil
}

// Close releases the native resources associated with the handle. Safe to
// call multiple times; subsequent calls are no-ops.
func (s *Seekable) Close() error {
	if s == nil || s.ptr == nil {
		return nil
	}
	C.zxc_seekable_free(s.ptr)
	s.ptr = nil

	if s.file != nil {
		C.fclose(s.file)
		s.file = nil
	}
	if s.cbuf != nil {
		C.free(s.cbuf)
		s.cbuf = nil
	}
	return nil
}

// NumBlocks returns the total number of data blocks in the archive
// (excluding the EOF marker block). Returns 0 if the handle has been
// closed.
func (s *Seekable) NumBlocks() uint32 {
	if s == nil || s.ptr == nil {
		return 0
	}
	return uint32(C.zxc_seekable_get_num_blocks(s.ptr))
}

// DecompressedSize returns the total decompressed size of the archive in
// bytes. Returns 0 if the handle has been closed.
func (s *Seekable) DecompressedSize() uint64 {
	if s == nil || s.ptr == nil {
		return 0
	}
	return uint64(C.zxc_seekable_get_decompressed_size(s.ptr))
}

// BlockCompressedSize returns the on-disk size of a specific block (block
// header + payload + optional per-block checksum). The second return value
// is false if blockIdx is out of range or the handle has been closed.
func (s *Seekable) BlockCompressedSize(blockIdx uint32) (uint32, bool) {
	if s == nil || s.ptr == nil || blockIdx >= s.NumBlocks() {
		return 0, false
	}
	return uint32(C.zxc_seekable_get_block_comp_size(s.ptr, C.uint32_t(blockIdx))), true
}

// BlockDecompressedSize returns the decompressed size of a specific block.
// The second return value is false if blockIdx is out of range or the
// handle has been closed.
func (s *Seekable) BlockDecompressedSize(blockIdx uint32) (uint32, bool) {
	if s == nil || s.ptr == nil || blockIdx >= s.NumBlocks() {
		return 0, false
	}
	return uint32(C.zxc_seekable_get_block_decomp_size(s.ptr, C.uint32_t(blockIdx))), true
}

// DecompressRange decompresses length bytes starting at offset (in the
// original uncompressed byte stream) into dst.
//
// Only the blocks overlapping the requested range are read and
// decompressed. dst must be at least length bytes long. Returns the number
// of bytes written, which equals length on success.
func (s *Seekable) DecompressRange(dst []byte, offset uint64, length int) (int, error) {
	if s == nil || s.ptr == nil {
		return 0, ErrNullInput
	}
	if length < 0 {
		return 0, ErrInvalidData
	}
	if len(dst) < length {
		return 0, ErrDstTooSmall
	}
	var dptr unsafe.Pointer
	if len(dst) > 0 {
		dptr = unsafe.Pointer(&dst[0])
	}
	res := C.zxc_seekable_decompress_range(
		s.ptr,
		dptr,
		C.size_t(len(dst)),
		C.uint64_t(offset),
		C.size_t(length),
	)
	if res < 0 {
		return 0, errorFromCode(res)
	}
	return int(res), nil
}

// ============================================================================
// Low-level seek table helpers
// ============================================================================

// SeekTableSize returns the encoded byte size of a seek table covering
// numBlocks data blocks. Use this to size a destination buffer for
// [WriteSeekTable].
func SeekTableSize(numBlocks uint32) int {
	return int(C.zxc_seek_table_size(C.uint32_t(numBlocks)))
}

// WriteSeekTable writes a seek table (header + entries) into dst.
// compSizes is the slice of per-block on-disk compressed sizes, in order.
//
// Most callers do not need this directly: the file and streaming APIs
// already emit a seek table when [WithSeekable] is enabled.
func WriteSeekTable(dst []byte, compSizes []uint32) (int, error) {
	if len(dst) == 0 {
		return 0, ErrDstTooSmall
	}
	if len(compSizes) == 0 {
		return 0, ErrInvalidData
	}
	res := C.zxc_write_seek_table(
		(*C.uint8_t)(unsafe.Pointer(&dst[0])),
		C.size_t(len(dst)),
		(*C.uint32_t)(unsafe.Pointer(&compSizes[0])),
		C.uint32_t(len(compSizes)),
	)
	if res < 0 {
		return 0, errorFromCode(res)
	}
	return int(res), nil
}
