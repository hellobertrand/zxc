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

	// LevelDensity provides maximum density: Huffman-coded literals on top
	// of LevelCompact plus a price-based optimal LZ77 parser. Slowest
	// compression, best ratio (level 6).
	LevelDensity Level = C.ZXC_LEVEL_DENSITY
)

// AllLevels returns all available compression levels from fastest to most
// compact.
func AllLevels() []Level {
	return []Level{LevelFastest, LevelFast, LevelDefault, LevelBalanced, LevelCompact, LevelDensity}
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

// MaxLevel returns the maximum supported compression level (currently 6).
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
