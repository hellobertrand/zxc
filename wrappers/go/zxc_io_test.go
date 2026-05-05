/*
ZXC - High-performance lossless compression

Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
SPDX-License-Identifier: BSD-3-Clause
*/

package zxc

import (
	"bytes"
	"io"
	"testing"
)

// ============================================================================
// io.Reader / io.Writer adapter tests
// ============================================================================

func ioRoundtrip(t *testing.T, data []byte, copts, dopts []Option) []byte {
	t.Helper()

	var buf bytes.Buffer
	w, err := NewWriter(&buf, copts...)
	if err != nil {
		t.Fatalf("NewWriter: %v", err)
	}
	n, err := w.Write(data)
	if err != nil {
		t.Fatalf("Writer.Write: %v", err)
	}
	if n != len(data) {
		t.Fatalf("Writer.Write short: %d/%d", n, len(data))
	}
	if err := w.Close(); err != nil {
		t.Fatalf("Writer.Close: %v", err)
	}

	r, err := NewReader(&buf, dopts...)
	if err != nil {
		t.Fatalf("NewReader: %v", err)
	}
	defer r.Close()

	got, err := io.ReadAll(r)
	if err != nil {
		t.Fatalf("ReadAll: %v", err)
	}
	return got
}

func TestWriterReaderSmallRoundtrip(t *testing.T) {
	data := []byte("Hello io.Writer / io.Reader bridge over ZXC.")
	got := ioRoundtrip(t, data, nil, nil)
	if !bytes.Equal(got, data) {
		t.Fatalf("mismatch: got %q want %q", got, data)
	}
}

func TestWriterReaderLargeRoundtrip(t *testing.T) {
	data := make([]byte, 2*1024*1024)
	for i := range data {
		data[i] = byte((i * 13) % 251)
	}
	got := ioRoundtrip(t, data, nil, nil)
	if !bytes.Equal(got, data) {
		t.Fatalf("large roundtrip mismatch (%d vs %d)", len(got), len(data))
	}
}

func TestWriterReaderManySmallWrites(t *testing.T) {
	var buf bytes.Buffer
	w, err := NewWriter(&buf)
	if err != nil {
		t.Fatalf("NewWriter: %v", err)
	}
	want := make([]byte, 0, 64*1024)
	for i := 0; i < 4096; i++ {
		chunk := []byte{byte(i), byte(i >> 8), byte(i ^ 0x5A)}
		want = append(want, chunk...)
		if _, err := w.Write(chunk); err != nil {
			t.Fatalf("Write(%d): %v", i, err)
		}
	}
	if err := w.Close(); err != nil {
		t.Fatalf("Writer.Close: %v", err)
	}

	r, err := NewReader(&buf)
	if err != nil {
		t.Fatalf("NewReader: %v", err)
	}
	defer r.Close()
	got, err := io.ReadAll(r)
	if err != nil {
		t.Fatalf("ReadAll: %v", err)
	}
	if !bytes.Equal(got, want) {
		t.Fatalf("many-writes mismatch (%d vs %d)", len(got), len(want))
	}
}

func TestWriterReaderWithChecksum(t *testing.T) {
	data := make([]byte, 32*1024)
	for i := range data {
		data[i] = byte(i)
	}
	got := ioRoundtrip(t, data, []Option{WithChecksum(true)}, []Option{WithChecksum(true)})
	if !bytes.Equal(got, data) {
		t.Fatalf("checksum roundtrip mismatch")
	}
}

func TestWriterCloseIsIdempotent(t *testing.T) {
	var buf bytes.Buffer
	w, err := NewWriter(&buf)
	if err != nil {
		t.Fatalf("NewWriter: %v", err)
	}
	if _, err := w.Write([]byte("hello")); err != nil {
		t.Fatalf("Write: %v", err)
	}
	if err := w.Close(); err != nil {
		t.Fatalf("Close 1: %v", err)
	}
	if err := w.Close(); err != nil {
		t.Fatalf("Close 2: %v", err)
	}
}

func TestReaderCloseIsIdempotent(t *testing.T) {
	var buf bytes.Buffer
	w, _ := NewWriter(&buf)
	w.Write([]byte("xxx"))
	w.Close()
	r, err := NewReader(&buf)
	if err != nil {
		t.Fatalf("NewReader: %v", err)
	}
	if err := r.Close(); err != nil {
		t.Fatalf("Close 1: %v", err)
	}
	if err := r.Close(); err != nil {
		t.Fatalf("Close 2: %v", err)
	}
}

func TestReaderTruncatedFrame(t *testing.T) {
	var buf bytes.Buffer
	w, _ := NewWriter(&buf)
	payload := bytes.Repeat([]byte("ABCDEFGH"), 4096)
	if _, err := w.Write(payload); err != nil {
		t.Fatalf("Write: %v", err)
	}
	if err := w.Close(); err != nil {
		t.Fatalf("Close: %v", err)
	}

	// Truncate before the footer to force a premature EOF.
	full := buf.Bytes()
	truncated := full[:len(full)/2]

	r, err := NewReader(bytes.NewReader(truncated))
	if err != nil {
		t.Fatalf("NewReader: %v", err)
	}
	defer r.Close()
	_, err = io.ReadAll(r)
	if err == nil {
		t.Fatal("expected error reading truncated frame, got nil")
	}
}

// ============================================================================
// DetectZxc
// ============================================================================

func TestDetectZxc(t *testing.T) {
	// Real ZXC frame must be detected.
	compressed, err := Compress([]byte("magic-detection-input"))
	if err != nil {
		t.Fatalf("Compress: %v", err)
	}
	if !DetectZxc(compressed) {
		t.Fatal("DetectZxc returned false for a valid ZXC frame")
	}

	// Same goes for an io.Writer-produced frame.
	var buf bytes.Buffer
	w, _ := NewWriter(&buf)
	w.Write([]byte("hi"))
	w.Close()
	if !DetectZxc(buf.Bytes()) {
		t.Fatal("DetectZxc returned false for an io.Writer-produced frame")
	}

	// Negative cases.
	cases := [][]byte{
		nil,
		{},
		{0xF5, 0x2E, 0xB0}, // too short
		{0x00, 0x00, 0x00, 0x00},
		[]byte("not a zxc frame at all"),
	}
	for _, c := range cases {
		if DetectZxc(c) {
			t.Fatalf("DetectZxc returned true for non-ZXC input %x", c)
		}
	}
}
