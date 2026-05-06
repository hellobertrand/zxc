/*
ZXC - High-performance lossless compression

Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
SPDX-License-Identifier: BSD-3-Clause
*/

package zxc

import (
	"io"
)

// ============================================================================
// io.Reader / io.Writer adapters over the push streaming API
// ============================================================================
//
// These wrappers turn a [CStream] / [DStream] pair into the standard Go
// streaming interfaces (io.Reader, io.Writer, io.Closer) so ZXC can be plugged
// into pipelines that expect them — notably OCI-style content stores, tar
// pipelines, HTTP transports, etc.

// Magic word identifying a ZXC file frame: little-endian 0x9CB02EF5.
var zxcMagicLE = [4]byte{0xF5, 0x2E, 0xB0, 0x9C}

// DetectZxc reports whether data starts with the ZXC file magic word.
//
// Useful for content-type sniffing in containers / object stores that need to
// decide which decoder to dispatch (e.g. OCI media-type negotiation). The
// check is cheap and side-effect free; it does not validate the rest of the
// header or the footer.
func DetectZxc(data []byte) bool {
	if len(data) < 4 {
		return false
	}
	return data[0] == zxcMagicLE[0] &&
		data[1] == zxcMagicLE[1] &&
		data[2] == zxcMagicLE[2] &&
		data[3] == zxcMagicLE[3]
}

// ----------------------------------------------------------------------------
// Writer
// ----------------------------------------------------------------------------

// Writer is an [io.WriteCloser] that compresses bytes written to it and
// forwards the resulting ZXC frame to an underlying writer.
//
// The caller MUST call [Writer.Close] to flush the residual block, EOF marker
// and footer; closing the underlying writer is the caller's responsibility.
//
// Writer is not safe for concurrent use.
type Writer struct {
	dst    io.Writer
	cs     *CStream
	outBuf []byte
	err    error
}

// NewWriter returns a [Writer] that compresses into w using a single-threaded
// push pipeline.
//
// Honoured options: [WithLevel], [WithChecksum]. [WithThreads] and
// [WithSeekable] are ignored (push API is single-threaded).
func NewWriter(w io.Writer, opts ...Option) (*Writer, error) {
	cs, err := NewCStream(opts...)
	if err != nil {
		return nil, err
	}

	return &Writer{
		dst:    w,
		cs:     cs,
		outBuf: make([]byte, cs.OutSize()),
	}, nil
}

// Write compresses p and forwards the produced bytes to the underlying writer.
// Returns the number of bytes consumed from p (always len(p) on success).
func (w *Writer) Write(p []byte) (int, error) {
	if w.err != nil {
		return 0, w.err
	}
	if w.cs == nil {
		return 0, ErrNullInput
	}

	total := 0
	for len(p) > 0 {
		consumed, produced, pending, err := w.cs.Compress(w.outBuf, p)
		if err != nil {
			w.err = err
			return total, err
		}
		if produced > 0 {
			if _, werr := w.dst.Write(w.outBuf[:produced]); werr != nil {
				w.err = werr
				return total, werr
			}
		}
		total += consumed
		p = p[consumed:]

		// If the encoder made no input progress, drain pending output and
		// retry — this guarantees forward progress even when the caller
		// supplies a smaller-than-recommended output buffer.
		if consumed == 0 && produced == 0 && pending == 0 {
			break
		}
	}
	return total, nil
}

// Close flushes any buffered data, writes the EOF block + footer, and releases
// the underlying CStream. The wrapped io.Writer is NOT closed.
//
// Safe to call multiple times; subsequent calls are no-ops.
func (w *Writer) Close() error {
	if w.cs == nil {
		return nil
	}
	defer func() {
		_ = w.cs.Close()
		w.cs = nil
	}()
	if w.err != nil {
		return w.err
	}
	for {
		produced, pending, err := w.cs.End(w.outBuf)
		if err != nil {
			w.err = err
			return err
		}
		if produced > 0 {
			if _, werr := w.dst.Write(w.outBuf[:produced]); werr != nil {
				w.err = werr
				return werr
			}
		}
		if pending == 0 {
			return nil
		}
	}
}

// ----------------------------------------------------------------------------
// Reader
// ----------------------------------------------------------------------------

// Reader is an [io.ReadCloser] that decompresses a ZXC frame read from an
// underlying reader.
//
// Reader is not safe for concurrent use.
type Reader struct {
	src   io.Reader
	ds    *DStream
	inBuf []byte
	inPos int
	inLen int
	err   error
	eof   bool // src returned io.EOF
}

// NewReader returns a [Reader] that decompresses from r.
//
// Honoured options: [WithChecksum]. [WithThreads] is ignored.
func NewReader(r io.Reader, opts ...Option) (*Reader, error) {
	ds, err := NewDStream(opts...)
	if err != nil {
		return nil, err
	}
	return &Reader{
		src:   r,
		ds:    ds,
		inBuf: make([]byte, ds.InSize()),
	}, nil
}

// Read decompresses bytes into p. Returns io.EOF after the footer has been
// validated; returns io.ErrUnexpectedEOF if the underlying reader is drained
// before the footer is reached.
func (r *Reader) Read(p []byte) (int, error) {
	if r.err != nil {
		return 0, r.err
	}
	if r.ds == nil {
		return 0, ErrNullInput
	}
	if len(p) == 0 {
		return 0, nil
	}

	for {
		if r.ds.Finished() {
			r.err = io.EOF
			return 0, io.EOF
		}

		// Try to decompress whatever is currently buffered (or drain mode
		// when src is at EOF).
		if r.inPos < r.inLen || r.eof {
			consumed, produced, derr := r.ds.Decompress(p, r.inBuf[r.inPos:r.inLen])
			if derr != nil {
				r.err = derr
				return 0, derr
			}
			r.inPos += consumed
			if produced > 0 {
				return produced, nil
			}
			if consumed == 0 {
				// No forward progress with current input.
				if r.eof {
					if r.ds.Finished() {
						r.err = io.EOF
						return 0, io.EOF
					}
					r.err = io.ErrUnexpectedEOF
					return 0, io.ErrUnexpectedEOF
				}
				// fall through to refill
			} else {
				// Bytes were consumed but no output yet — loop and try again.
				continue
			}
		}

		// Refill: shift consumed bytes out, then read more.
		if r.inPos > 0 {
			r.inLen = copy(r.inBuf, r.inBuf[r.inPos:r.inLen])
			r.inPos = 0
		}
		n, rerr := r.src.Read(r.inBuf[r.inLen:])
		r.inLen += n
		if rerr == io.EOF {
			r.eof = true
		} else if rerr != nil {
			r.err = rerr
			return 0, rerr
		}
		if n == 0 && !r.eof {
			// Underlying reader returned (0, nil): treat as a benign retry by
			// returning to the caller with no output.
			return 0, nil
		}
	}
}

// Close releases the underlying [DStream]. The wrapped io.Reader is NOT
// closed. Safe to call multiple times.
func (r *Reader) Close() error {
	if r.ds == nil {
		return nil
	}
	err := r.ds.Close()
	r.ds = nil
	return err
}
