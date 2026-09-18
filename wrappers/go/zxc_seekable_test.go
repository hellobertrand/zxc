/*
ZXC - High-performance lossless compression

Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
SPDX-License-Identifier: BSD-3-Clause
*/

package zxc

import (
	"bytes"
	"errors"
	"io"
	"os"
	"path/filepath"
	"sync/atomic"
	"testing"
)

func buildSeekableArchive(t *testing.T, payload []byte) string {
	t.Helper()
	dir := t.TempDir()
	in := filepath.Join(dir, "input.bin")
	out := filepath.Join(dir, "archive.zxc")

	if err := os.WriteFile(in, payload, 0o644); err != nil {
		t.Fatalf("write input: %v", err)
	}
	if _, err := CompressFile(in, out, WithSeekable(true), WithChecksum(true)); err != nil {
		t.Fatalf("CompressFile(seekable): %v", err)
	}
	return out
}

// Asserting both verdicts proves the switch reaches C: on, ErrBadChecksum; off,
// the corrupted bytes come back with no error.
func TestSeekableSetChecksum(t *testing.T) {
	// Incompressible on purpose: a RAW block memcpys a flipped byte straight
	// through, so the decode succeeds and only the checksum objects. A
	// compressible payload would pin this test to today's encoder output.
	payload := make([]byte, 256*1024)
	rng := uint32(0x2E5B9A17)
	for i := range payload {
		rng = rng*1103515245 + 12345
		payload[i] = byte(rng >> 16)
	}
	path := buildSeekableArchive(t, payload)

	s, err := Open(path)
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	out := make([]byte, 512)
	for _, on := range []bool{true, false, true} {
		if err := s.SetChecksum(on); err != nil {
			t.Fatalf("SetChecksum(%v): %v", on, err)
		}
		if _, err := s.DecompressRange(out, 0, len(out)); err != nil {
			t.Fatalf("DecompressRange with checksum=%v: %v", on, err)
		}
	}
	s.Close()

	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read archive: %v", err)
	}
	if raw[16] != 0 {
		t.Fatalf("block 0 type = %d, want RAW; the LCG payload is no longer incompressible", raw[16])
	}
	// 16-byte file header, then block 0's header (8 bytes), then payload.
	raw[16+8+4] ^= 0xFF
	bad := filepath.Join(t.TempDir(), "corrupt.zxc")
	if err := os.WriteFile(bad, raw, 0o644); err != nil {
		t.Fatalf("write corrupt: %v", err)
	}

	verifying, err := Open(bad)
	if err != nil {
		t.Fatalf("Open(corrupt): %v", err)
	}
	if err := verifying.SetChecksum(true); err != nil {
		t.Fatalf("SetChecksum(true): %v", err)
	}
	_, err = verifying.DecompressRange(out, 0, len(out))
	verifying.Close()
	if !errors.Is(err, ErrBadChecksum) {
		t.Fatalf("verifying: want ErrBadChecksum, got %v", err)
	}

	// Default is off: bytes come back, wrong, with no error.
	skipping, err := Open(bad)
	if err != nil {
		t.Fatalf("Open(corrupt): %v", err)
	}
	n, err := skipping.DecompressRange(out, 0, len(out))
	skipping.Close()
	if err != nil || n != len(out) {
		t.Fatalf("not verifying: want %d bytes, got %d (%v)", len(out), n, err)
	}
	if bytes.Equal(out, payload[:len(out)]) {
		t.Fatalf("not verifying: expected wrong bytes")
	}
}

func TestSeekableOpenAndQuery(t *testing.T) {
	payload := bytes.Repeat([]byte("ZXCseekable_"), 8192) // ~96 KiB
	path := buildSeekableArchive(t, payload)

	s, err := Open(path)
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	defer s.Close()

	if got := s.DecompressedSize(); got != uint64(len(payload)) {
		t.Fatalf("DecompressedSize = %d, want %d", got, len(payload))
	}
	if s.NumBlocks() == 0 {
		t.Fatalf("NumBlocks = 0, want >= 1")
	}

	if cs, ok, err := s.BlockCompressedSize(0); !ok || err != nil || cs == 0 {
		t.Fatalf("BlockCompressedSize(0) = %d ok=%v err=%v", cs, ok, err)
	}
	if ds, ok := s.BlockDecompressedSize(0); !ok || ds == 0 {
		t.Fatalf("BlockDecompressedSize(0) = %d ok=%v", ds, ok)
	}
	if _, ok, err := s.BlockCompressedSize(s.NumBlocks()); ok || err != nil {
		t.Fatalf("BlockCompressedSize(out-of-range) = ok=%v err=%v, want false, nil", ok, err)
	}
}

func TestSeekableForgedGroupIsAnError(t *testing.T) {
	payload := bytes.Repeat([]byte("ZXCseekable_"), 8192)
	arc, err := os.ReadFile(buildSeekableArchive(t, payload))
	if err != nil {
		t.Fatalf("read archive: %v", err)
	}
	probe, err := OpenBytes(arc)
	if err != nil {
		t.Fatalf("OpenBytes: %v", err)
	}
	n := int(probe.NumBlocks())
	probe.Close()
	// Group 0's anchor opens the table, before the 16-byte footer (size, digest).
	table := (n+63)/64*8 + n*4
	anchor := len(arc) - 16 - table
	if arc[anchor] != 16 {
		t.Fatalf("byte %d = %d, not group 0's anchor", anchor, arc[anchor])
	}
	arc[anchor] ^= 0xFF

	s, err := OpenBytes(arc)
	if err != nil {
		t.Fatalf("OpenBytes must not scan the table: %v", err)
	}
	defer s.Close()
	if _, ok, err := s.BlockCompressedSize(0); !ok || !errors.Is(err, ErrInvalidData) {
		t.Fatalf("BlockCompressedSize(0) on a forged group: ok=%v err=%v", ok, err)
	}
}

func TestSeekableDecompressRange(t *testing.T) {
	payload := make([]byte, 64*1024)
	for i := range payload {
		payload[i] = byte(i * 31)
	}
	path := buildSeekableArchive(t, payload)

	s, err := Open(path)
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	defer s.Close()

	// Full range.
	full := make([]byte, len(payload))
	n, err := s.DecompressRange(full, 0, len(payload))
	if err != nil {
		t.Fatalf("DecompressRange full: %v", err)
	}
	if n != len(payload) || !bytes.Equal(full, payload) {
		t.Fatalf("full range mismatch (n=%d)", n)
	}

	// Mid-range.
	const off, length = 1024, 8192
	mid := make([]byte, length)
	n, err = s.DecompressRange(mid, off, length)
	if err != nil {
		t.Fatalf("DecompressRange mid: %v", err)
	}
	if n != length || !bytes.Equal(mid, payload[off:off+length]) {
		t.Fatalf("mid range mismatch (n=%d)", n)
	}
}

func TestSeekableOpenBytes(t *testing.T) {
	payload := bytes.Repeat([]byte("memseekable_"), 4096)
	path := buildSeekableArchive(t, payload)

	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read archive: %v", err)
	}

	s, err := OpenBytes(data)
	if err != nil {
		t.Fatalf("OpenBytes: %v", err)
	}
	defer s.Close()

	if got := s.DecompressedSize(); got != uint64(len(payload)) {
		t.Fatalf("DecompressedSize = %d, want %d", got, len(payload))
	}

	out := make([]byte, len(payload))
	if _, err := s.DecompressRange(out, 0, len(payload)); err != nil {
		t.Fatalf("DecompressRange: %v", err)
	}
	if !bytes.Equal(out, payload) {
		t.Fatalf("payload mismatch after OpenBytes round-trip")
	}
}

func TestSeekableInvalidBytes(t *testing.T) {
	if _, err := OpenBytes(make([]byte, 32)); err == nil {
		t.Fatalf("OpenBytes on garbage should fail")
	}
	if _, err := OpenBytes(nil); err == nil {
		t.Fatalf("OpenBytes(nil) should fail")
	}
}

// countingReaderAt wraps bytes.Reader and counts ReadAt invocations so
// tests can assert lazy I/O at open and per-block reads.
type countingReaderAt struct {
	inner io.ReaderAt
	calls int64
}

func (c *countingReaderAt) ReadAt(p []byte, off int64) (int, error) {
	atomic.AddInt64(&c.calls, 1)
	return c.inner.ReadAt(p, off)
}

func TestSeekableOpenReader(t *testing.T) {
	payload := make([]byte, 256*1024)
	for i := range payload {
		payload[i] = byte(i*7) ^ 0x5A
	}
	path := buildSeekableArchive(t, payload)
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read archive: %v", err)
	}

	cr := &countingReaderAt{inner: bytes.NewReader(data)}
	s, err := OpenReader(cr, int64(len(data)))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	defer s.Close()

	// open_reader should have done exactly 3 reads: header, footer, EOF/SEK
	// block headers.
	if got := atomic.LoadInt64(&cr.calls); got != 3 {
		t.Fatalf("open phase calls = %d, want 3", got)
	}

	if got := s.DecompressedSize(); got != uint64(len(payload)) {
		t.Fatalf("DecompressedSize = %d, want %d", got, len(payload))
	}

	// Full-range round-trip.
	out := make([]byte, len(payload))
	if _, err := s.DecompressRange(out, 0, len(payload)); err != nil {
		t.Fatalf("DecompressRange full: %v", err)
	}
	if !bytes.Equal(out, payload) {
		t.Fatalf("payload mismatch after full DecompressRange")
	}

	// Sub-range within a single block: its seek table entries, then the block.
	before := atomic.LoadInt64(&cr.calls)
	chunk := make([]byte, 1024)
	if _, err := s.DecompressRange(chunk, 100, 1024); err != nil {
		t.Fatalf("DecompressRange sub: %v", err)
	}
	if !bytes.Equal(chunk, payload[100:1124]) {
		t.Fatalf("sub-range mismatch")
	}
	if delta := atomic.LoadInt64(&cr.calls) - before; delta != 2 {
		t.Fatalf("single-block sub-range should trigger 2 reads (entries, block), got %d", delta)
	}
}

// hookReaderAt runs hook once, on its next read.
type hookReaderAt struct {
	inner io.ReaderAt
	hook  func()
}

func (h *hookReaderAt) ReadAt(p []byte, off int64) (int, error) {
	if f := h.hook; f != nil {
		h.hook = nil
		f()
	}
	return h.inner.ReadAt(p, off)
}

// From ReadAt all is refused but a range under the getter, which shares nothing.
func TestSeekableReentryFromReader(t *testing.T) {
	payload := bytes.Repeat([]byte("ZXCseekable_"), 8192)
	arc, err := os.ReadFile(buildSeekableArchive(t, payload))
	if err != nil {
		t.Fatalf("read archive: %v", err)
	}
	rd := &hookReaderAt{inner: bytes.NewReader(arc)}
	s, err := OpenReader(rd, int64(len(arc)))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	defer s.Close()

	dst := make([]byte, len(payload))
	calls := map[string]func() error{
		"BlockCompressedSize": func() error { _, _, err := s.BlockCompressedSize(0); return err },
		"DecompressRange":     func() error { _, err := s.DecompressRange(dst, 0, len(dst)); return err },
	}
	actions := map[string]func() error{
		"Close":           s.Close,
		"SetDict":         func() error { return s.SetDict([]byte("x"), nil) },
		"DecompressRange": func() error { _, err := s.DecompressRange(make([]byte, 16), 0, 16); return err },
	}
	cases := []struct {
		call, action string
		want         error
	}{
		{"BlockCompressedSize", "Close", ErrSeekableInUse},
		{"BlockCompressedSize", "SetDict", ErrSeekableInUse},
		{"BlockCompressedSize", "DecompressRange", nil},
		{"DecompressRange", "Close", ErrSeekableInUse},
		{"DecompressRange", "SetDict", ErrSeekableInUse},
		{"DecompressRange", "DecompressRange", ErrSeekableInUse},
	}
	for _, c := range cases {
		got := errors.New("hook not run")
		rd.hook = func() { got = actions[c.action]() }
		if err := calls[c.call](); err != nil {
			t.Fatalf("%s with %s from ReadAt: %v", c.call, c.action, err)
		}
		if got != c.want {
			t.Fatalf("%s from ReadAt during %s: %v, want %v", c.action, c.call, got, c.want)
		}
	}
	if n, err := s.DecompressRange(dst, 0, len(dst)); err != nil || !bytes.Equal(dst[:n], payload) {
		t.Fatalf("handle after the refusals: n=%d err=%v", n, err)
	}
}

func TestSeekableOpenReaderRejectsNilAndZero(t *testing.T) {
	if _, err := OpenReader(nil, 100); err == nil {
		t.Fatalf("OpenReader(nil, 100) should fail")
	}
	if _, err := OpenReader(bytes.NewReader([]byte{1, 2, 3}), 0); err == nil {
		t.Fatalf("OpenReader(r, 0) should fail")
	}
}

func TestSeekableOpenReaderRejectsGarbage(t *testing.T) {
	// 64 bytes of zeros pass the minimum-size check but parse as invalid.
	// The cgo handle must still be released (no leak in steady state).
	garbage := bytes.NewReader(make([]byte, 64))
	if _, err := OpenReader(garbage, 64); err == nil {
		t.Fatalf("OpenReader on garbage should fail")
	}
}

func TestSeekTableSizeAndWrite(t *testing.T) {
	compSizes := []uint32{128, 256, 200, 4}
	sz := SeekTableSize(uint64(len(compSizes)))
	if sz == 0 {
		t.Fatalf("SeekTableSize = 0")
	}
	buf := make([]byte, sz)
	n, err := WriteSeekTable(buf, compSizes)
	if err != nil {
		t.Fatalf("WriteSeekTable: %v", err)
	}
	if n != sz {
		t.Fatalf("WriteSeekTable wrote %d bytes, want %d", n, sz)
	}
}
