/*
ZXC - High-performance lossless compression

Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
SPDX-License-Identifier: BSD-3-Clause
*/

package zxc

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"testing"
)

// dictSamples returns a small corpus of similar JSON-like records that share
// enough structure for the trainer to extract a useful dictionary.
func dictSamples() [][]byte {
	samples := make([][]byte, 0, 64)
	for i := 0; i < 64; i++ {
		s := fmt.Sprintf(
			`{"id":%d,"type":"user","name":"alice_%d","email":"alice%d@example.com","active":true,"role":"member"}`,
			i, i, i,
		)
		samples = append(samples, []byte(s))
	}
	return samples
}

func trainTestDict(t *testing.T) []byte {
	t.Helper()
	dict, err := TrainDict(dictSamples(), 0)
	if err != nil {
		t.Fatalf("TrainDict: %v", err)
	}
	if len(dict) == 0 {
		t.Fatalf("TrainDict returned empty dictionary")
	}
	return dict
}

func TestTrainDictNonEmpty(t *testing.T) {
	dict := trainTestDict(t)
	if len(dict) > DictSizeMax {
		t.Fatalf("dict size %d exceeds max %d", len(dict), DictSizeMax)
	}
	if DictID(dict) == 0 {
		t.Fatalf("DictID(content) = 0, want non-zero")
	}
}

func TestTrainDictEmptySamples(t *testing.T) {
	if _, err := TrainDict(nil, 0); err == nil {
		t.Fatalf("TrainDict(nil) should fail")
	}
}

func TestCompressDecompressWithDict(t *testing.T) {
	dict := trainTestDict(t)
	payload := []byte(`{"id":7,"type":"user","name":"alice_7","email":"alice7@example.com","active":true,"role":"member"}`)

	comp, err := Compress(payload, WithDict(dict))
	if err != nil {
		t.Fatalf("Compress(WithDict): %v", err)
	}

	got, err := Decompress(comp, WithDict(dict))
	if err != nil {
		t.Fatalf("Decompress(WithDict): %v", err)
	}
	if !bytes.Equal(got, payload) {
		t.Fatalf("roundtrip mismatch: got %q want %q", got, payload)
	}
}

func TestDecompressDictArchiveWithoutDictFails(t *testing.T) {
	dict := trainTestDict(t)
	payload := []byte(`{"id":3,"type":"user","name":"alice_3","email":"alice3@example.com","active":true,"role":"member"}`)

	comp, err := Compress(payload, WithDict(dict))
	if err != nil {
		t.Fatalf("Compress(WithDict): %v", err)
	}

	if _, err := Decompress(comp); err == nil {
		t.Fatalf("Decompress without dict should fail on a dict-compressed archive")
	}
}

func TestDictIDConsistency(t *testing.T) {
	dict := trainTestDict(t)
	payload := []byte(`{"id":42,"type":"user","name":"alice_42","email":"alice42@example.com","active":true,"role":"member"}`)

	contentID := DictID(dict)
	if contentID == 0 {
		t.Fatalf("DictID(content) = 0")
	}

	comp, err := Compress(payload, WithDict(dict))
	if err != nil {
		t.Fatalf("Compress(WithDict): %v", err)
	}
	if archiveID := GetDictID(comp); archiveID != contentID {
		t.Fatalf("GetDictID(archive) = %d, want %d", archiveID, contentID)
	}

	huf, err := TrainDictHuf(dictSamples(), dict)
	if err != nil {
		t.Fatalf("TrainDictHuf: %v", err)
	}
	zxd, err := DictSave(dict, huf)
	if err != nil {
		t.Fatalf("DictSave: %v", err)
	}
	// The .zxd id binds (content, table): non-zero and distinct from the
	// content-only id.
	if zxdID := DictGetID(zxd); zxdID == 0 || zxdID == contentID {
		t.Fatalf("DictGetID(.zxd) = %d, want non-zero and != content id %d", zxdID, contentID)
	}
	if got := DictHuf(zxd); !bytes.Equal(got, huf) {
		t.Fatalf("DictHuf(.zxd) mismatch")
	}
}

func TestDictSaveLoadRoundtrip(t *testing.T) {
	dict := trainTestDict(t)

	huf, err := TrainDictHuf(dictSamples(), dict)
	if err != nil {
		t.Fatalf("TrainDictHuf: %v", err)
	}
	zxd, err := DictSave(dict, huf)
	if err != nil {
		t.Fatalf("DictSave: %v", err)
	}

	content, id, err := DictLoad(zxd)
	if err != nil {
		t.Fatalf("DictLoad: %v", err)
	}
	if !bytes.Equal(content, dict) {
		t.Fatalf("DictLoad content mismatch (len got %d want %d)", len(content), len(dict))
	}
	if id != DictGetID(zxd) {
		t.Fatalf("DictLoad id = %d, want %d", id, DictGetID(zxd))
	}
}

func TestDictLoadInvalid(t *testing.T) {
	if _, _, err := DictLoad([]byte("not a zxd file")); err == nil {
		t.Fatalf("DictLoad on garbage should fail")
	}
}

func TestSeekableSetDictRange(t *testing.T) {
	dict := trainTestDict(t)

	// Build a payload large enough to span multiple seekable blocks so the
	// range decode exercises real block boundaries.
	var buf bytes.Buffer
	for i := 0; buf.Len() < 256*1024; i++ {
		fmt.Fprintf(&buf,
			`{"id":%d,"type":"user","name":"alice_%d","email":"alice%d@example.com","active":true,"role":"member"}`+"\n",
			i, i, i,
		)
	}
	payload := buf.Bytes()

	dir := t.TempDir()
	in := filepath.Join(dir, "input.bin")
	out := filepath.Join(dir, "archive.zxc")
	if err := os.WriteFile(in, payload, 0o644); err != nil {
		t.Fatalf("write input: %v", err)
	}
	if _, err := CompressFile(in, out, WithSeekable(true), WithDict(dict)); err != nil {
		t.Fatalf("CompressFile(seekable, dict): %v", err)
	}

	s, err := Open(out)
	if err != nil {
		t.Fatalf("Open: %v", err)
	}
	defer s.Close()

	if err := s.SetDict(dict, nil); err != nil {
		t.Fatalf("SetDict: %v", err)
	}

	// Decode a range that starts partway through the stream.
	const offset = 100 * 1024
	const length = 50 * 1024
	dst := make([]byte, length)
	n, err := s.DecompressRange(dst, offset, length)
	if err != nil {
		t.Fatalf("DecompressRange: %v", err)
	}
	if n != length {
		t.Fatalf("DecompressRange n = %d, want %d", n, length)
	}
	if !bytes.Equal(dst, payload[offset:offset+length]) {
		t.Fatalf("DecompressRange content mismatch")
	}
}

func TestDictionaryObjectRoundtrip(t *testing.T) {
	d, err := TrainDictionary(dictSamples())
	if err != nil {
		t.Fatalf("TrainDictionary: %v", err)
	}
	if len(d.Content()) == 0 || len(d.Huf()) != HufTableSize || d.ID() == 0 {
		t.Fatalf("Dictionary fields: content=%d huf=%d id=%d", len(d.Content()), len(d.Huf()), d.ID())
	}

	// Save/Load preserves the bundle and the pair-binding id.
	zxd, err := d.Save()
	if err != nil {
		t.Fatalf("Save: %v", err)
	}
	d2, err := LoadDictionary(zxd)
	if err != nil {
		t.Fatalf("LoadDictionary: %v", err)
	}
	if !bytes.Equal(d.Content(), d2.Content()) || !bytes.Equal(d.Huf(), d2.Huf()) || d.ID() != d2.ID() {
		t.Fatalf("Save/Load bundle mismatch")
	}
	if DictGetID(zxd) != d.ID() {
		t.Fatalf("DictGetID(.zxd) = %d, want %d", DictGetID(zxd), d.ID())
	}

	// One-call attach round-trip + id binding negative.
	payload := dictSamples()[2]
	comp, err := Compress(payload, WithLevel(LevelDensity), WithDictionary(d))
	if err != nil {
		t.Fatalf("Compress(WithDictionary): %v", err)
	}
	out, err := Decompress(comp, WithDictionary(d))
	if err != nil || !bytes.Equal(out, payload) {
		t.Fatalf("Decompress(WithDictionary): %v", err)
	}
	if _, err := Decompress(comp, WithDict(d.Content())); err == nil {
		t.Fatalf("content-only decode of a pair-bound archive should fail")
	}
}
