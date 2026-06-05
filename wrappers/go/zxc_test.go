/*
ZXC - High-performance lossless compression

Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
SPDX-License-Identifier: BSD-3-Clause
*/

package zxc

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"testing"
)

// ============================================================================
// Buffer API Tests
// ============================================================================

func TestRoundtrip(t *testing.T) {
	data := []byte("Hello, ZXC! This is a test of the Go wrapper.")

	compressed, err := Compress(data)
	if err != nil {
		t.Fatalf("Compress: %v", err)
	}

	decompressed, err := Decompress(compressed)
	if err != nil {
		t.Fatalf("Decompress: %v", err)
	}

	if !bytes.Equal(data, decompressed) {
		t.Fatal("roundtrip mismatch")
	}
}

func TestAllLevels(t *testing.T) {
	data := []byte("Test data with repetition: CCCCCCCCCCCCCCCCCCCCCCCCCCCCCC")

	for _, level := range AllLevels() {
		compressed, err := Compress(data, WithLevel(level))
		if err != nil {
			t.Fatalf("Compress at level %d: %v", level, err)
		}

		decompressed, err := Decompress(compressed)
		if err != nil {
			t.Fatalf("Decompress at level %d: %v", level, err)
		}

		if !bytes.Equal(data, decompressed) {
			t.Fatalf("roundtrip mismatch at level %d", level)
		}
	}
}

func TestLargeData(t *testing.T) {
	// 1 MB of compressible data
	data := make([]byte, 1024*1024)
	for i := range data {
		data[i] = byte((i % 256) ^ ((i / 256) % 256))
	}

	compressed, err := Compress(data)
	if err != nil {
		t.Fatalf("Compress: %v", err)
	}

	if len(compressed) >= len(data) {
		t.Fatalf("expected compression, got %d >= %d", len(compressed), len(data))
	}

	decompressed, err := Decompress(compressed)
	if err != nil {
		t.Fatalf("Decompress: %v", err)
	}

	if !bytes.Equal(data, decompressed) {
		t.Fatal("large data roundtrip mismatch")
	}
}

func TestDecompressedSize(t *testing.T) {
	data := []byte("Hello, world! Testing DecompressedSize function.")

	compressed, err := Compress(data)
	if err != nil {
		t.Fatalf("Compress: %v", err)
	}

	size, err := DecompressedSize(compressed)
	if err != nil {
		t.Fatalf("DecompressedSize: %v", err)
	}

	if size != uint64(len(data)) {
		t.Fatalf("DecompressedSize = %d, want %d", size, len(data))
	}
}

func TestCompressBound(t *testing.T) {
	bound := CompressBound(1024)
	if bound <= 1024 {
		t.Fatalf("CompressBound(1024) = %d, want > 1024", bound)
	}
}

func TestInvalidData(t *testing.T) {
	_, err := Decompress([]byte("not valid zxc data"))
	if err == nil {
		t.Fatal("expected error on invalid data")
	}
}

func TestEmptyInput(t *testing.T) {
	// Empty input produces a valid minimal frame (header + EOF + footer) that
	// round-trips back to empty.
	for _, in := range [][]byte{nil, {}} {
		compressed, err := Compress(in)
		if err != nil {
			t.Fatalf("Compress(empty) error: %v", err)
		}
		if len(compressed) == 0 {
			t.Fatal("Compress(empty) should produce a non-empty frame")
		}
		out, err := Decompress(compressed)
		if err != nil {
			t.Fatalf("Decompress(empty frame) error: %v", err)
		}
		if len(out) != 0 {
			t.Fatalf("empty must round-trip to empty, got %d bytes", len(out))
		}
	}
}

func TestChecksumRoundtrip(t *testing.T) {
	data := []byte("Test with checksum enabled for data integrity verification")

	compressed, err := Compress(data, WithChecksum(true))
	if err != nil {
		t.Fatalf("Compress with checksum: %v", err)
	}

	decompressed, err := Decompress(compressed, WithChecksum(true))
	if err != nil {
		t.Fatalf("Decompress with checksum: %v", err)
	}

	if !bytes.Equal(data, decompressed) {
		t.Fatal("checksum roundtrip mismatch")
	}
}

func TestCompressTo(t *testing.T) {
	data := []byte("Testing CompressTo with pre-allocated buffer")
	output := make([]byte, CompressBound(len(data)))

	n, err := CompressTo(data, output)
	if err != nil {
		t.Fatalf("CompressTo: %v", err)
	}

	decompressed, err := Decompress(output[:n])
	if err != nil {
		t.Fatalf("Decompress: %v", err)
	}

	if !bytes.Equal(data, decompressed) {
		t.Fatal("CompressTo roundtrip mismatch")
	}
}

func TestVersion(t *testing.T) {
	major, minor, patch := Version()
	s := VersionString()

	if s == "" {
		t.Fatal("VersionString returned empty")
	}

	expected := fmt.Sprintf("%d.%d.%d", major, minor, patch)
	if s != expected {
		t.Fatalf("VersionString() = %q, want %q", s, expected)
	}

	t.Logf("ZXC version: %s", s)
}

func TestErrorMessages(t *testing.T) {
	errors := []*Error{
		ErrMemory,
		ErrDstTooSmall,
		ErrCorruptData,
		ErrBadChecksum,
	}

	for _, e := range errors {
		msg := e.Error()
		if msg == "" {
			t.Fatalf("expected non-empty error message for code %d", e.Code)
		}
	}
}

// ============================================================================
// Streaming API Tests
// ============================================================================

func tempPath(t *testing.T, name string) string {
	t.Helper()
	dir := filepath.Join(os.TempDir(), "zxc_go_test")
	if err := os.MkdirAll(dir, 0o755); err != nil {
		t.Fatalf("MkdirAll: %v", err)
	}
	return filepath.Join(dir, name)
}

func TestFileRoundtrip(t *testing.T) {
	inputPath := tempPath(t, "roundtrip_input.bin")
	compressedPath := tempPath(t, "roundtrip_compressed.zxc")
	outputPath := tempPath(t, "roundtrip_output.bin")
	defer os.Remove(inputPath)
	defer os.Remove(compressedPath)
	defer os.Remove(outputPath)

	// 64 KB of compressible data
	data := make([]byte, 64*1024)
	for i := range data {
		data[i] = byte((i % 256) ^ ((i / 256) % 256))
	}

	if err := os.WriteFile(inputPath, data, 0o644); err != nil {
		t.Fatalf("WriteFile: %v", err)
	}

	csize, err := CompressFile(inputPath, compressedPath)
	if err != nil {
		t.Fatalf("CompressFile: %v", err)
	}
	if csize <= 0 {
		t.Fatalf("CompressFile returned %d bytes", csize)
	}

	dsize, err := DecompressFile(compressedPath, outputPath)
	if err != nil {
		t.Fatalf("DecompressFile: %v", err)
	}
	if dsize != int64(len(data)) {
		t.Fatalf("DecompressFile: got %d bytes, want %d", dsize, len(data))
	}

	result, err := os.ReadFile(outputPath)
	if err != nil {
		t.Fatalf("ReadFile: %v", err)
	}
	if !bytes.Equal(data, result) {
		t.Fatal("file roundtrip mismatch")
	}
}

func TestFileAllLevels(t *testing.T) {
	inputPath := tempPath(t, "levels_input.bin")
	defer os.Remove(inputPath)

	data := make([]byte, 32*1024)
	for i := range data {
		data[i] = byte((i % 256) ^ ((i / 256) % 256))
	}
	if err := os.WriteFile(inputPath, data, 0o644); err != nil {
		t.Fatalf("WriteFile: %v", err)
	}

	for _, level := range AllLevels() {
		compressedPath := tempPath(t, "levels_compressed.zxc")
		outputPath := tempPath(t, "levels_output.bin")

		_, err := CompressFile(inputPath, compressedPath, WithLevel(level))
		if err != nil {
			t.Fatalf("CompressFile at level %d: %v", level, err)
		}

		_, err = DecompressFile(compressedPath, outputPath)
		if err != nil {
			t.Fatalf("DecompressFile at level %d: %v", level, err)
		}

		result, err := os.ReadFile(outputPath)
		if err != nil {
			t.Fatalf("ReadFile: %v", err)
		}
		if !bytes.Equal(data, result) {
			t.Fatalf("file roundtrip mismatch at level %d", level)
		}

		os.Remove(compressedPath)
		os.Remove(outputPath)
	}
}

func TestFileMultithreaded(t *testing.T) {
	inputPath := tempPath(t, "mt_input.bin")
	compressedPath := tempPath(t, "mt_compressed.zxc")
	outputPath := tempPath(t, "mt_output.bin")
	defer os.Remove(inputPath)
	defer os.Remove(compressedPath)
	defer os.Remove(outputPath)

	// 1 MB of data
	data := make([]byte, 1024*1024)
	for i := range data {
		data[i] = byte((i % 256) ^ ((i / 256) % 256))
	}
	if err := os.WriteFile(inputPath, data, 0o644); err != nil {
		t.Fatalf("WriteFile: %v", err)
	}

	for _, threads := range []int{1, 2, 4} {
		_, err := CompressFile(inputPath, compressedPath, WithThreads(threads))
		if err != nil {
			t.Fatalf("CompressFile with %d threads: %v", threads, err)
		}

		dsize, err := DecompressFile(compressedPath, outputPath, WithThreads(threads))
		if err != nil {
			t.Fatalf("DecompressFile with %d threads: %v", threads, err)
		}
		if dsize != int64(len(data)) {
			t.Fatalf("got %d bytes with %d threads, want %d", dsize, threads, len(data))
		}

		result, err := os.ReadFile(outputPath)
		if err != nil {
			t.Fatalf("ReadFile: %v", err)
		}
		if !bytes.Equal(data, result) {
			t.Fatalf("mismatch with %d threads", threads)
		}
	}
}

// ============================================================================
// Push Streaming API Tests
// ============================================================================

// pstreamRoundtrip drives a CStream / DStream pair end-to-end and returns
// the decoded bytes.
func pstreamRoundtrip(t *testing.T, data []byte, copts, dopts []Option) []byte {
	t.Helper()

	cs, err := NewCStream(copts...)
	if err != nil {
		t.Fatalf("NewCStream: %v", err)
	}
	defer cs.Close()

	outCap := cs.OutSize()
	if outCap < 64 {
		outCap = 64
	}
	out := make([]byte, outCap)
	var compressed bytes.Buffer

	cursor := 0
	for cursor < len(data) {
		consumed, produced, _, err := cs.Compress(out, data[cursor:])
		if err != nil {
			t.Fatalf("CStream.Compress: %v", err)
		}
		cursor += consumed
		compressed.Write(out[:produced])
	}
	for {
		produced, pending, err := cs.End(out)
		if err != nil {
			t.Fatalf("CStream.End: %v", err)
		}
		compressed.Write(out[:produced])
		if pending == 0 {
			break
		}
	}

	ds, err := NewDStream(dopts...)
	if err != nil {
		t.Fatalf("NewDStream: %v", err)
	}
	defer ds.Close()

	dout := make([]byte, 64*1024)
	var decompressed bytes.Buffer
	src := compressed.Bytes()
	cursor = 0
	for cursor < len(src) && !ds.Finished() {
		consumed, produced, err := ds.Decompress(dout, src[cursor:])
		if err != nil {
			t.Fatalf("DStream.Decompress: %v", err)
		}
		cursor += consumed
		decompressed.Write(dout[:produced])
		if consumed == 0 && produced == 0 {
			break
		}
	}
	for {
		_, produced, err := ds.Decompress(dout, nil)
		if err != nil {
			t.Fatalf("DStream.Decompress (drain): %v", err)
		}
		decompressed.Write(dout[:produced])
		if produced == 0 {
			break
		}
	}
	if !ds.Finished() {
		t.Fatalf("DStream did not finalise (truncated stream?)")
	}
	return decompressed.Bytes()
}

func TestPStreamSmallRoundtrip(t *testing.T) {
	data := []byte("Hello pstream! Round-trip through the Go push API.")
	got := pstreamRoundtrip(t, data, nil, nil)
	if !bytes.Equal(got, data) {
		t.Fatalf("mismatch: got %d bytes, want %d", len(got), len(data))
	}
}

func TestPStreamWithChecksum(t *testing.T) {
	data := make([]byte, 32*1024)
	for i := range data {
		data[i] = byte(i % 251)
	}
	copts := []Option{WithChecksum(true)}
	dopts := []Option{WithChecksum(true)}
	got := pstreamRoundtrip(t, data, copts, dopts)
	if !bytes.Equal(got, data) {
		t.Fatalf("checksum roundtrip mismatch (%d vs %d bytes)", len(got), len(data))
	}
}

func TestPStreamMultiBlock(t *testing.T) {
	data := make([]byte, 512*1024)
	for i := range data {
		data[i] = byte((i * 7) % 256)
	}
	got := pstreamRoundtrip(t, data, nil, nil)
	if !bytes.Equal(got, data) {
		t.Fatalf("multi-block roundtrip mismatch (%d vs %d bytes)", len(got), len(data))
	}
}

func TestPStreamSizeHints(t *testing.T) {
	cs, err := NewCStream()
	if err != nil {
		t.Fatalf("NewCStream: %v", err)
	}
	defer cs.Close()
	if cs.InSize() == 0 || cs.OutSize() == 0 {
		t.Fatalf("cstream size hints should be non-zero")
	}

	ds, err := NewDStream()
	if err != nil {
		t.Fatalf("NewDStream: %v", err)
	}
	defer ds.Close()
	if ds.InSize() == 0 || ds.OutSize() == 0 {
		t.Fatalf("dstream size hints should be non-zero")
	}
	if ds.Finished() {
		t.Fatalf("dstream should not be finished before input")
	}
}

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

// ============================================================================
// Benchmarks
// ============================================================================

func BenchmarkCompress(b *testing.B) {
	data := make([]byte, 1024*1024)
	for i := range data {
		data[i] = byte((i % 256) ^ ((i / 256) % 256))
	}

	b.SetBytes(int64(len(data)))
	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		_, err := Compress(data)
		if err != nil {
			b.Fatal(err)
		}
	}
}

func BenchmarkDecompress(b *testing.B) {
	data := make([]byte, 1024*1024)
	for i := range data {
		data[i] = byte((i % 256) ^ ((i / 256) % 256))
	}

	compressed, err := Compress(data)
	if err != nil {
		b.Fatal(err)
	}

	b.SetBytes(int64(len(data)))
	b.ResetTimer()

	for i := 0; i < b.N; i++ {
		_, err := Decompress(compressed)
		if err != nil {
			b.Fatal(err)
		}
	}
}
