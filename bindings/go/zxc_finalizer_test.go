// SPDX-License-Identifier: BSD-3-Clause
// ZXC - High-performance lossless compression
//
// Copyright (c) Bertrand Lebonnois and contributors.

package zxc

import (
	"bytes"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sync/atomic"
	"testing"
	"time"
)

// Regression tests for handles dropped without Close: a finalizer must release
// what Close releases. Before the fix, every native context leaked, Open leaked
// its FILE*, OpenReader kept its reader reachable forever, and OpenBytes crashed
// the process once the garbage collector found its Pinner still pinned.

// settle runs the garbage collector until done reports true or 5s elapse.
// Finalizers run on their own goroutine after a cycle, and a finalizer that
// releases a reference frees its target only on a later cycle.
func settle(done func() bool) bool {
	deadline := time.Now().Add(5 * time.Second)
	for {
		runtime.GC()
		time.Sleep(10 * time.Millisecond)
		if done() || time.Now().After(deadline) {
			return done()
		}
	}
}

// settleFor runs the collector through n cycles, for checks with no condition.
func settleFor(n int) {
	for i := 0; i < n; i++ {
		runtime.GC()
		time.Sleep(10 * time.Millisecond)
	}
}

func seekableArchive(t *testing.T) []byte {
	t.Helper()
	data, err := Compress(bytes.Repeat([]byte("finalizer "), 8192), WithSeekable(true))
	if err != nil {
		t.Fatal(err)
	}
	return data
}

const finalizerChildEnv = "ZXC_FINALIZER_CHILD"

// runChild reruns test in a child process with extra environment and fails t
// with the child's output if the child fails.
func runChild(t *testing.T, test, role string, env ...string) {
	t.Helper()
	cmd := exec.Command(os.Args[0], "-test.run=^"+test+"$", "-test.count=1", "-test.v")
	cmd.Env = append(append(os.Environ(), finalizerChildEnv+"="+role), env...)
	out, err := cmd.CombinedOutput()
	if err != nil {
		t.Fatalf("child failed: %v\n%s", err, out)
	}
	t.Logf("child output:\n%s", out)
}

// The Pinner panics from a finalizer goroutine, which no recover can catch, so
// the dropped handle lives in a child process whose exit status is the result.
func TestFinalizerOpenBytesUnclosed(t *testing.T) {
	if os.Getenv(finalizerChildEnv) == "openbytes" {
		data := seekableArchive(t)
		func() {
			s, err := OpenBytes(data)
			if err != nil {
				t.Fatal(err)
			}
			if s.NumBlocks() == 0 {
				t.Fatal("no blocks")
			}
		}()
		settleFor(10)
		return
	}
	runChild(t, "TestFinalizerOpenBytesUnclosed", "openbytes")
}

type trackedReader struct {
	io.ReaderAt
}

func TestFinalizerOpenReaderUnclosed(t *testing.T) {
	data := seekableArchive(t)
	const n = 16
	var collected atomic.Int32
	for i := 0; i < n; i++ {
		r := &trackedReader{bytes.NewReader(data)}
		runtime.SetFinalizer(r, func(*trackedReader) { collected.Add(1) })
		s, err := OpenReader(r, int64(len(data)))
		if err != nil {
			t.Fatal(err)
		}
		_ = s
	}
	if !settle(func() bool { return collected.Load() == n }) {
		t.Fatalf("readers behind dropped handles never collected: %d/%d", collected.Load(), n)
	}
}

func TestFinalizerOpenUnclosed(t *testing.T) {
	path := filepath.Join(t.TempDir(), "a.zxc")
	if err := os.WriteFile(path, seekableArchive(t), 0o644); err != nil {
		t.Fatal(err)
	}
	before, ok := openHandles()
	if !ok {
		t.Skip("open handle count unavailable on " + runtime.GOOS)
	}
	const n = 32
	for i := 0; i < n; i++ {
		s, err := Open(path)
		if err != nil {
			t.Fatal(err)
		}
		_ = s
	}
	var after int
	settle(func() bool { after, _ = openHandles(); return after < before+n/2 })
	if after >= before+n/2 {
		t.Fatalf("files behind dropped handles never closed: %d open handles, %d before", after, before)
	}
	// Windows locks an open file against deletion.
	if err := os.Remove(path); err != nil {
		t.Fatalf("archive still in use after its handles were dropped: %v", err)
	}
}

func TestFinalizerNativeMemory(t *testing.T) {
	if testing.Short() {
		t.Skip("allocates about 100 MiB of native memory")
	}
	if _, ok := committedBytes(); !ok {
		t.Skip("committed memory unavailable on " + runtime.GOOS)
	}
	// glibc keeps memory freed from another thread (the finalizer's) in its
	// arenas, which reads as a leak. The child pins the mmap threshold so that
	// every large buffer is unmapped on free; other C runtimes ignore this.
	if os.Getenv(finalizerChildEnv) != "memory" {
		runChild(t, "TestFinalizerNativeMemory", "memory",
			"MALLOC_MMAP_THRESHOLD_=131072", "MALLOC_ARENA_MAX=1")
		return
	}

	src := make([]byte, 1<<20)
	for i := range src {
		src[i] = byte(i * 7 / 5)
	}
	block := make([]byte, CompressBlockBound(len(src)))
	cctx, err := NewCctx()
	if err != nil {
		t.Fatal(err)
	}
	m, err := cctx.CompressBlock(src, block)
	cctx.Close()
	if err != nil {
		t.Fatal(err)
	}
	block = block[:m]
	frame, err := Compress(src)
	if err != nil {
		t.Fatal(err)
	}
	dst := make([]byte, DecompressBlockBound(len(src)))
	cbuf := make([]byte, len(block)+(1<<20))

	// Each constructor puts its handle to work so that the native buffers
	// sized by the input are allocated, then returns it unclosed.
	cases := []struct {
		name string
		open func(t *testing.T) io.Closer
	}{
		{"Cctx", func(t *testing.T) io.Closer {
			c, err := NewCctx()
			if err != nil {
				t.Fatal(err)
			}
			if _, err := c.CompressBlock(src, cbuf); err != nil {
				t.Fatal(err)
			}
			return c
		}},
		{"Dctx", func(t *testing.T) io.Closer {
			d, err := NewDctx()
			if err != nil {
				t.Fatal(err)
			}
			if _, err := d.DecompressBlock(block, dst); err != nil {
				t.Fatal(err)
			}
			return d
		}},
		{"CStream", func(t *testing.T) io.Closer {
			c, err := NewCStream()
			if err != nil {
				t.Fatal(err)
			}
			if _, _, _, err := c.Compress(cbuf, src); err != nil {
				t.Fatal(err)
			}
			return c
		}},
		{"DStream", func(t *testing.T) io.Closer {
			d, err := NewDStream()
			if err != nil {
				t.Fatal(err)
			}
			if _, _, err := d.Decompress(dst, frame); err != nil {
				t.Fatal(err)
			}
			return d
		}},
		{"Writer", func(t *testing.T) io.Closer {
			w, err := NewWriter(io.Discard)
			if err != nil {
				t.Fatal(err)
			}
			if _, err := w.Write(src); err != nil {
				t.Fatal(err)
			}
			return w
		}},
		{"Reader", func(t *testing.T) io.Closer {
			r, err := NewReader(bytes.NewReader(frame))
			if err != nil {
				t.Fatal(err)
			}
			if _, err := io.ReadFull(r, dst[:4096]); err != nil {
				t.Fatal(err)
			}
			return r
		}},
	}

	// round drops n handles and lets the collector settle.
	round := func(t *testing.T, n int, open func(*testing.T) io.Closer) {
		for i := 0; i < n; i++ {
			_ = open(t)
		}
		settleFor(10)
	}

	// A first round settles the allocator; the second must leave nothing.
	// 32 dropped handles leaked about 60 to 130 MiB per round before the fix;
	// the slack absorbs allocator noise.
	const n = 32
	const slack = 32 << 20
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			round(t, n, tc.open)
			before, _ := committedBytes()
			round(t, n, tc.open)
			after, _ := committedBytes()
			t.Logf("%d dropped handles: %+.1f MiB", n, float64(after-before)/(1<<20))
			if after-before > slack {
				t.Fatalf("dropped handles leaked %.1f MiB of native memory",
					float64(after-before)/(1<<20))
			}
		})
	}
}
