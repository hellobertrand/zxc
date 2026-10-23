// SPDX-License-Identifier: BSD-3-Clause
// ZXC - High-performance lossless compression
//
// Copyright (c) Bertrand Lebonnois and contributors.

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
	"runtime"
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

	// LevelCompact provides high density: storage, firmware and assets
	// (level 5).
	LevelCompact Level = C.ZXC_LEVEL_COMPACT

	// LevelDensity provides high density: Huffman-coded literals on top
	// of LevelCompact plus a price-based optimal LZ77 parser (level 6).
	LevelDensity Level = C.ZXC_LEVEL_DENSITY

	// LevelUltra provides maximum density: Huffman-coded literals and sequence
	// tokens with a deep parse. Slowest compression, best ratio (level 7).
	LevelUltra Level = C.ZXC_LEVEL_ULTRA
)

// AllLevels returns all available compression levels from fastest to most
// compact.
func AllLevels() []Level {
	return []Level{LevelFastest, LevelFast, LevelDefault, LevelBalanced, LevelCompact, LevelDensity, LevelUltra}
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
// libzxc.
func LibraryVersion() string {
	return C.GoString(C.zxc_version_string())
}

// MinLevel returns the minimum supported compression level (currently 1).
func MinLevel() int {
	return int(C.zxc_min_level())
}

// MaxLevel returns the maximum supported compression level (currently 7).
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
	ErrDictRequired = &Error{Code: int(C.ZXC_ERROR_DICT_REQUIRED), Name: "archive requires a dictionary"}
	ErrDictMismatch = &Error{Code: int(C.ZXC_ERROR_DICT_MISMATCH), Name: "dictionary ID mismatch"}
	ErrDictTooLarge = &Error{Code: int(C.ZXC_ERROR_DICT_TOO_LARGE), Name: "dictionary exceeds maximum size"}
	ErrBadLevel     = &Error{Code: int(C.ZXC_ERROR_BAD_LEVEL), Name: "compression level out of range"}
	ErrBadParam     = &Error{Code: int(C.ZXC_ERROR_BAD_PARAM), Name: "parameter out of range"}
	ErrInvalidData  = errors.New("zxc: invalid compressed data")

	// ErrBadHufTable is returned when a shared literal Huffman table does not
	// have the required [HufTableSize] length.
	ErrBadHufTable = errors.New("zxc: shared Huffman table must be HufTableSize bytes")

	// ErrDictUnsupported is returned by the push streaming API when dictionary
	// options are supplied: the push-stream format carries no dictionary ID, so
	// dictionary compression would produce undecodable archives. Static C
	// contexts report the same condition (ZXC_ERROR_DICT_UNSUPPORTED).
	ErrDictUnsupported = errors.New("zxc: dictionaries are not supported by the push streaming API")

	// ErrSeekableInUse is returned when ReadAt calls Close, SetDict or a nested
	// DecompressRange on its [Seekable], which would free or share what the
	// running call uses.
	ErrSeekableInUse = errors.New("zxc: seekable handle in use by the running call")
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
	case int(C.ZXC_ERROR_DICT_REQUIRED):
		return ErrDictRequired
	case int(C.ZXC_ERROR_DICT_MISMATCH):
		return ErrDictMismatch
	case int(C.ZXC_ERROR_DICT_TOO_LARGE):
		return ErrDictTooLarge
	case int(C.ZXC_ERROR_BAD_LEVEL):
		return ErrBadLevel
	case int(C.ZXC_ERROR_DICT_UNSUPPORTED):
		return ErrDictUnsupported
	case int(C.ZXC_ERROR_BAD_PARAM):
		return ErrBadParam
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
	dict     []byte
	dictHuf  []byte

	// *Set records whether the caller supplied the option explicitly, so a
	// per-call option can fall back to the context's value, and an empty one
	// can clear it (see Cctx).
	levelSet    bool
	checksumSet bool
	dictSet     bool
	dictHufSet  bool
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

// WithLevel sets the compression level. A level outside [MinLevel, MaxLevel]
// is refused with [ErrBadLevel]; 0 selects [LevelDefault].
func WithLevel(l Level) Option {
	return func(o *options) {
		o.level = l
		o.levelSet = true
	}
}

// WithChecksum enables checksum computation and verification.
func WithChecksum(enabled bool) Option {
	return func(o *options) {
		o.checksum = enabled
		o.checksumSet = true
	}
}

// WithThreads sets the number of worker threads for streaming operations.
// A value of 0 means auto-detect the CPU core count; a negative one, or more
// than the library's maximum (512), is refused with [ErrBadParam].
func WithThreads(n int) Option {
	return func(o *options) { o.threads = n }
}

// WithSeekable enables seek table generation for random-access decompression.
func WithSeekable(enabled bool) Option {
	return func(o *options) { o.seekable = enabled }
}

// WithDict sets a pre-trained dictionary used for compression or
// decompression. The same dictionary content must be supplied to both
// Compress and Decompress (the dictionary ID stored in the archive header is
// validated against it). An empty slice clears the dictionary.
//
// Train a dictionary with [TrainDict] or load one from a .zxd file with
// [DictLoad]. The content size must not exceed [DictSizeMax].
func WithDict(dict []byte) Option {
	return func(o *options) { o.dict, o.dictSet = dict, true }
}

// WithDictionary attaches a [Dictionary] (content + shared table) in one call.
func WithDictionary(d *Dictionary) Option {
	return func(o *options) {
		o.dict, o.dictSet = d.Content(), true
		o.dictHuf, o.dictHufSet = d.Huf(), true
	}
}

// WithDictHuf sets the dictionary's shared literal Huffman table
// ([HufTableSize] bytes, from [TrainDictHuf] or [DictHuf]). It is ignored
// without [WithDict]. The archive's dictionary ID binds the (dict, table)
// pair, so the same table must be supplied at decompression time.
func WithDictHuf(huf []byte) Option {
	return func(o *options) { o.dictHuf, o.dictHufSet = huf, true }
}

func applyOptions(opts []Option) options {
	o := defaultOptions()
	for _, fn := range opts {
		fn(&o)
	}
	return o
}

// ============================================================================
// Native context configuration
// ============================================================================
//
// The C library takes every parameter on a context (zxc_cctx_set_param,
// zxc_cctx_set_dict and the dctx counterparts). The helpers below translate
// an options value into those calls, so each entry point configures a
// context, runs, and frees it; the Go API keeps its functional options.

// boolParam maps a flag to the 0/1 a context parameter takes.
func boolParam(b bool) int64 {
	if b {
		return 1
	}
	return 0
}

// setCParam sets one compression parameter, mapping a refusal to its error.
func setCParam(ptr *C.zxc_cctx, param C.zxc_cparam_t, v int64) error {
	if rc := C.zxc_cctx_set_param(ptr, param, C.int64_t(v)); rc != C.ZXC_OK {
		return errorFromCode(C.int64_t(rc))
	}
	return nil
}

// setDParam sets one decompression parameter, mapping a refusal to its error.
func setDParam(ptr *C.zxc_dctx, param C.zxc_dparam_t, v int64) error {
	if rc := C.zxc_dctx_set_param(ptr, param, C.int64_t(v)); rc != C.ZXC_OK {
		return errorFromCode(C.int64_t(rc))
	}
	return nil
}

// configureCctx sets the level, checksum, seek table and thread count of o
// on ptr, then attaches its dictionary (see [attachCompressDict] for the
// pinning contract).
func configureCctx(ptr *C.zxc_cctx, o options, pinner *runtime.Pinner) error {
	if err := setCParam(ptr, C.ZXC_C_LEVEL, int64(o.level)); err != nil {
		return err
	}
	if err := setCParam(ptr, C.ZXC_C_CHECKSUM, boolParam(o.checksum)); err != nil {
		return err
	}
	if err := setCParam(ptr, C.ZXC_C_SEEKABLE, boolParam(o.seekable)); err != nil {
		return err
	}
	if err := setCParam(ptr, C.ZXC_C_THREADS, int64(o.threads)); err != nil {
		return err
	}
	return attachCompressDict(ptr, o, pinner)
}

// configureDctx sets the checksum and thread count of o on ptr, then attaches
// its dictionary.
func configureDctx(ptr *C.zxc_dctx, o options, pinner *runtime.Pinner) error {
	if err := setDParam(ptr, C.ZXC_D_CHECKSUM, boolParam(o.checksum)); err != nil {
		return err
	}
	if err := setDParam(ptr, C.ZXC_D_THREADS, int64(o.threads)); err != nil {
		return err
	}
	return attachDecompressDict(ptr, o, pinner)
}

// createCctx returns a native compression context configured from o. The
// caller frees it with C.zxc_free_cctx before pinner.Unpin: the context holds
// the dictionary's address until then.
func createCctx(o options, pinner *runtime.Pinner) (*C.zxc_cctx, error) {
	ptr := C.zxc_create_cctx()
	if ptr == nil {
		return nil, ErrMemory
	}
	if err := configureCctx(ptr, o, pinner); err != nil {
		C.zxc_free_cctx(ptr)
		return nil, err
	}
	return ptr, nil
}

// createDctx is [createCctx] for a decompression context.
func createDctx(o options, pinner *runtime.Pinner) (*C.zxc_dctx, error) {
	ptr := C.zxc_create_dctx()
	if ptr == nil {
		return nil, ErrMemory
	}
	if err := configureDctx(ptr, o, pinner); err != nil {
		C.zxc_free_dctx(ptr)
		return nil, err
	}
	return ptr, nil
}
