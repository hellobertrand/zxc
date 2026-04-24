/*
ZXC - High-performance lossless compression

Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
SPDX-License-Identifier: BSD-3-Clause
*/

// Package zxc provides Go bindings to the ZXC high-performance lossless
// compression library.
//
// ZXC is optimised for fast decompression speed: it is designed for
// "Write Once, Read Many" workloads such as ML datasets, game assets,
// firmware images and caches.
//
// # Quick Start
//
//	compressed, err := zxc.Compress(data)
//	if err != nil { log.Fatal(err) }
//
//	original, err := zxc.Decompress(compressed)
//	if err != nil { log.Fatal(err) }
//
// # Streaming (file-based)
//
//	n, err := zxc.CompressFile("input.bin", "output.zxc")
//	n, err  = zxc.DecompressFile("output.zxc", "restored.bin")
package zxc

/*
#cgo CFLAGS:  -I${SRCDIR}/../../include -DZXC_STATIC_DEFINE
#cgo LDFLAGS: -lpthread

#include <stdlib.h>
#include <stdio.h>
#include "zxc.h"
*/
import "C"
import (
	"errors"
	"fmt"
	"os"
	"unsafe"
)

// ============================================================================
// Compression Levels
// ============================================================================

// Level represents a ZXC compression level.
type Level int

const (
	// LevelFastest provides the fastest compression, best for real-time
	// applications (level 1).
	LevelFastest Level = C.ZXC_LEVEL_FASTEST

	// LevelFast provides fast compression, good for real-time applications
	// (level 2).
	LevelFast Level = C.ZXC_LEVEL_FAST

	// LevelDefault is the recommended default: ratio > LZ4, decode speed > LZ4
	// (level 3).
	LevelDefault Level = C.ZXC_LEVEL_DEFAULT

	// LevelBalanced provides good ratio with good decode speed (level 4).
	LevelBalanced Level = C.ZXC_LEVEL_BALANCED

	// LevelCompact provides the highest density. Best for storage, firmware
	// and assets (level 5).
	LevelCompact Level = C.ZXC_LEVEL_COMPACT
)

// AllLevels returns all available compression levels from fastest to most
// compact.
func AllLevels() []Level {
	return []Level{LevelFastest, LevelFast, LevelDefault, LevelBalanced, LevelCompact}
}

// ============================================================================
// Version
// ============================================================================

const (
	// VersionMajor is the major version of the underlying C library.
	VersionMajor = C.ZXC_VERSION_MAJOR

	// VersionMinor is the minor version of the underlying C library.
	VersionMinor = C.ZXC_VERSION_MINOR

	// VersionPatch is the patch version of the underlying C library.
	VersionPatch = C.ZXC_VERSION_PATCH
)

// Version returns the library version as (major, minor, patch).
func Version() (int, int, int) {
	return int(VersionMajor), int(VersionMinor), int(VersionPatch)
}

// VersionString returns the library version as a "major.minor.patch" string,
// computed from the compile-time constants in the C header.
//
// For the version reported by the dynamically-linked native library (useful
// for detecting ABI mismatch), call [LibraryVersion].
func VersionString() string {
	return fmt.Sprintf("%d.%d.%d", VersionMajor, VersionMinor, VersionPatch)
}

// LibraryVersion returns the version string reported by the linked native
// libzxc (e.g. "0.10.0").
func LibraryVersion() string {
	return C.GoString(C.zxc_version_string())
}

// MinLevel returns the minimum supported compression level (currently 1).
func MinLevel() int {
	return int(C.zxc_min_level())
}

// MaxLevel returns the maximum supported compression level (currently 5).
func MaxLevel() int {
	return int(C.zxc_max_level())
}

// DefaultLevel returns the default compression level (currently 3).
func DefaultLevel() int {
	return int(C.zxc_default_level())
}

// ============================================================================
// Error Handling
// ============================================================================

// Error represents a ZXC library error.
type Error struct {
	Code int
	Name string
}

func (e *Error) Error() string {
	return fmt.Sprintf("zxc: %s (code %d)", e.Name, e.Code)
}

// Sentinel errors for each ZXC error code.
var (
	ErrMemory       = &Error{Code: int(C.ZXC_ERROR_MEMORY), Name: "memory allocation failed"}
	ErrDstTooSmall  = &Error{Code: int(C.ZXC_ERROR_DST_TOO_SMALL), Name: "destination buffer too small"}
	ErrSrcTooSmall  = &Error{Code: int(C.ZXC_ERROR_SRC_TOO_SMALL), Name: "source buffer too small"}
	ErrBadMagic     = &Error{Code: int(C.ZXC_ERROR_BAD_MAGIC), Name: "invalid magic word"}
	ErrBadVersion   = &Error{Code: int(C.ZXC_ERROR_BAD_VERSION), Name: "unsupported format version"}
	ErrBadHeader    = &Error{Code: int(C.ZXC_ERROR_BAD_HEADER), Name: "corrupted header"}
	ErrBadChecksum  = &Error{Code: int(C.ZXC_ERROR_BAD_CHECKSUM), Name: "checksum verification failed"}
	ErrCorruptData  = &Error{Code: int(C.ZXC_ERROR_CORRUPT_DATA), Name: "corrupted compressed data"}
	ErrBadOffset    = &Error{Code: int(C.ZXC_ERROR_BAD_OFFSET), Name: "invalid match offset"}
	ErrOverflow     = &Error{Code: int(C.ZXC_ERROR_OVERFLOW), Name: "buffer overflow detected"}
	ErrIO           = &Error{Code: int(C.ZXC_ERROR_IO), Name: "I/O error"}
	ErrNullInput    = &Error{Code: int(C.ZXC_ERROR_NULL_INPUT), Name: "null input pointer"}
	ErrBadBlockType = &Error{Code: int(C.ZXC_ERROR_BAD_BLOCK_TYPE), Name: "unknown block type"}
	ErrBadBlockSize = &Error{Code: int(C.ZXC_ERROR_BAD_BLOCK_SIZE), Name: "invalid block size"}
	ErrInvalidData  = errors.New("zxc: invalid compressed data")
)

// errorFromCode converts a negative C error code to a Go error.
func errorFromCode(code C.int64_t) error {
	switch int(code) {
	case int(C.ZXC_ERROR_MEMORY):
		return ErrMemory
	case int(C.ZXC_ERROR_DST_TOO_SMALL):
		return ErrDstTooSmall
	case int(C.ZXC_ERROR_SRC_TOO_SMALL):
		return ErrSrcTooSmall
	case int(C.ZXC_ERROR_BAD_MAGIC):
		return ErrBadMagic
	case int(C.ZXC_ERROR_BAD_VERSION):
		return ErrBadVersion
	case int(C.ZXC_ERROR_BAD_HEADER):
		return ErrBadHeader
	case int(C.ZXC_ERROR_BAD_CHECKSUM):
		return ErrBadChecksum
	case int(C.ZXC_ERROR_CORRUPT_DATA):
		return ErrCorruptData
	case int(C.ZXC_ERROR_BAD_OFFSET):
		return ErrBadOffset
	case int(C.ZXC_ERROR_OVERFLOW):
		return ErrOverflow
	case int(C.ZXC_ERROR_IO):
		return ErrIO
	case int(C.ZXC_ERROR_NULL_INPUT):
		return ErrNullInput
	case int(C.ZXC_ERROR_BAD_BLOCK_TYPE):
		return ErrBadBlockType
	case int(C.ZXC_ERROR_BAD_BLOCK_SIZE):
		return ErrBadBlockSize
	default:
		return fmt.Errorf("zxc: unknown error (code %d)", int(code))
	}
}

// ============================================================================
// Options (functional options pattern)
// ============================================================================

// options holds all configurable parameters.
type options struct {
	level    Level
	checksum bool
	seekable bool
	threads  int
}

func defaultOptions() options {
	return options{
		level:    LevelDefault,
		checksum: false,
		threads:  0, // auto-detect
	}
}

// Option configures compression or decompression.
type Option func(*options)

// WithLevel sets the compression level.
func WithLevel(l Level) Option {
	return func(o *options) { o.level = l }
}

// WithChecksum enables checksum computation and verification.
func WithChecksum(enabled bool) Option {
	return func(o *options) { o.checksum = enabled }
}

// WithThreads sets the number of worker threads for streaming operations.
// A value of 0 means auto-detect the CPU core count.
func WithThreads(n int) Option {
	return func(o *options) { o.threads = n }
}

// WithSeekable enables seek table generation for random-access decompression.
func WithSeekable(enabled bool) Option {
	return func(o *options) { o.seekable = enabled }
}

func applyOptions(opts []Option) options {
	o := defaultOptions()
	for _, fn := range opts {
		fn(&o)
	}
	return o
}

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
	if len(data) == 0 {
		return nil, ErrSrcTooSmall
	}

	o := applyOptions(opts)
	bound := CompressBound(len(data))
	dst := make([]byte, bound)

	var copts C.zxc_compress_opts_t
	copts.level = C.int(o.level)
	if o.checksum {
		copts.checksum_enabled = 1
	}

	written := C.zxc_compress(
		unsafe.Pointer(&data[0]),
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

	size, err := DecompressedSize(data)
	if err != nil {
		return nil, err
	}
	if size == 0 {
		return []byte{}, nil
	}

	o := applyOptions(opts)
	dst := make([]byte, size)

	var dopts C.zxc_decompress_opts_t
	if o.checksum {
		dopts.checksum_enabled = 1
	}

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
	defer C.fclose(cOut)

	var copts C.zxc_compress_opts_t
	copts.n_threads = C.int(o.threads)
	copts.level = C.int(o.level)
	if o.checksum {
		copts.checksum_enabled = 1
	}
	if o.seekable {
		copts.seekable = 1
	}

	result := C.zxc_stream_compress(cIn, cOut, &copts)
	if result < 0 {
		return 0, errorFromCode(result)
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
	defer C.fclose(cOut)

	var dopts C.zxc_decompress_opts_t
	dopts.n_threads = C.int(o.threads)
	if o.checksum {
		dopts.checksum_enabled = 1
	}

	result := C.zxc_stream_decompress(cIn, cOut, &dopts)
	if result < 0 {
		return 0, errorFromCode(result)
	}
	return int64(result), nil
}

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
// by [Cctx.DecompressBlock] for a block of uncompressedSize bytes.
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
// context reserves when compressing a single block of srcSize bytes via
// [Cctx.CompressBlock].
//
// The estimate covers all per-chunk working buffers (chain table, literals,
// sequence/token/offset/extras buffers) plus the fixed hash tables and
// cache-line alignment padding. It scales roughly linearly with srcSize and
// is intended for integrators that need an accurate memory budget.
//
// Returns 0 if srcSize is 0 or negative.
func EstimateCctxSize(srcSize int) uint64 {
	if srcSize <= 0 {
		return 0
	}
	return uint64(C.zxc_estimate_cctx_size(C.size_t(srcSize)))
}

// Cctx is a reusable compression context for the Block API.
//
// It wraps an opaque C handle that is freed by [Cctx.Close]. Create with
// [NewCctx] and always defer a call to Close to release the native memory.
type Cctx struct {
	ptr *C.zxc_cctx
}

// NewCctx creates a new compression context.
//
// Options [WithLevel], [WithChecksum] and [WithBlockSize] are supported at
// creation time to pre-allocate internal buffers; otherwise allocation is
// deferred to the first call to [Cctx.CompressBlock].
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
	return &Cctx{ptr: ptr}, nil
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

	o := applyOptions(opts)
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
