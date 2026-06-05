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

// cDictCopy copies a dictionary into C-owned memory and points opts at it.
// The push streams retain the dict pointer for their whole lifetime, so it
// must reference native (not Go) memory. Returns the C buffer to free later,
// or nil when no dictionary is configured.
func cCompressDictCopy(copts *C.zxc_compress_opts_t, o options) unsafe.Pointer {
	if len(o.dict) == 0 {
		return nil
	}
	buf := C.malloc(C.size_t(len(o.dict)))
	if buf == nil {
		return nil
	}
	C.memcpy(buf, unsafe.Pointer(&o.dict[0]), C.size_t(len(o.dict)))
	copts.dict = buf
	copts.dict_size = C.size_t(len(o.dict))
	return buf
}

func cDecompressDictCopy(dopts *C.zxc_decompress_opts_t, o options) unsafe.Pointer {
	if len(o.dict) == 0 {
		return nil
	}
	buf := C.malloc(C.size_t(len(o.dict)))
	if buf == nil {
		return nil
	}
	C.memcpy(buf, unsafe.Pointer(&o.dict[0]), C.size_t(len(o.dict)))
	dopts.dict = buf
	dopts.dict_size = C.size_t(len(o.dict))
	return buf
}

// ============================================================================
// Push Streaming API (single-threaded, caller-driven)
// ============================================================================

// CStream is a push-based, single-threaded compression stream — the Go
// counterpart of the C [zxc_cstream]. Use it to integrate ZXC into event
// loops, callback-driven libraries, or non-blocking network protocols where
// the [CompressFile] FILE*-based pipeline is not appropriate.
//
// CStream is not safe for concurrent use; one goroutine per stream.
type CStream struct {
	ptr  *C.zxc_cstream
	dbuf unsafe.Pointer // C-owned dictionary copy retained for the stream's life
}

// NewCStream creates a push compression stream.
//
// Honoured options: [WithLevel], [WithChecksum], [WithDict]. [WithThreads] and
// [WithSeekable] are ignored (the push API is single-threaded and never
// emits a seek table).
func NewCStream(opts ...Option) (*CStream, error) {
	o := applyOptions(opts)
	var copts C.zxc_compress_opts_t
	copts.level = C.int(o.level)
	if o.checksum {
		copts.checksum_enabled = 1
	}
	dbuf := cCompressDictCopy(&copts, o)
	if dbuf == nil && len(o.dict) > 0 {
		return nil, ErrMemory
	}
	ptr := C.zxc_cstream_create(&copts)
	if ptr == nil {
		if dbuf != nil {
			C.free(dbuf)
		}
		return nil, ErrMemory
	}
	return &CStream{ptr: ptr, dbuf: dbuf}, nil
}

// Close releases the stream and all internal buffers. Safe to call multiple
// times.
func (c *CStream) Close() error {
	if c == nil || c.ptr == nil {
		return nil
	}
	C.zxc_cstream_free(c.ptr)
	c.ptr = nil
	if c.dbuf != nil {
		C.free(c.dbuf)
		c.dbuf = nil
	}
	return nil
}

// InSize returns the suggested input chunk size for best throughput.
func (c *CStream) InSize() int {
	if c == nil || c.ptr == nil {
		return 0
	}
	return int(C.zxc_cstream_in_size(c.ptr))
}

// OutSize returns the suggested output chunk size to never trigger a partial
// drain.
func (c *CStream) OutSize() int {
	if c == nil || c.ptr == nil {
		return 0
	}
	return int(C.zxc_cstream_out_size(c.ptr))
}

// Compress pushes bytes from in into the stream and writes compressed bytes
// to out. Returns:
//
//   - consumed: bytes read from in.
//   - produced: bytes written into out.
//   - pending:  bytes still staged inside the stream (drain out and call
//     again with the same or new in to continue).
//
// A return of (consumed, produced, 0, nil) means in was fully consumed and
// no compressed bytes remain pending. Calling with an empty in is valid
// (drain-only mode).
func (c *CStream) Compress(out, in []byte) (consumed, produced int, pending int64, err error) {
	if c == nil || c.ptr == nil {
		return 0, 0, 0, ErrNullInput
	}

	var pinner runtime.Pinner
	defer pinner.Unpin()

	var inBuf C.zxc_inbuf_t
	if len(in) > 0 {
		pinner.Pin(&in[0])
		inBuf.src = unsafe.Pointer(&in[0])
	}
	inBuf.size = C.size_t(len(in))

	var outBuf C.zxc_outbuf_t
	if len(out) > 0 {
		pinner.Pin(&out[0])
		outBuf.dst = unsafe.Pointer(&out[0])
	}
	outBuf.size = C.size_t(len(out))

	r := C.zxc_cstream_compress(c.ptr, &outBuf, &inBuf)
	if r < 0 {
		return 0, 0, 0, errorFromCode(r)
	}
	return int(inBuf.pos), int(outBuf.pos), int64(r), nil
}

// End finalises the stream by flushing the residual block, then writing the
// EOF block and the file footer to out. Call repeatedly while pending > 0,
// draining out between calls.
//
// After End returns (produced, 0, nil), the stream is in DONE state; further
// calls return ErrNullInput. Always call [CStream.Close] to release the
// native resources.
func (c *CStream) End(out []byte) (produced int, pending int64, err error) {
	if c == nil || c.ptr == nil {
		return 0, 0, ErrNullInput
	}

	var pinner runtime.Pinner
	defer pinner.Unpin()

	var outBuf C.zxc_outbuf_t
	if len(out) > 0 {
		pinner.Pin(&out[0])
		outBuf.dst = unsafe.Pointer(&out[0])
	}
	outBuf.size = C.size_t(len(out))

	r := C.zxc_cstream_end(c.ptr, &outBuf)
	if r < 0 {
		return 0, 0, errorFromCode(r)
	}
	return int(outBuf.pos), int64(r), nil
}

// DStream is a push-based, single-threaded decompression stream — the Go
// counterpart of the C [zxc_dstream].
type DStream struct {
	ptr  *C.zxc_dstream
	dbuf unsafe.Pointer // C-owned dictionary copy retained for the stream's life
}

// NewDStream creates a push decompression stream.
//
// Honoured options: [WithChecksum], [WithDict]. [WithThreads] is ignored.
func NewDStream(opts ...Option) (*DStream, error) {
	o := applyOptions(opts)
	var dopts C.zxc_decompress_opts_t
	if o.checksum {
		dopts.checksum_enabled = 1
	}
	dbuf := cDecompressDictCopy(&dopts, o)
	if dbuf == nil && len(o.dict) > 0 {
		return nil, ErrMemory
	}
	ptr := C.zxc_dstream_create(&dopts)
	if ptr == nil {
		if dbuf != nil {
			C.free(dbuf)
		}
		return nil, ErrMemory
	}
	return &DStream{ptr: ptr, dbuf: dbuf}, nil
}

// Close releases the stream and all internal buffers. Safe to call multiple
// times.
func (d *DStream) Close() error {
	if d == nil || d.ptr == nil {
		return nil
	}
	C.zxc_dstream_free(d.ptr)
	d.ptr = nil
	if d.dbuf != nil {
		C.free(d.dbuf)
		d.dbuf = nil
	}
	return nil
}

// InSize returns the suggested input chunk size for the decompressor.
func (d *DStream) InSize() int {
	if d == nil || d.ptr == nil {
		return 0
	}
	return int(C.zxc_dstream_in_size(d.ptr))
}

// OutSize returns the suggested output chunk size for the decompressor.
func (d *DStream) OutSize() int {
	if d == nil || d.ptr == nil {
		return 0
	}
	return int(C.zxc_dstream_out_size(d.ptr))
}

// Finished reports whether the decoder has reached and validated the file
// footer. Useful to detect truncated streams: if the input source is
// drained and Finished returns false, the stream ended prematurely.
func (d *DStream) Finished() bool {
	if d == nil || d.ptr == nil {
		return false
	}
	return C.zxc_dstream_finished(d.ptr) != 0
}

// Decompress pushes compressed bytes from in into the stream and writes
// decompressed bytes to out. Returns:
//
//   - consumed: bytes read from in.
//   - produced: bytes written into out.
//
// A return of (0, 0, nil) when in is non-empty means no progress could be
// made: either the parser is waiting for more input (feed more), or the
// stream has reached DONE state (any trailing bytes in in are ignored).
// Use [DStream.Finished] to disambiguate.
func (d *DStream) Decompress(out, in []byte) (consumed, produced int, err error) {
	if d == nil || d.ptr == nil {
		return 0, 0, ErrNullInput
	}

	var pinner runtime.Pinner
	defer pinner.Unpin()

	var inBuf C.zxc_inbuf_t
	if len(in) > 0 {
		pinner.Pin(&in[0])
		inBuf.src = unsafe.Pointer(&in[0])
	}
	inBuf.size = C.size_t(len(in))

	var outBuf C.zxc_outbuf_t
	if len(out) > 0 {
		pinner.Pin(&out[0])
		outBuf.dst = unsafe.Pointer(&out[0])
	}
	outBuf.size = C.size_t(len(out))

	r := C.zxc_dstream_decompress(d.ptr, &outBuf, &inBuf)
	if r < 0 {
		return 0, 0, errorFromCode(r)
	}
	return int(inBuf.pos), int(outBuf.pos), nil
}
