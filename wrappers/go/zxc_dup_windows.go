//go:build windows

package zxc

/*
#include <io.h>
#include <stdio.h>
*/
import "C"
import (
	"fmt"
	"os"
	"runtime"
)

// C mode strings — allocated once, never freed (intentional; they live for the
// process lifetime and avoid per-call C.CString/C.free overhead).
var (
	cModeRead  = C.CString("rb")
	cModeWrite = C.CString("wb")
)

// dupFileRead duplicates a Go *os.File's fd and wraps it in a C FILE* for
// reading. Uses Windows CRT _dup/_fdopen/_close.
func dupFileRead(f *os.File) (*C.FILE, error) {
	fd := C.int(f.Fd())
	dupFd := C._dup(fd)
	runtime.KeepAlive(f)
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

// dupFileWrite duplicates a Go *os.File's fd and wraps it in a C FILE* for
// writing. Uses Windows CRT _dup/_fdopen/_close.
func dupFileWrite(f *os.File) (*C.FILE, error) {
	fd := C.int(f.Fd())
	dupFd := C._dup(fd)
	runtime.KeepAlive(f)
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
