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
)

// C mode strings - allocated once, never freed (intentional; they live for the
// process lifetime and avoid per-call C.CString/C.free overhead).
var (
	cModeRead  = C.CString("rb")
	cModeWrite = C.CString("wb")
)

// dupFileRead converts a Go *os.File to a C FILE* for reading on Windows.
//
// os.File.Fd() returns a Windows HANDLE (not a CRT fd), so we must use
// _open_osfhandle() to wrap it as a CRT fd, then _dup() to own it
// independently, since _open_osfhandle() transfers ownership of the HANDLE.
func dupFileRead(f *os.File) (*C.FILE, error) {
	handle := C.intptr_t(f.Fd())
	runtime.KeepAlive(f)

	// Wrap the HANDLE as a read-only CRT fd.
	crtFd := C._open_osfhandle(handle, C._O_RDONLY)
	if crtFd < 0 {
		return nil, fmt.Errorf("zxc: _open_osfhandle failed for read fd")
	}

	// Dup so we own this fd independently from Go's *os.File.
	// Do NOT close crtFd: _open_osfhandle transfers HANDLE ownership to the
	// CRT; closing it would also close the HANDLE still owned by Go.
	dupFd := C._dup(crtFd)
	if dupFd < 0 {
		return nil, fmt.Errorf("zxc: _dup failed for read fd")
	}

	cFile := C._fdopen(dupFd, cModeRead)
	if cFile == nil {
		C._close(dupFd)
		return nil, fmt.Errorf("zxc: _fdopen failed for read fd")
	}
	return cFile, nil
}

// dupFileWrite converts a Go *os.File to a C FILE* for writing on Windows.
func dupFileWrite(f *os.File) (*C.FILE, error) {
	handle := C.intptr_t(f.Fd())
	runtime.KeepAlive(f)

	crtFd := C._open_osfhandle(handle, C._O_WRONLY)
	if crtFd < 0 {
		return nil, fmt.Errorf("zxc: _open_osfhandle failed for write fd")
	}

	dupFd := C._dup(crtFd)
	if dupFd < 0 {
		return nil, fmt.Errorf("zxc: _dup failed for write fd")
	}

	cFile := C._fdopen(dupFd, cModeWrite)
	if cFile == nil {
		C._close(dupFd)
		return nil, fmt.Errorf("zxc: _fdopen failed for write fd")
	}
	return cFile, nil
}
