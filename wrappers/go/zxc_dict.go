/*
ZXC - High-performance lossless compression

Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
SPDX-License-Identifier: BSD-3-Clause
*/

package zxc

/*
#include <stdlib.h>
#include <string.h>
#include "zxc.h"
*/
import "C"
import (
	"runtime"
	"unsafe"
)

// ============================================================================
// Dictionary API (pre-trained dictionaries)
// ============================================================================

// DictSizeMax is the maximum dictionary content size in bytes (65535).
const DictSizeMax = int(C.ZXC_DICT_SIZE_MAX)

// HufTableSize is the size in bytes of the shared literal Huffman table
// carried by a .zxd file (packed 4-bit code lengths for 256 symbols).
const HufTableSize = int(C.ZXC_HUF_TABLE_SIZE)

// setCompressDict points copts at the dictionary content from o when one is
// configured. The dict byte is pinned for the duration of the C call so that
// the Go pointer stored inside the (Go-allocated) opts struct satisfies the
// cgo pointer-passing rules; callers must keep pinner alive until the C call
// returns.
func setCompressDict(copts *C.zxc_compress_opts_t, o options, pinner *runtime.Pinner) {
	if len(o.dict) == 0 {
		return
	}
	pinner.Pin(&o.dict[0])
	copts.dict = unsafe.Pointer(&o.dict[0])
	copts.dict_size = C.size_t(len(o.dict))
	if len(o.dictHuf) > 0 {
		pinner.Pin(&o.dictHuf[0])
		copts.dict_huf = unsafe.Pointer(&o.dictHuf[0])
	}
}

// setDecompressDict mirrors setCompressDict for decompression options.
func setDecompressDict(dopts *C.zxc_decompress_opts_t, o options, pinner *runtime.Pinner) {
	if len(o.dict) == 0 {
		return
	}
	pinner.Pin(&o.dict[0])
	dopts.dict = unsafe.Pointer(&o.dict[0])
	dopts.dict_size = C.size_t(len(o.dict))
	if len(o.dictHuf) > 0 {
		pinner.Pin(&o.dictHuf[0])
		dopts.dict_huf = unsafe.Pointer(&o.dictHuf[0])
	}
}

// TrainDict trains a dictionary from a corpus of samples.
//
// It analyses the samples to select byte sequences that maximise LZ77 match
// coverage, returning raw dictionary content suitable for [WithDict],
// [DictSave], or [DictID]. maxSize caps the trained dictionary size; values
// <= 0 or greater than [DictSizeMax] are clamped to [DictSizeMax].
//
// At least one non-empty sample is required.
func TrainDict(samples [][]byte, maxSize int) ([]byte, error) {
	if len(samples) == 0 {
		return nil, ErrSrcTooSmall
	}
	if maxSize <= 0 || maxSize > DictSizeMax {
		maxSize = DictSizeMax
	}

	// cgo forbids passing a Go pointer to memory that itself contains Go
	// pointers, so the sample buffers, the pointer array, and the size array
	// are all built in C-allocated memory. Each sample is copied into C memory
	// for the duration of the call.
	n := len(samples)
	cptrs := C.malloc(C.size_t(n) * C.size_t(unsafe.Sizeof(uintptr(0))))
	if cptrs == nil {
		return nil, ErrMemory
	}
	defer C.free(cptrs)
	csizes := C.malloc(C.size_t(n) * C.size_t(unsafe.Sizeof(C.size_t(0))))
	if csizes == nil {
		return nil, ErrMemory
	}
	defer C.free(csizes)

	ptrSlice := unsafe.Slice((*unsafe.Pointer)(cptrs), n)
	sizeSlice := unsafe.Slice((*C.size_t)(csizes), n)
	// Free each copied sample buffer on return.
	defer func() {
		for _, p := range ptrSlice {
			if p != nil {
				C.free(p)
			}
		}
	}()

	for i, s := range samples {
		sizeSlice[i] = C.size_t(len(s))
		if len(s) == 0 {
			// Allocate a 1-byte placeholder so the pointer is non-NULL.
			ptrSlice[i] = C.malloc(1)
			continue
		}
		buf := C.malloc(C.size_t(len(s)))
		if buf == nil {
			return nil, ErrMemory
		}
		C.memcpy(buf, unsafe.Pointer(&s[0]), C.size_t(len(s)))
		ptrSlice[i] = buf
	}

	dict := make([]byte, DictSizeMax)
	written := C.zxc_train_dict(
		(*unsafe.Pointer)(cptrs),
		(*C.size_t)(csizes),
		C.size_t(n),
		unsafe.Pointer(&dict[0]),
		C.size_t(maxSize),
	)
	if written < 0 {
		return nil, errorFromCode(written)
	}
	if written == 0 {
		return nil, ErrInvalidData
	}
	return dict[:int(written)], nil
}

// DictID computes the deterministic 32-bit ID of raw dictionary content.
// Returns 0 for empty content.
func DictID(content []byte) uint32 {
	if len(content) == 0 {
		return 0
	}
	return uint32(C.zxc_dict_id(unsafe.Pointer(&content[0]), C.size_t(len(content)), nil))
}

// GetDictID returns the dictionary ID recorded in a compressed .zxc archive
// header, or 0 if the archive was not compressed with a dictionary (or is too
// small / invalid).
func GetDictID(archive []byte) uint32 {
	if len(archive) == 0 {
		return 0
	}
	return uint32(C.zxc_get_dict_id(unsafe.Pointer(&archive[0]), C.size_t(len(archive))))
}

// DictGetID returns the dictionary ID stored in a serialized .zxd file buffer,
// or 0 if the buffer is not a valid .zxd file.
func DictGetID(zxd []byte) uint32 {
	if len(zxd) == 0 {
		return 0
	}
	return uint32(C.zxc_dict_get_id(unsafe.Pointer(&zxd[0]), C.size_t(len(zxd))))
}

// DictSave serializes dictionary content and its shared literal Huffman
// table ([HufTableSize] bytes, from [TrainDictHuf]) into the .zxd file
// format. The stored dictionary ID covers both content and table.
func DictSave(content, hufLengths []byte) ([]byte, error) {
	if len(content) == 0 {
		return nil, ErrSrcTooSmall
	}
	if len(hufLengths) != HufTableSize {
		return nil, ErrInvalidData
	}
	bound := uint64(C.zxc_dict_save_bound(C.size_t(len(content))))
	buf := make([]byte, bound)
	n := C.zxc_dict_save(
		unsafe.Pointer(&content[0]),
		C.size_t(len(content)),
		unsafe.Pointer(&hufLengths[0]),
		unsafe.Pointer(&buf[0]),
		C.size_t(bound),
	)
	if n < 0 {
		return nil, errorFromCode(n)
	}
	return buf[:int(n)], nil
}

// TrainDictHuf trains the shared literal Huffman table for an
// already-trained dictionary (see [TrainDict]). It compresses the samples
// with the dictionary and derives canonical code lengths from the real
// post-LZ literal distribution. The returned [HufTableSize]-byte table is
// required by [DictSave] and can be attached with [WithDictHuf].
func TrainDictHuf(samples [][]byte, dict []byte) ([]byte, error) {
	if len(samples) == 0 || len(dict) == 0 {
		return nil, ErrSrcTooSmall
	}

	n := len(samples)
	cptrs := C.malloc(C.size_t(n) * C.size_t(unsafe.Sizeof(uintptr(0))))
	if cptrs == nil {
		return nil, ErrMemory
	}
	defer C.free(cptrs)
	csizes := C.malloc(C.size_t(n) * C.size_t(unsafe.Sizeof(C.size_t(0))))
	if csizes == nil {
		return nil, ErrMemory
	}
	defer C.free(csizes)

	ptrSlice := unsafe.Slice((*unsafe.Pointer)(cptrs), n)
	sizeSlice := unsafe.Slice((*C.size_t)(csizes), n)
	defer func() {
		for _, p := range ptrSlice {
			if p != nil {
				C.free(p)
			}
		}
	}()

	for i, s := range samples {
		sizeSlice[i] = C.size_t(len(s))
		if len(s) == 0 {
			ptrSlice[i] = C.malloc(1)
			continue
		}
		buf := C.malloc(C.size_t(len(s)))
		if buf == nil {
			return nil, ErrMemory
		}
		C.memcpy(buf, unsafe.Pointer(&s[0]), C.size_t(len(s)))
		ptrSlice[i] = buf
	}

	huf := make([]byte, HufTableSize)
	rc := C.zxc_train_dict_huf(
		(*unsafe.Pointer)(cptrs),
		(*C.size_t)(csizes),
		C.size_t(n),
		unsafe.Pointer(&dict[0]),
		C.size_t(len(dict)),
		(*C.uint8_t)(unsafe.Pointer(&huf[0])),
	)
	if rc != C.ZXC_OK {
		return nil, errorFromCode(C.int64_t(rc))
	}
	return huf, nil
}

// DictHuf returns a copy of the shared literal Huffman table stored in a
// .zxd file buffer, or nil if the buffer is not a valid .zxd file.
func DictHuf(zxd []byte) []byte {
	if len(zxd) == 0 {
		return nil
	}
	p := C.zxc_dict_huf(unsafe.Pointer(&zxd[0]), C.size_t(len(zxd)))
	if p == nil {
		return nil
	}
	return C.GoBytes(p, C.int(HufTableSize))
}

// DictLoad validates a .zxd file buffer and returns a copy of its dictionary
// content along with the dictionary ID. Prefer [LoadDictionary] for the full
// (content, table, id) bundle.
func DictLoad(zxd []byte) (content []byte, id uint32, err error) {
	d, err := LoadDictionary(zxd)
	if err != nil {
		return nil, 0, err
	}
	return d.Content(), d.ID(), nil
}

// Dictionary bundles a trained dictionary's LZ-window content and its shared
// literal Huffman table, so callers never juggle the pair by hand.
//
// Create one with [TrainDictionary] (from samples) or [LoadDictionary] (from
// .zxd bytes); attach it with [WithDictionary] or [Seekable.SetDictionary].
type Dictionary struct {
	content []byte
	huf     []byte // HufTableSize bytes
	id      uint32
}

// TrainDictionary trains a complete dictionary (content + shared table) from
// a corpus of samples in one call.
func TrainDictionary(samples [][]byte) (*Dictionary, error) {
	if len(samples) == 0 {
		return nil, ErrSrcTooSmall
	}

	n := len(samples)
	cptrs := C.malloc(C.size_t(n) * C.size_t(unsafe.Sizeof(uintptr(0))))
	if cptrs == nil {
		return nil, ErrMemory
	}
	defer C.free(cptrs)
	csizes := C.malloc(C.size_t(n) * C.size_t(unsafe.Sizeof(C.size_t(0))))
	if csizes == nil {
		return nil, ErrMemory
	}
	defer C.free(csizes)

	ptrSlice := unsafe.Slice((*unsafe.Pointer)(cptrs), n)
	sizeSlice := unsafe.Slice((*C.size_t)(csizes), n)
	defer func() {
		for _, p := range ptrSlice {
			if p != nil {
				C.free(p)
			}
		}
	}()

	for i, s := range samples {
		sizeSlice[i] = C.size_t(len(s))
		if len(s) == 0 {
			ptrSlice[i] = C.malloc(1)
			continue
		}
		buf := C.malloc(C.size_t(len(s)))
		if buf == nil {
			return nil, ErrMemory
		}
		C.memcpy(buf, unsafe.Pointer(&s[0]), C.size_t(len(s)))
		ptrSlice[i] = buf
	}

	cap := uint64(C.zxc_dict_save_bound(C.size_t(DictSizeMax)))
	zxd := make([]byte, cap)
	written := C.zxc_dict_train(
		(*unsafe.Pointer)(cptrs),
		(*C.size_t)(csizes),
		C.size_t(n),
		unsafe.Pointer(&zxd[0]),
		C.size_t(cap),
	)
	if written <= 0 {
		if written < 0 {
			return nil, errorFromCode(written)
		}
		return nil, ErrInvalidData
	}
	return LoadDictionary(zxd[:int(written)])
}

// LoadDictionary parses .zxd bytes into an owned Dictionary.
func LoadDictionary(zxd []byte) (*Dictionary, error) {
	if len(zxd) == 0 {
		return nil, ErrSrcTooSmall
	}
	var contentPtr unsafe.Pointer
	var contentSize C.size_t
	var hufPtr unsafe.Pointer
	var dictID C.uint32_t
	rc := C.zxc_dict_load(
		unsafe.Pointer(&zxd[0]),
		C.size_t(len(zxd)),
		&contentPtr,
		&contentSize,
		&hufPtr,
		&dictID,
	)
	if rc < 0 {
		return nil, errorFromCode(C.int64_t(rc))
	}
	// The pointers alias into zxd (zero-copy); copy into Go-owned memory so
	// the result is independent of the input buffer's lifetime.
	return &Dictionary{
		content: C.GoBytes(contentPtr, C.int(contentSize)),
		huf:     C.GoBytes(hufPtr, C.int(HufTableSize)),
		id:      uint32(dictID),
	}, nil
}

// Save serializes the dictionary back to .zxd file bytes.
func (d *Dictionary) Save() ([]byte, error) {
	return DictSave(d.content, d.huf)
}

// ID returns the dictionary ID binding the (content, table) pair, as recorded
// in .zxd files and archive headers.
func (d *Dictionary) ID() uint32 { return d.id }

// Content returns the raw LZ-window content bytes.
func (d *Dictionary) Content() []byte { return d.content }

// Huf returns the 128-byte shared literal Huffman table.
func (d *Dictionary) Huf() []byte { return d.huf }
