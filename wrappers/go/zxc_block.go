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
	"unsafe"
)

// ============================================================================
// Block API (single block, no file framing)
// ============================================================================

// CompressBlockBound returns the maximum compressed size for a single block
// of inputSize bytes (no file framing, no EOF, no footer).
func CompressBlockBound(inputSize int) uint64 {
	if inputSize < 0 {
		return 0
	}
	return uint64(C.zxc_compress_block_bound(C.size_t(inputSize)))
}

// DecompressBlockBound returns the minimum destination buffer size required
// by [Dctx.DecompressBlock] for a block of uncompressedSize bytes.
//
// The fast decoder uses speculative wild-copy writes and needs a small tail
// pad beyond the declared uncompressed size. Callers that cannot oversize
// their destination buffer should use [Dctx.DecompressBlockSafe] instead.
func DecompressBlockBound(uncompressedSize int) uint64 {
	if uncompressedSize < 0 {
		return 0
	}
	return uint64(C.zxc_decompress_block_bound(C.size_t(uncompressedSize)))
}

// EstimateCctxSize returns an accurate estimate of the memory a compression
// context reserves when compressing a single block of srcSize bytes at the
// given compression level via [Cctx.CompressBlock].
//
// The estimate covers all per-chunk working buffers (chain table, literals,
// sequence/token/offset/extras buffers) plus the fixed hash tables and
// cache-line alignment padding. At level 6+ it also accounts for the
// optimal-parser scratch. It scales roughly linearly with srcSize and is
// intended for integrators that need an accurate memory budget.
//
// Returns 0 if srcSize is 0 or negative.
func EstimateCctxSize(srcSize, level int) uint64 {
	if srcSize <= 0 {
		return 0
	}
	return uint64(C.zxc_estimate_cctx_size(C.size_t(srcSize), C.int(level)))
}

// Cctx is a reusable compression context for the Block API.
//
// It wraps an opaque C handle that is freed by [Cctx.Close]. Create with
// [NewCctx] and always defer a call to Close to release the native memory.
type Cctx struct {
	ptr *C.zxc_cctx

	// Creation-time settings, used as the fallback for CompressBlock calls
	// that do not override them explicitly.
	level    Level
	checksum bool
}

// NewCctx creates a new compression context.
//
// Options [WithLevel] and [WithChecksum] are supported at creation time and
// become the defaults for every [Cctx.CompressBlock] call that does not
// override them per call.
func NewCctx(opts ...Option) (*Cctx, error) {
	o := applyOptions(opts)
	var copts C.zxc_compress_opts_t
	copts.level = C.int(o.level)
	if o.checksum {
		copts.checksum_enabled = 1
	}
	// Block size is optional; 0 lets the library pick the default.

	ptr := C.zxc_create_cctx(&copts)
	if ptr == nil {
		return nil, ErrMemory
	}
	return &Cctx{ptr: ptr, level: o.level, checksum: o.checksum}, nil
}

// Close releases the native resources held by the context. Safe to call
// multiple times.
func (c *Cctx) Close() error {
	if c == nil || c.ptr == nil {
		return nil
	}
	C.zxc_free_cctx(c.ptr)
	c.ptr = nil
	return nil
}

// CompressBlock compresses a single block using the context. Output format
// is [block_header(8B) + payload (+ optional checksum 4B)]. Use
// [CompressBlockBound] to size dst.
//
// Per-call [WithLevel] / [WithChecksum] override the values given to
// [NewCctx]; when omitted, the creation-time settings apply.
func (c *Cctx) CompressBlock(src, dst []byte, opts ...Option) (int, error) {
	if c == nil || c.ptr == nil {
		return 0, ErrNullInput
	}
	if len(src) == 0 {
		return 0, ErrSrcTooSmall
	}
	if len(dst) == 0 {
		return 0, ErrDstTooSmall
	}

	// Merge per-call options over the creation-time settings: the C side
	// always receives a non-NULL opts struct, so its own stored-level
	// fallback never triggers and the merge must happen here.
	o := applyOptions(opts)
	if !o.levelSet {
		o.level = c.level
	}
	if !o.checksumSet {
		o.checksum = c.checksum
	}
	var copts C.zxc_compress_opts_t
	copts.level = C.int(o.level)
	if o.checksum {
		copts.checksum_enabled = 1
	}

	n := C.zxc_compress_block(
		c.ptr,
		unsafe.Pointer(&src[0]),
		C.size_t(len(src)),
		unsafe.Pointer(&dst[0]),
		C.size_t(len(dst)),
		&copts,
	)
	if n < 0 {
		return 0, errorFromCode(n)
	}
	return int(n), nil
}

// Dctx is a reusable decompression context for the Block API.
type Dctx struct {
	ptr *C.zxc_dctx
}

// NewDctx creates a new decompression context.
func NewDctx() (*Dctx, error) {
	ptr := C.zxc_create_dctx()
	if ptr == nil {
		return nil, ErrMemory
	}
	return &Dctx{ptr: ptr}, nil
}

// Close releases the native resources held by the context.
func (d *Dctx) Close() error {
	if d == nil || d.ptr == nil {
		return nil
	}
	C.zxc_free_dctx(d.ptr)
	d.ptr = nil
	return nil
}

// DecompressBlock decompresses a single block produced by
// [Cctx.CompressBlock].
//
// dst should be at least [DecompressBlockBound](uncompressedSize) to enable
// the fast decode path. For a strictly-sized destination buffer use
// [Dctx.DecompressBlockSafe] instead.
func (d *Dctx) DecompressBlock(src, dst []byte, opts ...Option) (int, error) {
	if d == nil || d.ptr == nil {
		return 0, ErrNullInput
	}
	if len(src) == 0 {
		return 0, ErrSrcTooSmall
	}
	if len(dst) == 0 {
		return 0, ErrDstTooSmall
	}

	o := applyOptions(opts)
	var dopts C.zxc_decompress_opts_t
	if o.checksum {
		dopts.checksum_enabled = 1
	}

	n := C.zxc_decompress_block(
		d.ptr,
		unsafe.Pointer(&src[0]),
		C.size_t(len(src)),
		unsafe.Pointer(&dst[0]),
		C.size_t(len(dst)),
		&dopts,
	)
	if n < 0 {
		return 0, errorFromCode(n)
	}
	return int(n), nil
}

// DecompressBlockSafe is a strict-sized variant of [Dctx.DecompressBlock]:
// it accepts a destination buffer sized exactly to the uncompressed length,
// with no tail-pad required. Slightly slower than the fast path; output is
// bit-identical.
func (d *Dctx) DecompressBlockSafe(src, dst []byte, opts ...Option) (int, error) {
	if d == nil || d.ptr == nil {
		return 0, ErrNullInput
	}
	if len(src) == 0 {
		return 0, ErrSrcTooSmall
	}
	if len(dst) == 0 {
		return 0, ErrDstTooSmall
	}

	o := applyOptions(opts)
	var dopts C.zxc_decompress_opts_t
	if o.checksum {
		dopts.checksum_enabled = 1
	}

	n := C.zxc_decompress_block_safe(
		d.ptr,
		unsafe.Pointer(&src[0]),
		C.size_t(len(src)),
		unsafe.Pointer(&dst[0]),
		C.size_t(len(dst)),
		&dopts,
	)
	if n < 0 {
		return 0, errorFromCode(n)
	}
	return int(n), nil
}
