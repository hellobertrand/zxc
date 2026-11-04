// SPDX-License-Identifier: BSD-3-Clause
// ZXC - High-performance lossless compression
//
// Copyright (c) Bertrand Lebonnois and contributors.

package zxc

/*
#include <stddef.h>
#include <stdint.h>
#include "zxc_error.h"
*/
import "C"

import (
	"errors"
	"io"
	"runtime/cgo"
	"unsafe"
)

// zxcGoSeekableReadAt is the C->Go trampoline invoked by the library for
// every positional read against a user-supplied io.ReaderAt. It must not
// panic across the FFI boundary; any error is mapped to ZXC_ERROR_IO, and a
// panic in ReadAt is recovered into the seekReader for the Go caller to
// re-raise.
//
// This trampoline lives in its own file, whose preamble contains no
// declaration of zxcGoSeekableReadAt. cgo copies the preamble of any file
// carrying an //export directive into _cgo_export.c, right before the
// generated declaration that marks the symbol __declspec(dllexport) on
// Windows. A hand-written forward declaration in that same preamble would
// then precede the dllexport one, which clang on windows/arm64 rejects
// ("redeclaration ... cannot add 'dllexport' attribute"). The forward
// declaration needed by the reader glue stays in zxc_seekable.go — a file
// without //export, so its preamble never reaches _cgo_export.c.
//
//export zxcGoSeekableReadAt
func zxcGoSeekableReadAt(ctx unsafe.Pointer, dst unsafe.Pointer, length C.size_t, offset C.uint64_t) (ret C.int64_t) {
	h := cgo.Handle(uintptr(ctx))
	rc, ok := h.Value().(*seekReader)
	if !ok || dst == nil {
		return C.int64_t(C.ZXC_ERROR_IO)
	}
	defer func() {
		if pv := recover(); pv != nil {
			rc.panicked, rc.pv = true, pv
			ret = C.int64_t(C.ZXC_ERROR_IO)
		}
	}()
	// Map the C-owned destination region as a Go slice without copying.
	buf := unsafe.Slice((*byte)(dst), int(length))
	n, err := rc.r.ReadAt(buf, int64(offset))
	// io.ReaderAt explicitly allows (n == len(buf), io.EOF) when the read
	// ends exactly at EOF — which the footer and seek-table reads always do.
	if n != int(length) || (err != nil && !errors.Is(err, io.EOF)) {
		return C.int64_t(C.ZXC_ERROR_IO)
	}
	return C.int64_t(length)
}
