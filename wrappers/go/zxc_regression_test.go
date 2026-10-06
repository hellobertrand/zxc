// SPDX-License-Identifier: BSD-3-Clause
// ZXC - High-performance lossless compression
//
// Copyright (c) Bertrand Lebonnois and contributors.

package zxc

import (
	"bytes"
	"errors"
	"testing"
)

// Regression tests for wrapper-review fixes: create-time option inheritance
// in the block API, footer plausibility validation, pstream dictionary
// rejection, dictionary error-code mapping, and Huffman-table length checks.

func TestFixCctxStickyLevel(t *testing.T) {
	src := bytes.Repeat([]byte("the quick brown fox jumps over the lazy dog 0123456789 "), 2000)
	dst := make([]byte, CompressBlockBound(len(src)))

	cUltra, err := NewCctx(WithLevel(LevelUltra))
	if err != nil {
		t.Fatal(err)
	}
	defer cUltra.Close()
	nDefault := 0
	{
		c, _ := NewCctx(WithLevel(LevelDefault))
		defer c.Close()
		nDefault, err = c.CompressBlock(src, dst, WithLevel(LevelDefault))
		if err != nil {
			t.Fatal(err)
		}
	}
	nExplicitUltra := 0
	{
		c, _ := NewCctx()
		defer c.Close()
		nExplicitUltra, err = c.CompressBlock(src, dst, WithLevel(LevelUltra))
		if err != nil {
			t.Fatal(err)
		}
	}
	// No per-call options: must inherit the create-time ULTRA level.
	nInherited, err := cUltra.CompressBlock(src, dst)
	if err != nil {
		t.Fatal(err)
	}
	if nInherited != nExplicitUltra {
		t.Fatalf("create-time level not inherited: got %d, explicit ultra %d, default %d",
			nInherited, nExplicitUltra, nDefault)
	}
	if nInherited == nDefault && nDefault != nExplicitUltra {
		t.Fatalf("no-opts call silently used LevelDefault")
	}
}

// footerLen is the footer's length: the digest when the header announces
// checksums, then the two sizes and their lengths byte (FORMAT.md 8).
func footerLen(arc []byte) int {
	lens := arc[len(arc)-1]
	n := int(lens&7) + int(lens>>4) + 3
	if arc[6]&0x80 != 0 {
		n += 8
	}
	return n
}

// forgeFooterSize rewrites arc's footer to store size, keeping it well formed
// as a forger would: the digest, then the minimal sizes and their lengths byte.
func forgeFooterSize(arc []byte, size uint64) []byte {
	sizes := footerLen(arc)
	if arc[6]&0x80 != 0 {
		sizes -= 8
	}
	out := append([]byte(nil), arc[:len(arc)-sizes]...)
	uintBytes := func(v uint64) int {
		n := 1
		for n < 8 && v>>(8*n) != 0 {
			n++
		}
		return n
	}
	nd := uintBytes(size)
	base := uint64(len(out) + nd + 1)
	nf := 1
	for uintBytes(base+uint64(nf)) > nf {
		nf++
	}
	for i, v := 0, size; i < nd; i, v = i+1, v>>8 {
		out = append(out, byte(v))
	}
	for i, v := 0, base+uint64(nf); i < nf; i, v = i+1, v>>8 {
		out = append(out, byte(v))
	}
	return append(out, byte((nd-1)|(nf-1)<<4))
}

func TestFixDecompressCraftedFooter(t *testing.T) {
	comp, err := Compress([]byte("hello world hello world"))
	if err != nil {
		t.Fatal(err)
	}
	// A well-formed footer storing 2^64-1.
	comp = forgeFooterSize(comp, ^uint64(0))
	defer func() {
		if r := recover(); r != nil {
			t.Fatalf("Decompress panicked on crafted footer: %v", r)
		}
	}()
	if _, err := Decompress(comp); !errors.Is(err, ErrCorruptData) {
		t.Fatalf("want ErrCorruptData, got %v", err)
	}
}

func TestDecompressZeroedFooterSize(t *testing.T) {
	comp, err := Compress([]byte("hello world hello world hello world"))
	if err != nil {
		t.Fatal(err)
	}
	// A stored size of 0 is plausible, so the C size probe reports the archive
	// as empty; only the block walk contradicts it. Decompress must not hand
	// that back as a buffer-sizing error.
	comp = forgeFooterSize(comp, 0)
	if _, err := Decompress(comp); !errors.Is(err, ErrInvalidData) {
		t.Fatalf("want ErrInvalidData, got %v", err)
	}
}

func TestFixPstreamDictRejected(t *testing.T) {
	if _, err := NewCStream(WithDict([]byte("abc"))); !errors.Is(err, ErrDictUnsupported) {
		t.Fatalf("NewCStream(WithDict): want ErrDictUnsupported, got %v", err)
	}
	if _, err := NewDStream(WithDict([]byte("abc"))); !errors.Is(err, ErrDictUnsupported) {
		t.Fatalf("NewDStream(WithDict): want ErrDictUnsupported, got %v", err)
	}
}

func TestFixDictErrorCodes(t *testing.T) {
	dict := bytes.Repeat([]byte("sample dictionary content for zxc "), 100)
	payload := bytes.Repeat([]byte("sample dictionary content for zxc payload"), 50)
	comp, err := Compress(payload, WithDict(dict))
	if err != nil {
		t.Fatal(err)
	}
	if _, err := Decompress(comp); !errors.Is(err, ErrDictRequired) {
		t.Fatalf("want ErrDictRequired, got %v", err)
	}
	wrong := bytes.Repeat([]byte("a completely different dictionary "), 100)
	if _, err := Decompress(comp, WithDict(wrong)); !errors.Is(err, ErrDictMismatch) {
		t.Fatalf("want ErrDictMismatch, got %v", err)
	}
}

func TestFixHufTableValidation(t *testing.T) {
	dict := bytes.Repeat([]byte("sample dictionary content for zxc "), 100)
	if _, err := Compress([]byte("data"), WithDict(dict), WithDictHuf([]byte("short"))); !errors.Is(err, ErrBadHufTable) {
		t.Fatalf("want ErrBadHufTable, got %v", err)
	}
}
