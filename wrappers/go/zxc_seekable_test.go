/*
ZXC - High-performance lossless compression

Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
SPDX-License-Identifier: BSD-3-Clause
*/

package zxc

import (
	"bytes"
	"os"
	"path/filepath"
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

	if cs, ok := s.BlockCompressedSize(0); !ok || cs == 0 {
		t.Fatalf("BlockCompressedSize(0) = %d ok=%v", cs, ok)
	}
	if ds, ok := s.BlockDecompressedSize(0); !ok || ds == 0 {
		t.Fatalf("BlockDecompressedSize(0) = %d ok=%v", ds, ok)
	}
	if _, ok := s.BlockCompressedSize(s.NumBlocks()); ok {
		t.Fatalf("BlockCompressedSize(out-of-range) should fail")
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

func TestSeekTableSizeAndWrite(t *testing.T) {
	compSizes := []uint32{128, 256, 200, 4}
	sz := SeekTableSize(uint32(len(compSizes)))
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
