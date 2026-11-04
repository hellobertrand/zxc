// SPDX-License-Identifier: BSD-3-Clause
// ZXC - High-performance lossless compression
//
// Copyright (c) Bertrand Lebonnois and contributors.

package zxc

import (
	"bytes"
	"errors"
	"io"
	"os"
	"path/filepath"
	"runtime"
	"strings"
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

// Parameters are set on a native context, which refuses a value out of its
// bounds instead of clamping it: the level on every compression entry point
// (at creation for a Cctx), the thread count on the file pipeline.
func TestParamRefusal(t *testing.T) {
	data := []byte("parameter bounds")
	if _, err := Compress(data, WithLevel(Level(MaxLevel()+1))); !errors.Is(err, ErrBadLevel) {
		t.Fatalf("Compress(level 8): want ErrBadLevel, got %v", err)
	}
	if _, err := Compress(data, WithLevel(0)); err != nil {
		t.Fatalf("Compress(level 0) selects the default: %v", err)
	}
	if _, err := NewCctx(WithLevel(-1)); !errors.Is(err, ErrBadLevel) {
		t.Fatalf("NewCctx(level -1): want ErrBadLevel, got %v", err)
	}
	if _, err := NewCStream(WithLevel(Level(MaxLevel() + 1))); !errors.Is(err, ErrBadLevel) {
		t.Fatalf("NewCStream(level 8): want ErrBadLevel, got %v", err)
	}
	if _, err := Compress(data, WithThreads(-1)); !errors.Is(err, ErrBadParam) {
		t.Fatalf("WithThreads(-1): want ErrBadParam, got %v", err)
	}
	c, err := NewCctx()
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	dst := make([]byte, CompressBlockBound(len(data)))
	if _, err := c.CompressBlock(data, dst, WithLevel(99)); !errors.Is(err, ErrBadLevel) {
		t.Fatalf("CompressBlock(level 99): want ErrBadLevel, got %v", err)
	}
	// The refused call leaves the context usable.
	if _, err := c.CompressBlock(data, dst); err != nil {
		t.Fatalf("CompressBlock after a refused level: %v", err)
	}
}

// A writable output in a directory that refuses new files is written in place.
func TestFileInPlaceFallback(t *testing.T) {
	if runtime.GOOS == "windows" || os.Getuid() == 0 {
		t.Skip("needs a directory the process cannot write to")
	}
	dir := t.TempDir()
	in := filepath.Join(dir, "in")
	locked := filepath.Join(dir, "locked")
	out := filepath.Join(locked, "out")
	payload := []byte("in-place fallback")
	if err := os.WriteFile(in, payload, 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.Mkdir(locked, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(out, []byte("old"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.Chmod(locked, 0o555); err != nil {
		t.Fatal(err)
	}
	defer os.Chmod(locked, 0o755) // so TempDir can clean up
	if _, err := CompressFile(in, out); err != nil {
		t.Fatalf("CompressFile into a read-only directory: %v", err)
	}
	arc, err := os.ReadFile(out)
	if err != nil {
		t.Fatal(err)
	}
	if dec, err := Decompress(arc); err != nil || !bytes.Equal(dec, payload) {
		t.Fatalf("in-place output does not decode: %v", err)
	}
	if ents, _ := os.ReadDir(locked); len(ents) != 1 {
		t.Fatalf("temporary file left behind: %d entries", len(ents))
	}
}

// A failed file call, refused option or bad data, leaves an existing output
// untouched and no temporary file behind; a successful one replaces it.
func TestFileFailureKeepsOutput(t *testing.T) {
	dir := t.TempDir()
	in := filepath.Join(dir, "in")
	arc := filepath.Join(dir, "arc")
	out := filepath.Join(dir, "out")
	payload := bytes.Repeat([]byte("file pipeline input "), 500)
	if err := os.WriteFile(in, payload, 0o644); err != nil {
		t.Fatal(err)
	}
	if _, err := CompressFile(in, arc, WithChecksum(true)); err != nil {
		t.Fatal(err)
	}
	bad, err := os.ReadFile(arc)
	if err != nil {
		t.Fatal(err)
	}
	bad[len(bad)/2] ^= 0xFF
	corrupt := filepath.Join(dir, "corrupt")
	if err := os.WriteFile(corrupt, bad, 0o644); err != nil {
		t.Fatal(err)
	}
	keep := []byte("previous content must survive")
	if err := os.WriteFile(out, keep, 0o600); err != nil {
		t.Fatal(err)
	}

	if _, err := CompressFile(in, out, WithLevel(Level(MaxLevel()+1))); !errors.Is(err, ErrBadLevel) {
		t.Fatalf("CompressFile(level 8): want ErrBadLevel, got %v", err)
	}
	if _, err := DecompressFile(arc, out, WithThreads(-1)); !errors.Is(err, ErrBadParam) {
		t.Fatalf("DecompressFile(threads -1): want ErrBadParam, got %v", err)
	}
	if _, err := DecompressFile(corrupt, out, WithChecksum(true)); err == nil {
		t.Fatal("DecompressFile(corrupt): want an error")
	}
	if got, _ := os.ReadFile(out); !bytes.Equal(got, keep) {
		t.Fatalf("output changed by a failed call: %q", got)
	}
	if ents, _ := os.ReadDir(dir); len(ents) != 4 {
		t.Fatalf("temporary file left behind: %d entries", len(ents))
	}

	if _, err := DecompressFile(arc, out); err != nil {
		t.Fatal(err)
	}
	if got, _ := os.ReadFile(out); !bytes.Equal(got, payload) {
		t.Fatal("successful call did not replace the output")
	}
	if st, err := os.Stat(out); err != nil || (runtime.GOOS != "windows" && st.Mode().Perm() != 0o600) {
		t.Fatalf("output mode not kept: %v %v", st.Mode(), err)
	}
	if runtime.GOOS != "windows" {
		if _, err := DecompressFile(arc, os.DevNull); err != nil {
			t.Fatalf("DecompressFile to %s: %v", os.DevNull, err)
		}
	}
}

// padded inserts n junk bytes before arc's footer: bytes a footer can claim to
// have held blocks, which is all the size check asks of a forged archive.
func padded(arc []byte, n int) []byte {
	sizes := footerLen(arc)
	out := append([]byte(nil), arc[:len(arc)-sizes]...)
	out = append(out, make([]byte, n)...)
	return append(out, arc[len(arc)-sizes:]...)
}

func TestMaxOutputSizeRefusesBeforeAllocating(t *testing.T) {
	src := bytes.Repeat([]byte("forged footer "), 1000)
	arc, err := Compress(src)
	if err != nil {
		t.Fatal(err)
	}
	const claim = 1 << 36
	forged := forgeFooterSize(padded(arc, 1<<20), claim)
	if size, err := DecompressedSize(forged); err != nil || size != claim {
		t.Fatalf("forged footer not accepted by the size query: %d %v", size, err)
	}
	var before, after runtime.MemStats
	runtime.ReadMemStats(&before)
	_, err = Decompress(forged, WithMaxOutputSize(1<<20))
	runtime.ReadMemStats(&after)
	if !errors.Is(err, ErrDstTooSmall) {
		t.Fatalf("want ErrDstTooSmall, got %v", err)
	}
	if delta := after.TotalAlloc - before.TotalAlloc; delta > 1<<20 {
		t.Fatalf("allocated %d bytes before refusing", delta)
	}

	if out, err := Decompress(arc, WithMaxOutputSize(uint64(len(src)))); err != nil || !bytes.Equal(out, src) {
		t.Fatalf("limit equal to the size: %v", err)
	}
	if _, err := Decompress(arc, WithMaxOutputSize(uint64(len(src))-1)); !errors.Is(err, ErrDstTooSmall) {
		t.Fatalf("limit one below the size: want ErrDstTooSmall, got %v", err)
	}
	if _, err := Decompress(arc, WithMaxOutputSize(0)); !errors.Is(err, ErrDstTooSmall) {
		t.Fatalf("limit 0 is a limit: want ErrDstTooSmall, got %v", err)
	}
	empty, err := Compress(nil)
	if err != nil {
		t.Fatal(err)
	}
	if out, err := Decompress(empty, WithMaxOutputSize(0)); err != nil || len(out) != 0 {
		t.Fatalf("empty archive under limit 0: %d %v", len(out), err)
	}
}

func TestFileRefusesSameFile(t *testing.T) {
	dir := t.TempDir()
	f := filepath.Join(dir, "f")
	src := bytes.Repeat([]byte("same file "), 1000)
	if err := os.WriteFile(f, src, 0o644); err != nil {
		t.Fatal(err)
	}
	check := func(what string, err error) {
		t.Helper()
		if !errors.Is(err, ErrBadParam) {
			t.Fatalf("%s: want ErrBadParam, got %v", what, err)
		}
		if got, _ := os.ReadFile(f); !bytes.Equal(got, src) {
			t.Fatalf("%s: input changed, %d bytes left", what, len(got))
		}
	}
	_, err := CompressFile(f, f)
	check("CompressFile", err)
	_, err = DecompressFile(f, f)
	check("DecompressFile", err)

	if runtime.GOOS != "windows" {
		link := filepath.Join(dir, "link")
		if err := os.Symlink(f, link); err != nil {
			t.Fatal(err)
		}
		_, err = CompressFile(f, link)
		check("CompressFile through a symlink", err)
	}

	// The case that used to lose data: no sibling allowed, so the fallback
	// truncates the input before reading it.
	if runtime.GOOS == "windows" || os.Getuid() == 0 {
		return
	}
	if err := os.Chmod(dir, 0o555); err != nil {
		t.Fatal(err)
	}
	defer os.Chmod(dir, 0o755) // so TempDir can clean up
	_, err = CompressFile(f, f)
	check("CompressFile in a read-only directory", err)
}

func TestNewDctxChecksumIsDefault(t *testing.T) {
	src := bytes.Repeat([]byte("line of text for the block checksum test\n"), 1600)
	cc, err := NewCctx(WithChecksum(true))
	if err != nil {
		t.Fatal(err)
	}
	defer cc.Close()
	block := make([]byte, CompressBlockBound(len(src)))
	n, err := cc.CompressBlock(src, block)
	if err != nil {
		t.Fatal(err)
	}
	block = block[:n]

	dc, err := NewDctx(WithChecksum(true))
	if err != nil {
		t.Fatal(err)
	}
	defer dc.Close()
	dst := make([]byte, DecompressBlockBound(len(src)))
	if m, err := dc.DecompressBlock(block, dst); err != nil || !bytes.Equal(dst[:m], src) {
		t.Fatalf("intact block: %v", err)
	}
	for i := 8; i < len(block); i += 7 {
		bad := append([]byte(nil), block...)
		bad[i] ^= 0x10
		if m, err := dc.DecompressBlock(bad, dst); err == nil && !bytes.Equal(dst[:m], src) {
			t.Fatalf("flipped byte %d decoded to wrong bytes with no error", i)
		}
		if m, err := dc.DecompressBlockSafe(bad, dst); err == nil && !bytes.Equal(dst[:m], src) {
			t.Fatalf("flipped byte %d: safe path, wrong bytes with no error", i)
		}
	}
	// A per-call option still overrides it.
	flipped := -1
	for i := 8; i < len(block) && flipped < 0; i += 7 {
		bad := append([]byte(nil), block...)
		bad[i] ^= 0x10
		if m, err := dc.DecompressBlock(bad, dst, WithChecksum(false)); err == nil && !bytes.Equal(dst[:m], src) {
			flipped = i
		}
	}
	if flipped < 0 {
		t.Fatal("WithChecksum(false) did not override the context's default")
	}
}

func TestEmptyArchiveSizeAndDecompressTo(t *testing.T) {
	empty, err := Compress(nil)
	if err != nil {
		t.Fatal(err)
	}
	for name, arc := range map[string][]byte{
		"plain":    empty,
		"concat":   append(append([]byte(nil), empty...), empty...),
		"seekable": mustCompress(t, nil, WithSeekable(true)),
		"checksum": mustCompress(t, nil, WithChecksum(true)),
	} {
		if size, err := DecompressedSize(arc); err != nil || size != 0 {
			t.Fatalf("%s: DecompressedSize = %d, %v", name, size, err)
		}
		for _, out := range [][]byte{nil, make([]byte, 1)} {
			if n, err := DecompressTo(arc, out); err != nil || n != 0 {
				t.Fatalf("%s: DecompressTo(%d-byte buffer) = %d, %v", name, len(out), n, err)
			}
		}
	}

	// Invalid input still reads as invalid, also behind a valid last frame.
	for name, bad := range map[string][]byte{
		"junk":         bytes.Repeat([]byte("garbage!"), 8),
		"truncated":    empty[:len(empty)-1],
		"junk prefix":  append(bytes.Repeat([]byte("garbage!"), 8), empty...),
		"frame + junk": append(append([]byte(nil), empty...), 1, 2, 3, 4, 5, 6, 7, 8),
	} {
		if size, err := DecompressedSize(bad); !errors.Is(err, ErrInvalidData) {
			t.Fatalf("%s: want ErrInvalidData, got %d, %v", name, size, err)
		}
	}

	// A non-empty archive still needs room.
	full := mustCompress(t, []byte("not empty"))
	if _, err := DecompressTo(full, nil); !errors.Is(err, ErrDstTooSmall) {
		t.Fatalf("DecompressTo(data, nil): want ErrDstTooSmall, got %v", err)
	}
}

func mustCompress(t *testing.T, data []byte, opts ...Option) []byte {
	t.Helper()
	arc, err := Compress(data, opts...)
	if err != nil {
		t.Fatal(err)
	}
	return arc
}

func TestCompressDoesNotPinBound(t *testing.T) {
	src := bytes.Repeat([]byte("compressible line of text\n"), 200000)
	arc := mustCompress(t, src)
	if cap(arc) != len(arc) {
		t.Fatalf("result of %d bytes pins an array of %d", len(arc), cap(arc))
	}
	if dec, err := Decompress(arc); err != nil || !bytes.Equal(dec, src) {
		t.Fatalf("roundtrip: %v", err)
	}
	// Incompressible data keeps the buffer, within a factor of two of the result.
	rnd := make([]byte, 1<<16)
	for i, x := 0, uint32(1); i < len(rnd); i++ {
		x = x*1664525 + 1013904223
		rnd[i] = byte(x >> 24)
	}
	if arc := mustCompress(t, rnd); cap(arc) > 2*len(arc) {
		t.Fatalf("result of %d bytes pins an array of %d", len(arc), cap(arc))
	}
}

// oneShotReader hands everything over in one Read, along with a final error.
type oneShotReader struct {
	data []byte
	err  error
}

func (r *oneShotReader) Read(p []byte) (int, error) {
	n := copy(p, r.data)
	r.data = r.data[n:]
	if len(r.data) == 0 {
		return n, r.err
	}
	return n, nil
}

func TestReaderKeepsBytesReturnedWithAnError(t *testing.T) {
	src := bytes.Repeat([]byte("reader data with an error "), 40000)
	arc := mustCompress(t, src)
	boom := errors.New("boom")

	for _, size := range []int{1 << 20, 100} {
		r, err := NewReader(&oneShotReader{data: arc, err: boom})
		if err != nil {
			t.Fatal(err)
		}
		defer r.Close()
		var got bytes.Buffer
		buf := make([]byte, size)
		for {
			n, err := r.Read(buf)
			got.Write(buf[:n])
			if err != nil {
				if !errors.Is(err, boom) {
					t.Fatalf("read size %d: want boom, got %v", size, err)
				}
				break
			}
		}
		if !bytes.Equal(got.Bytes(), src) {
			t.Fatalf("read size %d: got %d of %d bytes", size, got.Len(), len(src))
		}
		if _, err := r.Read(buf); !errors.Is(err, boom) {
			t.Fatalf("read size %d: error must stick, got %v", size, err)
		}
	}

	// Truncated by the failure: what the bytes hold, then the source's error.
	r, err := NewReader(&oneShotReader{data: arc[:len(arc)-5], err: boom})
	if err != nil {
		t.Fatal(err)
	}
	defer r.Close()
	if _, err := io.ReadAll(r); !errors.Is(err, boom) {
		t.Fatalf("truncated: want boom, got %v", err)
	}
}

func TestOutputLongName(t *testing.T) {
	dir := t.TempDir()
	in := filepath.Join(dir, "in")
	src := []byte("long output name")
	if err := os.WriteFile(in, src, 0o644); err != nil {
		t.Fatal(err)
	}
	long := filepath.Join(dir, strings.Repeat("n", 245))
	f, err := os.Create(long)
	if err != nil {
		t.Skipf("file system refuses the name itself: %v", err)
	}
	f.Close()
	os.Remove(long)

	if _, err := CompressFile(in, long); err != nil {
		t.Fatalf("CompressFile: %v", err)
	}
	arc, err := os.ReadFile(long)
	if err != nil {
		t.Fatal(err)
	}
	if dec, err := Decompress(arc); err != nil || !bytes.Equal(dec, src) {
		t.Fatalf("roundtrip: %v", err)
	}
	if ents, _ := os.ReadDir(dir); len(ents) != 2 {
		t.Fatalf("temporary file left behind: %d entries", len(ents))
	}
}

func TestOutputSymlinks(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("symlinks need a privilege on Windows")
	}
	dir := t.TempDir()
	in := filepath.Join(dir, "in")
	src := []byte("output through a link")
	if err := os.WriteFile(in, src, 0o644); err != nil {
		t.Fatal(err)
	}
	isLink := func(p string) bool {
		st, err := os.Lstat(p)
		return err == nil && st.Mode()&os.ModeSymlink != 0
	}
	decoded := func(p string) []byte {
		t.Helper()
		arc, err := os.ReadFile(p)
		if err != nil {
			t.Fatal(err)
		}
		dec, err := Decompress(arc)
		if err != nil {
			t.Fatal(err)
		}
		return dec
	}

	// Dangling, relative, and through a second link.
	if err := os.Mkdir(filepath.Join(dir, "sub"), 0o755); err != nil {
		t.Fatal(err)
	}
	second := filepath.Join(dir, "sub", "second")
	if err := os.Symlink("../target", second); err != nil {
		t.Fatal(err)
	}
	first := filepath.Join(dir, "first")
	if err := os.Symlink(filepath.Join("sub", "second"), first); err != nil {
		t.Fatal(err)
	}
	if _, err := CompressFile(in, first); err != nil {
		t.Fatalf("dangling link: %v", err)
	}
	if !isLink(first) || !isLink(second) {
		t.Fatal("dangling link replaced by a regular file")
	}
	if !bytes.Equal(decoded(filepath.Join(dir, "target")), src) {
		t.Fatal("dangling link: target not written")
	}

	// ".." in a link's target is relative to its real directory.
	if err := os.MkdirAll(filepath.Join(dir, "a", "b"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink(filepath.Join("a", "b"), filepath.Join(dir, "via")); err != nil {
		t.Fatal(err)
	}
	up := filepath.Join(dir, "a", "b", "up")
	if err := os.Symlink("../uptarget", up); err != nil {
		t.Fatal(err)
	}
	if _, err := CompressFile(in, filepath.Join(dir, "via", "up")); err != nil {
		t.Fatalf("link through a symlinked directory: %v", err)
	}
	if !bytes.Equal(decoded(filepath.Join(dir, "a", "uptarget")), src) {
		t.Fatal("link through a symlinked directory: target not written")
	}

	// A cycle is an error, and creates nothing.
	cyc := filepath.Join(dir, "cycle")
	if err := os.Symlink("cycle", cyc); err != nil {
		t.Fatal(err)
	}
	if _, err := CompressFile(in, cyc); err == nil {
		t.Fatal("symlink cycle: want an error")
	}
	if !isLink(cyc) {
		t.Fatal("symlink cycle replaced")
	}
}

func TestDecompressFooterTooSmallIsInvalidData(t *testing.T) {
	src := bytes.Repeat([]byte("footer smaller than its blocks "), 1000)
	arc := mustCompress(t, src)
	if _, err := Decompress(forgeFooterSize(arc, uint64(len(src))-1)); !errors.Is(err, ErrInvalidData) {
		t.Fatalf("want ErrInvalidData, got %v", err)
	}
	if _, err := Decompress(forgeFooterSize(arc, 1)); !errors.Is(err, ErrInvalidData) {
		t.Fatalf("want ErrInvalidData, got %v", err)
	}
}

func TestDictErrorTypes(t *testing.T) {
	var e *Error
	if !errors.As(error(ErrDictUnsupported), &e) || e.Code != -19 {
		t.Fatalf("ErrDictUnsupported is not an *Error with code -19: %v", ErrDictUnsupported)
	}
	if _, err := NewCStream(WithDict([]byte("abc"))); !errors.As(err, &e) || e.Code != -19 {
		t.Fatalf("NewCStream(WithDict): %v", err)
	}
	if _, err := DictSave([]byte("content"), []byte("short")); !errors.Is(err, ErrBadHufTable) {
		t.Fatalf("DictSave(bad table): want ErrBadHufTable, got %v", err)
	}
}

func TestNilDictionary(t *testing.T) {
	src := []byte("no dictionary at all")
	arc, err := Compress(src, WithDictionary(nil))
	if err != nil {
		t.Fatal(err)
	}
	if GetDictID(arc) != 0 {
		t.Fatal("WithDictionary(nil) produced a dictionary archive")
	}
	if dec, err := Decompress(arc, WithDictionary(nil)); err != nil || !bytes.Equal(dec, src) {
		t.Fatalf("Decompress(WithDictionary(nil)): %v", err)
	}

	s, err := OpenBytes(mustCompress(t, src, WithSeekable(true)))
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()
	if err := s.SetDictionary(nil); !errors.Is(err, ErrNullInput) {
		t.Fatalf("SetDictionary(nil): want ErrNullInput, got %v", err)
	}
}

// checksumPayload is incompressible, so its first block is RAW: a flipped payload
// byte decodes cleanly and only a checksum can object.
func checksumPayload() []byte {
	payload := make([]byte, 256*1024)
	for i, x := 0, uint32(0x2E5B9A17); i < len(payload); i++ {
		x = x*1103515245 + 12345
		payload[i] = byte(x >> 16)
	}
	return payload
}

// readPaths are the frame read paths; skip opts out of verifying.
var readPaths = []struct {
	name string
	read func(t *testing.T, arc []byte, skip bool) ([]byte, error)
}{
	{"Decompress", func(t *testing.T, arc []byte, skip bool) ([]byte, error) {
		return Decompress(arc, checksumOpts(skip)...)
	}},
	{"DecompressTo", func(t *testing.T, arc []byte, skip bool) ([]byte, error) {
		out := make([]byte, 1<<20)
		n, err := DecompressTo(arc, out, checksumOpts(skip)...)
		return out[:n], err
	}},
	{"DStream", func(t *testing.T, arc []byte, skip bool) ([]byte, error) {
		ds, err := NewDStream(checksumOpts(skip)...)
		if err != nil {
			t.Fatal(err)
		}
		defer ds.Close()
		out := make([]byte, ds.OutSize())
		var got []byte
		for !ds.Finished() {
			c, p, err := ds.Decompress(out, arc)
			if err != nil {
				return got, err
			}
			if c == 0 && p == 0 {
				return got, io.ErrUnexpectedEOF
			}
			got = append(got, out[:p]...)
			arc = arc[c:]
		}
		return got, nil
	}},
	{"Reader", func(t *testing.T, arc []byte, skip bool) ([]byte, error) {
		r, err := NewReader(bytes.NewReader(arc), checksumOpts(skip)...)
		if err != nil {
			t.Fatal(err)
		}
		defer r.Close()
		return io.ReadAll(r)
	}},
	{"DecompressFile", func(t *testing.T, arc []byte, skip bool) ([]byte, error) {
		dir := t.TempDir()
		in, out := filepath.Join(dir, "in"), filepath.Join(dir, "out")
		if err := os.WriteFile(in, arc, 0o644); err != nil {
			t.Fatal(err)
		}
		if _, err := DecompressFile(in, out, checksumOpts(skip)...); err != nil {
			return nil, err
		}
		return os.ReadFile(out)
	}},
	{"Seekable.Open", func(t *testing.T, arc []byte, skip bool) ([]byte, error) {
		p := filepath.Join(t.TempDir(), "arc")
		if err := os.WriteFile(p, arc, 0o644); err != nil {
			t.Fatal(err)
		}
		s, err := Open(p)
		if err != nil {
			t.Fatal(err)
		}
		return readSeekable(t, s, skip)
	}},
	{"Seekable.OpenBytes", func(t *testing.T, arc []byte, skip bool) ([]byte, error) {
		s, err := OpenBytes(arc)
		if err != nil {
			t.Fatal(err)
		}
		return readSeekable(t, s, skip)
	}},
	{"Seekable.OpenReader", func(t *testing.T, arc []byte, skip bool) ([]byte, error) {
		s, err := OpenReader(bytes.NewReader(arc), int64(len(arc)))
		if err != nil {
			t.Fatal(err)
		}
		return readSeekable(t, s, skip)
	}},
}

func checksumOpts(skip bool) []Option {
	if skip {
		return []Option{WithChecksum(false)}
	}
	return nil
}

func readSeekable(t *testing.T, s *Seekable, skip bool) ([]byte, error) {
	t.Helper()
	defer s.Close()
	if skip {
		if err := s.SetChecksum(false); err != nil {
			t.Fatal(err)
		}
	}
	out := make([]byte, s.DecompressedSize())
	n, err := s.DecompressRange(out, 0, len(out))
	return out[:n], err
}

func TestDecompressionVerifiesByDefault(t *testing.T) {
	src := checksumPayload()
	arc := mustCompress(t, src, WithChecksum(true), WithSeekable(true))
	if arc[16] != 0 {
		t.Fatalf("block 0 type = %d, want RAW", arc[16])
	}
	bad := append([]byte(nil), arc...)
	bad[16+8+4] ^= 0xFF // file header, block header, into the payload

	plain := mustCompress(t, src, WithSeekable(true))
	if fi, err := GetFrameInfo(plain); err != nil || fi.HasChecksum {
		t.Fatalf("no-checksum archive: %+v, %v", fi, err)
	}

	for _, p := range readPaths {
		t.Run(p.name, func(t *testing.T) {
			if got, err := p.read(t, arc, false); err != nil || !bytes.Equal(got, src) {
				t.Fatalf("intact archive with checksums: %d bytes, %v", len(got), err)
			}
			if _, err := p.read(t, bad, false); !errors.Is(err, ErrBadChecksum) {
				t.Fatalf("corrupt archive, default: want ErrBadChecksum, got %v", err)
			}
			got, err := p.read(t, bad, true)
			if err != nil {
				t.Fatalf("corrupt archive, opted out: %v", err)
			}
			if len(got) != len(src) || bytes.Equal(got, src) {
				t.Fatalf("corrupt archive, opted out: want the wrong %d bytes, got %d", len(src), len(got))
			}
			if got, err := p.read(t, plain, false); err != nil || !bytes.Equal(got, src) {
				t.Fatalf("archive without checksums, default: %d bytes, %v", len(got), err)
			}
			if got, err := p.read(t, plain, true); err != nil || !bytes.Equal(got, src) {
				t.Fatalf("archive without checksums, opted out: %d bytes, %v", len(got), err)
			}
		})
	}
}

func TestCompressionWritesNoChecksumByDefault(t *testing.T) {
	src := []byte("compressed with every default")
	check := func(name string, arc []byte) {
		t.Helper()
		fi, err := GetFrameInfo(arc)
		if err != nil {
			t.Fatalf("%s: %v", name, err)
		}
		if fi.HasChecksum || fi.Digest != 0 {
			t.Fatalf("%s: default archive carries a checksum: %+v", name, fi)
		}
	}
	check("Compress", mustCompress(t, src))

	var w bytes.Buffer
	zw, err := NewWriter(&w)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := zw.Write(src); err != nil {
		t.Fatal(err)
	}
	if err := zw.Close(); err != nil {
		t.Fatal(err)
	}
	check("Writer", w.Bytes())

	cs, err := NewCStream()
	if err != nil {
		t.Fatal(err)
	}
	defer cs.Close()
	var cout []byte
	buf := make([]byte, cs.OutSize())
	if c, p, _, err := cs.Compress(buf, src); err != nil || c != len(src) {
		t.Fatalf("CStream.Compress: %d, %v", c, err)
	} else {
		cout = append(cout, buf[:p]...)
	}
	for {
		p, pending, err := cs.End(buf)
		if err != nil {
			t.Fatal(err)
		}
		cout = append(cout, buf[:p]...)
		if pending == 0 {
			break
		}
	}
	check("CStream", cout)

	dir := t.TempDir()
	in, out := filepath.Join(dir, "in"), filepath.Join(dir, "out")
	if err := os.WriteFile(in, src, 0o644); err != nil {
		t.Fatal(err)
	}
	if _, err := CompressFile(in, out); err != nil {
		t.Fatal(err)
	}
	arc, err := os.ReadFile(out)
	if err != nil {
		t.Fatal(err)
	}
	check("CompressFile", arc)
}

func TestBlockChecksumOffByDefault(t *testing.T) {
	src := checksumPayload()
	block := func(opts ...Option) []byte {
		t.Helper()
		cc, err := NewCctx(opts...)
		if err != nil {
			t.Fatal(err)
		}
		defer cc.Close()
		dst := make([]byte, CompressBlockBound(len(src)))
		n, err := cc.CompressBlock(src, dst)
		if err != nil {
			t.Fatal(err)
		}
		return dst[:n]
	}
	plain, summed := block(), block(WithChecksum(true))
	if len(summed) != len(plain)+4 {
		t.Fatalf("default block must carry no checksum: %d bytes, %d with one", len(plain), len(summed))
	}

	dc, err := NewDctx()
	if err != nil {
		t.Fatal(err)
	}
	defer dc.Close()
	dst := make([]byte, DecompressBlockBound(len(src)))

	bad := append([]byte(nil), plain...)
	bad[8+4] ^= 0xFF
	if m, err := dc.DecompressBlock(bad, dst); err != nil || bytes.Equal(dst[:m], src) {
		t.Fatalf("default decode of a flipped block: %d bytes, %v", m, err)
	}
	if m, err := dc.DecompressBlock(plain, dst); err != nil || !bytes.Equal(dst[:m], src) {
		t.Fatalf("both off: %v", err)
	}
	if m, err := dc.DecompressBlock(summed, dst, WithChecksum(true)); err != nil || !bytes.Equal(dst[:m], src) {
		t.Fatalf("both on: %v", err)
	}
	// Sides that disagree: a written checksum goes unchecked, a missing one fails.
	if m, err := dc.DecompressBlock(summed, dst); err != nil || !bytes.Equal(dst[:m], src) {
		t.Fatalf("written, not verified: %v", err)
	}
	if _, err := dc.DecompressBlock(plain, dst, WithChecksum(true)); err == nil {
		t.Fatal("verified with none written: want an error")
	}
}

// A per-call dictionary must not pair with the creation-time table: a block has
// to decode with exactly the options given to compress it.
func TestBlockPerCallDictDropsCreationTable(t *testing.T) {
	// Literal-heavy text with a skewed alphabet, so the shared table is used.
	text := func(seed uint32, n int) []byte {
		const alphabet = "eeeeeeeeeeaaaaaaiiiioooonnnrrsstl"
		b := make([]byte, n)
		for i, x := 0, seed; i < n; i++ {
			x = x*1664525 + 1013904223
			b[i] = alphabet[int(x>>16)%len(alphabet)]
		}
		return b
	}
	var samples [][]byte
	for i := 0; i < 64; i++ {
		samples = append(samples, text(uint32(i+1), 2048))
	}
	first, err := TrainDictionary(samples)
	if err != nil {
		t.Fatal(err)
	}
	second, err := TrainDict(samples[:32], 4096)
	if err != nil {
		t.Fatal(err)
	}
	src := text(999, 1024)
	block := make([]byte, CompressBlockBound(len(src)))
	out := make([]byte, DecompressBlockBound(len(src)))

	newC := func(opts ...Option) *Cctx {
		t.Helper()
		c, err := NewCctx(append([]Option{WithLevel(LevelUltra)}, opts...)...)
		if err != nil {
			t.Fatal(err)
		}
		t.Cleanup(func() { c.Close() })
		return c
	}
	newD := func(opts ...Option) *Dctx {
		t.Helper()
		d, err := NewDctx(opts...)
		if err != nil {
			t.Fatal(err)
		}
		t.Cleanup(func() { d.Close() })
		return d
	}
	decodes := func(what string, d *Dctx, n int, opts ...Option) {
		t.Helper()
		if m, err := d.DecompressBlock(block[:n], out, opts...); err != nil || !bytes.Equal(out[:m], src) {
			t.Fatalf("%s: %v", what, err)
		}
	}

	createdC, createdD, bareD := newC(WithDictionary(first)), newD(WithDictionary(first)), newD()

	// Compression: the table stays out of the block.
	n, err := createdC.CompressBlock(src, block, WithDict(second))
	if err != nil {
		t.Fatal(err)
	}
	decodes("Cctx: per-call dictionary alone", bareD, n, WithDict(second))

	// Decompression: a block needing a table is refused without the call's own.
	if n, err = newC().CompressBlock(src, block, WithDict(second), WithDictHuf(first.Huf())); err != nil {
		t.Fatal(err)
	}
	if _, err := createdD.DecompressBlock(block[:n], out, WithDict(second)); !errors.Is(err, ErrDictRequired) {
		t.Fatalf("Dctx: per-call dictionary paired with the creation-time table: %v", err)
	}
	decodes("Dctx: per-call pair", createdD, n, WithDict(second), WithDictHuf(first.Huf()))

	// No per-call dictionary: the creation-time pair applies, on both sides.
	if n, err = createdC.CompressBlock(src, block); err != nil {
		t.Fatal(err)
	}
	decodes("creation-time pair", createdD, n)
	if _, err := bareD.DecompressBlock(block[:n], out, WithDict(first.Content())); !errors.Is(err, ErrDictRequired) {
		t.Fatalf("creation-time table was not used: %v", err)
	}

	// A table alone keeps the creation-time dictionary.
	plainHuf, err := TrainDictHuf(samples, first.Content())
	if err != nil {
		t.Fatal(err)
	}
	dictOnly := newC(WithDict(first.Content()))
	if n, err = dictOnly.CompressBlock(src, block, WithDictHuf(plainHuf)); err != nil {
		t.Fatal(err)
	}
	decodes("per-call table", newD(WithDict(first.Content())), n, WithDictHuf(plainHuf))
}

func TestFileSameDeviceIsNotRefused(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("no device to read back and write on every Windows runner")
	}
	if _, err := CompressFile(os.DevNull, os.DevNull); err != nil {
		t.Fatalf("CompressFile on the null device: %v", err)
	}
}
