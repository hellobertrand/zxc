// SPDX-License-Identifier: BSD-3-Clause
// ZXC - High-performance lossless compression
//
// Copyright (c) Bertrand Lebonnois and contributors.

package zxc

/*
#include <stdio.h>
#include "zxc_stream.h"
*/
import "C"
import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sync/atomic"
)

// outputFile is the destination of a file pipeline. A regular or new output
// is written to a sibling temporary file renamed over it on success, so a
// failed call leaves the previous content intact. It is written in place when
// that is impossible: a device or FIFO, or a directory that refuses the
// temporary file.
type outputFile struct {
	*os.File
	final string // rename target; empty once in place
}

var tmpSeq atomic.Uint32

func createOutput(path string) (*outputFile, error) {
	if p, err := filepath.EvalSymlinks(path); err == nil {
		path = p // replace a link's target, not the link
	}
	// Opening the existing output fails where os.Create would, tells a
	// device or FIFO apart, and gives the mode the replacement keeps.
	exists := false
	var mode os.FileMode
	if f, err := os.OpenFile(path, os.O_WRONLY, 0); err == nil {
		st, err := f.Stat()
		if err == nil && !st.Mode().IsRegular() {
			return &outputFile{File: f}, nil
		}
		f.Close()
		if err != nil {
			return nil, err
		}
		exists, mode = true, st.Mode().Perm()
	} else if !errors.Is(err, os.ErrNotExist) {
		return nil, err
	}
	for {
		tmp := fmt.Sprintf("%s.%d-%d.zxc-tmp", path, os.Getpid(), tmpSeq.Add(1))
		f, err := os.OpenFile(tmp, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o666)
		switch {
		case errors.Is(err, os.ErrExist):
			continue
		case errors.Is(err, os.ErrPermission) && exists:
			// The directory refuses a sibling: truncate the output in place.
			f, err = os.OpenFile(path, os.O_WRONLY|os.O_TRUNC, 0)
			if err != nil {
				return nil, err
			}
			return &outputFile{File: f}, nil
		case err != nil:
			return nil, err
		}
		out := &outputFile{File: f, final: path}
		if exists {
			if err := f.Chmod(mode); err != nil {
				out.discard()
				return nil, err
			}
		}
		return out, nil
	}
}

// commit closes the output and, for a temporary file, renames it into place.
func (o *outputFile) commit() error {
	if err := o.Close(); err != nil {
		return err
	}
	if o.final != "" {
		if err := os.Rename(o.Name(), o.final); err != nil {
			return err
		}
		o.final = ""
	}
	return nil
}

// discard closes an uncommitted output and removes its temporary file.
func (o *outputFile) discard() {
	o.Close()
	if o.final != "" {
		os.Remove(o.Name())
	}
}

// ============================================================================
// Streaming API (file-based)
// ============================================================================

// CompressFile compresses src to dst using multi-threaded streaming.
//
// This is the recommended method for large files. It uses an asynchronous
// pipeline with separate reader, worker, and writer threads.
//
// Options: [WithLevel], [WithChecksum], [WithThreads], [WithSeekable],
// [WithDict].
//
//	n, err := zxc.CompressFile("input.bin", "output.zxc",
//	    zxc.WithLevel(zxc.LevelCompact), zxc.WithThreads(4))
func CompressFile(input, output string, opts ...Option) (int64, error) {
	o := applyOptions(opts)

	// The pipeline reads its parameters from a configured context, built
	// first so a refused option touches no file.
	cctx, err := createCctx(o)
	if err != nil {
		return 0, err
	}
	defer C.zxc_free_cctx(cctx)

	fIn, err := os.Open(input)
	if err != nil {
		return 0, fmt.Errorf("zxc: open input: %w", err)
	}
	defer fIn.Close()

	fOut, err := createOutput(output)
	if err != nil {
		return 0, fmt.Errorf("zxc: create output: %w", err)
	}
	defer fOut.discard()

	// Duplicate file descriptors so the C FILE* owns its own fd.
	cIn, err := dupFileRead(fIn)
	if err != nil {
		return 0, err
	}
	defer C.fclose(cIn)

	cOut, err := dupFileWrite(fOut.File)
	if err != nil {
		return 0, err
	}
	outClosed := false
	defer func() {
		if !outClosed {
			C.fclose(cOut)
		}
	}()

	result := C.zxc_stream_compress(cIn, cOut, cctx)

	// fclose flushes the last stdio buffer of compressed output; a failure
	// here (ENOSPC, EIO) means the archive tail never reached the OS, so it
	// must be reported instead of being swallowed by a deferred close.
	outClosed = true
	closeRC := C.fclose(cOut)
	if result < 0 {
		return 0, errorFromCode(result)
	}
	if closeRC != 0 {
		return 0, ErrIO
	}
	if err := fOut.commit(); err != nil {
		return 0, fmt.Errorf("zxc: write output: %w", err)
	}
	return int64(result), nil
}

// FileDecompressedSize reads the original uncompressed size from the footer
// of a ZXC compressed file without performing decompression. The file is
// opened in "rb" mode; the underlying file position is restored internally.
func FileDecompressedSize(path string) (int64, error) {
	f, err := os.Open(path)
	if err != nil {
		return 0, fmt.Errorf("zxc: open input: %w", err)
	}
	defer f.Close()

	cf, err := dupFileRead(f)
	if err != nil {
		return 0, err
	}
	defer C.fclose(cf)

	result := C.zxc_stream_get_decompressed_size(cf)
	if result < 0 {
		return 0, errorFromCode(result)
	}
	return int64(result), nil
}

// DecompressFile decompresses src to dst using multi-threaded streaming.
//
// Options: [WithChecksum], [WithThreads], [WithDict].
//
//	n, err := zxc.DecompressFile("compressed.zxc", "output.bin")
func DecompressFile(input, output string, opts ...Option) (int64, error) {
	o := applyOptions(opts)

	// See CompressFile.
	dctx, err := createDctx(o)
	if err != nil {
		return 0, err
	}
	defer C.zxc_free_dctx(dctx)

	fIn, err := os.Open(input)
	if err != nil {
		return 0, fmt.Errorf("zxc: open input: %w", err)
	}
	defer fIn.Close()

	fOut, err := createOutput(output)
	if err != nil {
		return 0, fmt.Errorf("zxc: create output: %w", err)
	}
	defer fOut.discard()

	cIn, err := dupFileRead(fIn)
	if err != nil {
		return 0, err
	}
	defer C.fclose(cIn)

	cOut, err := dupFileWrite(fOut.File)
	if err != nil {
		return 0, err
	}
	outClosed := false
	defer func() {
		if !outClosed {
			C.fclose(cOut)
		}
	}()

	result := C.zxc_stream_decompress(cIn, cOut, dctx)

	// See CompressFile: the fclose flush result is part of the success path.
	outClosed = true
	closeRC := C.fclose(cOut)
	if result < 0 {
		return 0, errorFromCode(result)
	}
	if closeRC != 0 {
		return 0, ErrIO
	}
	if err := fOut.commit(); err != nil {
		return 0, fmt.Errorf("zxc: write output: %w", err)
	}
	return int64(result), nil
}
