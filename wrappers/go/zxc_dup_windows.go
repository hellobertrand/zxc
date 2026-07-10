//go:build windows

package zxc

/*
#include <io.h>
#include <stdio.h>
#include <fcntl.h>
*/
import "C"
import (
	"fmt"
	"os"
	"runtime"
	"syscall"
)

// C mode strings - allocated once, never freed (intentional; they live for the
// process lifetime and avoid per-call C.CString/C.free overhead).
var (
	cModeRead  = C.CString("rb")
	cModeWrite = C.CString("wb")
)

// dupHandleToFile duplicates the Windows HANDLE owned by f, wraps the
// duplicate as a CRT fd, then opens a C FILE* over it. The FILE* owns the
// whole chain: fclose() closes the CRT fd, which closes the duplicated
// HANDLE. Go's *os.File and its HANDLE are never touched.
//
// Duplicating the HANDLE first (rather than _open_osfhandle on Go's HANDLE
// followed by _dup) matters: _open_osfhandle transfers HANDLE ownership to
// the CRT fd, which then could never be closed without also closing Go's
// HANDLE — permanently leaking one slot of the bounded CRT fd table per call.
func dupHandleToFile(f *os.File, oflag C.int, mode *C.char, what string) (*C.FILE, error) {
	proc, _ := syscall.GetCurrentProcess()
	var dup syscall.Handle
	err := syscall.DuplicateHandle(
		proc, syscall.Handle(f.Fd()), proc, &dup,
		0, false, syscall.DUPLICATE_SAME_ACCESS,
	)
	runtime.KeepAlive(f)
	if err != nil {
		return nil, fmt.Errorf("zxc: DuplicateHandle failed for %s fd: %w", what, err)
	}

	crtFd := C._open_osfhandle(C.intptr_t(dup), oflag)
	if crtFd < 0 {
		syscall.CloseHandle(dup)
		return nil, fmt.Errorf("zxc: _open_osfhandle failed for %s fd", what)
	}

	cFile := C._fdopen(crtFd, mode)
	if cFile == nil {
		C._close(crtFd) // also closes the duplicated HANDLE
		return nil, fmt.Errorf("zxc: _fdopen failed for %s fd", what)
	}
	return cFile, nil
}

// dupFileRead converts a Go *os.File to an independently-owned C FILE* for
// reading on Windows.
func dupFileRead(f *os.File) (*C.FILE, error) {
	return dupHandleToFile(f, C._O_RDONLY, cModeRead, "read")
}

// dupFileWrite converts a Go *os.File to an independently-owned C FILE* for
// writing on Windows.
func dupFileWrite(f *os.File) (*C.FILE, error) {
	return dupHandleToFile(f, C._O_WRONLY, cModeWrite, "write")
}
