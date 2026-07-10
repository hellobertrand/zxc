package zxc

import (
	"bytes"
	"encoding/binary"
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

func TestFixDecompressCraftedFooter(t *testing.T) {
	comp, err := Compress([]byte("hello world hello world"))
	if err != nil {
		t.Fatal(err)
	}
	// Footer = original_size(8) + checksum(4); patch original_size to 2^64-1.
	binary.LittleEndian.PutUint64(comp[len(comp)-12:], ^uint64(0))
	defer func() {
		if r := recover(); r != nil {
			t.Fatalf("Decompress panicked on crafted footer: %v", r)
		}
	}()
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
