//go:build unix

package zxc

/*
#include <unistd.h>
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
// reading. The returned FILE* owns its own fd and must be closed with
// C.fclose().
func dupFileRead(f *os.File) (*C.FILE, error) {
	fd := C.int(f.Fd())
	dupFd := C.dup(fd)
	runtime.KeepAlive(f)
	if dupFd < 0 {
		return nil, fmt.Errorf("zxc: dup failed for read fd")
	}

	cFile := C.fdopen(dupFd, cModeRead)
	if cFile == nil {
		C.close(dupFd)
		return nil, fmt.Errorf("zxc: fdopen failed for read fd")
	}
	return cFile, nil
}

// dupFileWrite duplicates a Go *os.File's fd and wraps it in a C FILE* for
// writing. The returned FILE* owns its own fd and must be closed with
// C.fclose().
func dupFileWrite(f *os.File) (*C.FILE, error) {
	fd := C.int(f.Fd())
	dupFd := C.dup(fd)
	runtime.KeepAlive(f)
	if dupFd < 0 {
		return nil, fmt.Errorf("zxc: dup failed for write fd")
	}

	cFile := C.fdopen(dupFd, cModeWrite)
	if cFile == nil {
		C.close(dupFd)
		return nil, fmt.Errorf("zxc: fdopen failed for write fd")
	}
	return cFile, nil
}
