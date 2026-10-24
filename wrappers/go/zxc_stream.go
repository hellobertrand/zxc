// SPDX-License-Identifier: BSD-3-Clause
// ZXC - High-performance lossless compression
//
// Copyright (c) Bertrand Lebonnois and contributors.

package zxc

/*
#include <stdlib.h>
#include "zxc.h"
*/
import "C"
import (
	"runtime"
	"unsafe"
)

// ============================================================================
// Push Streaming API (single-threaded, caller-driven)
// ============================================================================

// CStream is a push-based, single-threaded compression stream — the Go
// counterpart of the C [zxc_cstream]. Use it to integrate ZXC into event
// loops, callback-driven libraries, or non-blocking network protocols where
// the [CompressFile] FILE*-based pipeline is not appropriate.
//
// CStream is not safe for concurrent use; one goroutine per stream. Always
// defer a call to [CStream.Close]; a finalizer frees a dropped stream only
// once the garbage collector runs.
type CStream struct {
	ptr *C.zxc_cstream
}

// NewCStream creates a push compression stream.
//
// Honoured options: [WithLevel], [WithChecksum]. [WithThreads] and
// [WithSeekable] are ignored (the push API is single-threaded and never
// emits a seek table). [WithDict]/[WithDictionary] return
// [ErrDictUnsupported]: the push-stream format carries no dictionary ID, so
// dictionary compression would produce undecodable archives.
func NewCStream(opts ...Option) (*CStream, error) {
	o := applyOptions(opts)
	if len(o.dict) > 0 || len(o.dictHuf) > 0 {
		return nil, ErrDictUnsupported
	}
	// The stream copies its parameters from a configured context at creation.
	var pinner runtime.Pinner
	defer pinner.Unpin()
	cctx, err := createCctx(o, &pinner)
	if err != nil {
		return nil, err
	}
	defer C.zxc_free_cctx(cctx)
	ptr := C.zxc_cstream_create(cctx)
	if ptr == nil {
		return nil, ErrMemory
	}
	c := &CStream{ptr: ptr}
	runtime.SetFinalizer(c, (*CStream).Close)
	return c, nil
}

// Close releases the stream and all internal buffers. Safe to call multiple
// times.
func (c *CStream) Close() error {
	if c == nil || c.ptr == nil {
		return nil
	}
	C.zxc_cstream_free(c.ptr)
	c.ptr = nil
	runtime.SetFinalizer(c, nil)
	return nil
}

// InSize returns the suggested input chunk size for best throughput.
func (c *CStream) InSize() int {
	if c == nil || c.ptr == nil {
		return 0
	}
	n := int(C.zxc_cstream_in_size(c.ptr))
	runtime.KeepAlive(c)
	return n
}

// OutSize returns the suggested output chunk size to never trigger a partial
// drain.
func (c *CStream) OutSize() int {
	if c == nil || c.ptr == nil {
		return 0
	}
	n := int(C.zxc_cstream_out_size(c.ptr))
	runtime.KeepAlive(c)
	return n
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
	runtime.KeepAlive(c)
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
	runtime.KeepAlive(c)
	if r < 0 {
		return 0, 0, errorFromCode(r)
	}
	return int(outBuf.pos), int64(r), nil
}

// DStream is a push-based, single-threaded decompression stream — the Go
// counterpart of the C [zxc_dstream]. Always defer a call to
// [DStream.Close]; as with [CStream], a finalizer is only a safety net.
type DStream struct {
	ptr *C.zxc_dstream
}

// NewDStream creates a push decompression stream.
//
// Honoured options: [WithChecksum]. [WithThreads] is ignored.
// [WithDict]/[WithDictionary] return [ErrDictUnsupported] (see [NewCStream]).
func NewDStream(opts ...Option) (*DStream, error) {
	o := applyOptions(opts)
	if len(o.dict) > 0 || len(o.dictHuf) > 0 {
		return nil, ErrDictUnsupported
	}
	var pinner runtime.Pinner
	defer pinner.Unpin()
	dctx, err := createDctx(o, &pinner)
	if err != nil {
		return nil, err
	}
	defer C.zxc_free_dctx(dctx)
	ptr := C.zxc_dstream_create(dctx)
	if ptr == nil {
		return nil, ErrMemory
	}
	d := &DStream{ptr: ptr}
	runtime.SetFinalizer(d, (*DStream).Close)
	return d, nil
}

// Close releases the stream and all internal buffers. Safe to call multiple
// times.
func (d *DStream) Close() error {
	if d == nil || d.ptr == nil {
		return nil
	}
	C.zxc_dstream_free(d.ptr)
	d.ptr = nil
	runtime.SetFinalizer(d, nil)
	return nil
}

// InSize returns the suggested input chunk size for the decompressor.
func (d *DStream) InSize() int {
	if d == nil || d.ptr == nil {
		return 0
	}
	n := int(C.zxc_dstream_in_size(d.ptr))
	runtime.KeepAlive(d)
	return n
}

// OutSize returns the suggested output chunk size for the decompressor.
func (d *DStream) OutSize() int {
	if d == nil || d.ptr == nil {
		return 0
	}
	n := int(C.zxc_dstream_out_size(d.ptr))
	runtime.KeepAlive(d)
	return n
}

// Finished reports whether the input so far ends on a validated footer; more
// input starts the next frame. Useful to detect truncated streams: if the
// input source is drained and Finished returns false, the stream ended
// prematurely.
func (d *DStream) Finished() bool {
	if d == nil || d.ptr == nil {
		return false
	}
	done := C.zxc_dstream_finished(d.ptr) != 0
	runtime.KeepAlive(d)
	return done
}

// Decompress pushes compressed bytes from in into the stream and writes
// decompressed bytes to out. Returns:
//
//   - consumed: bytes read from in.
//   - produced: bytes written into out.
//
// Each call stops at the end of a frame, consumed pointing just past it: a
// caller after one frame stops there once [DStream.Finished] is true. A later
// call with more input decodes the next frame (concatenated archives); input
// that does not open with the magic word is an error.
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
	runtime.KeepAlive(d)
	if r < 0 {
		return 0, 0, errorFromCode(r)
	}
	return int(inBuf.pos), int(outBuf.pos), nil
}
