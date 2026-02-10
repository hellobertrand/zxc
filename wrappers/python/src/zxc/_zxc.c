/*
 * Copyright (c) 2025-2026, Bertrand Lebonnois
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#define PY_SSIZE_T_CLEAN

#include "zxc.h"
#include <Python.h>

#define Py_Return_Errno(err)                                                   \
    do {                                                                       \
        PyErr_SetFromErrno(err);                                               \
        return NULL;                                                           \
    } while (0)
#define Py_Return_Err(err, str)                                                \
    do {                                                                       \
        PyErr_SetString(err, str);                                             \
        return NULL;                                                           \
    } while (0)

// =============================================================================
// Platform
// =============================================================================

static inline int zxc_dup(int fd) {
#ifdef _WIN32
    return _dup(fd);
#else
    return dup(fd);
#endif
}

static inline FILE *zxc_fdopen(int fd, const char *mode) {
#ifdef _WIN32
    return _fdopen(fd, mode);
#else
    return fdopen(fd, mode);
#endif
}

static inline int zxc_close(int fd) {
#ifdef _WIN32
    return _close(fd);
#else
    return close(fd);
#endif
}

// =============================================================================
// Wrapper functions
// =============================================================================

static PyObject *pyzxc_compress(PyObject *self, PyObject *args,
                                PyObject *kwargs);
static PyObject *pyzxc_decompress(PyObject *self, PyObject *args,
                                  PyObject *kwargs);
static PyObject *pyzxc_stream_compress(PyObject *self, PyObject *args,
                                       PyObject *kwargs);
static PyObject *pyzxc_stream_decompress(PyObject *self, PyObject *args,
                                         PyObject *kwargs);
static PyObject *pyzxc_get_decompressed_size(PyObject *self, PyObject *args,
                                         PyObject *kwargs);

// =============================================================================
// Initialize python module
// =============================================================================

PyDoc_STRVAR(zxc_doc,
"ZXC: High-performance, lossless asymmetric compression.\n"
"\n"
"Functions:\n"
"  compress(data: bytes, level: int = LEVEL_DEFAULT, checksum: bool = False) -> bytes\n"
"      Compress a bytes object.\n"
"\n"
"  decompress(data: bytes, decompress_size: int, checksum: bool = False) -> bytes\n"
"      Decompress a bytes object to its original size.\n"
"\n"
"  stream_compress(src: file-like, dst: file-like, n_threads: int = 0, level: int = LEVEL_DEFAULT, checksum: bool = False) -> None\n"
"      Compress data from a readable file-like object to a writable file-like object.\n"
"\n"
"  stream_decompress(src: file-like, dst: file-like, n_threads: int = 0, checksum: bool = False) -> None\n"
"      Decompress data from a readable file-like object to a writable file-like object.\n"
"\n"
"Notes:\n"
"  - File-like objects must support fileno(), readable(), and writable().\n"
"  - Stream functions release the GIL for multi-threaded compression/decompression.\n"
);

static PyMethodDef zxc_methods[] = {
    {"pyzxc_compress", (PyCFunction)pyzxc_compress, METH_VARARGS | METH_KEYWORDS, NULL},
    {"pyzxc_decompress", (PyCFunction)pyzxc_decompress, METH_VARARGS | METH_KEYWORDS, NULL},
    {"pyzxc_stream_compress", (PyCFunction)pyzxc_stream_compress, METH_VARARGS | METH_KEYWORDS, NULL},
    {"pyzxc_stream_decompress", (PyCFunction)pyzxc_stream_decompress, METH_VARARGS | METH_KEYWORDS, NULL},
    {"pyzxc_get_decompressed_size", (PyCFunction)pyzxc_get_decompressed_size, METH_VARARGS | METH_KEYWORDS, NULL},
    {NULL, NULL, 0, NULL}  // sentinel
};

static struct PyModuleDef zxc_module = {PyModuleDef_HEAD_INIT, "_zxc", zxc_doc,
                                        0, zxc_methods};

PyMODINIT_FUNC PyInit__zxc(void) { 
    PyObject *m = PyModule_Create(&zxc_module);
    if (!m)
        return NULL;

    PyModule_AddIntConstant(m, "LEVEL_FASTEST",  ZXC_LEVEL_FASTEST);
    PyModule_AddIntConstant(m, "LEVEL_FAST",     ZXC_LEVEL_FAST);
    PyModule_AddIntConstant(m, "LEVEL_DEFAULT",  ZXC_LEVEL_DEFAULT);
    PyModule_AddIntConstant(m, "LEVEL_BALANCED", ZXC_LEVEL_BALANCED);
    PyModule_AddIntConstant(m, "LEVEL_COMPACT",  ZXC_LEVEL_COMPACT);
    return m; 
}

// =============================================================================
// Functions definitions
// =============================================================================

static PyObject *pyzxc_compress(PyObject *self, PyObject *args,
                                PyObject *kwargs) {
    Py_buffer view;
    int level = ZXC_LEVEL_DEFAULT;
    int checksum = 0;

    static char *kwlist[] = {"data", "level", "checksum", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "y*|ip", kwlist, &view,
                                     &level, &checksum)) {
        return NULL;
    }

    if (view.itemsize != 1) {
        PyBuffer_Release(&view);
        PyErr_SetString(PyExc_TypeError,
                        "expected a byte buffer (itemsize==1)");
        return NULL;
    }

    size_t src_size = (size_t)view.len;

    size_t bound = zxc_compress_bound(src_size);

    PyObject *out = PyBytes_FromStringAndSize(NULL, (Py_ssize_t)bound);
    if (!out) {
        PyBuffer_Release(&view);
        return NULL;
    }

    char *dst = PyBytes_AsString(out); // Return a pointer to the contents
    size_t nwritten;                    // The number of bytes written to dst

    Py_BEGIN_ALLOW_THREADS
    nwritten = zxc_compress(view.buf, // Source buffer
                            src_size, // Source size
                            dst,      // Destination buffer
                            bound,    // Destination capacity
                            level,    // Compression level
                            checksum  // Checksum
    );
    Py_END_ALLOW_THREADS

    PyBuffer_Release(&view);

    if (nwritten == 0) {
        Py_DECREF(out);
        if (src_size == 0)
            Py_Return_Err(PyExc_ValueError, "input is too small to be compressed");
        else 
            Py_Return_Err(PyExc_RuntimeError, "an error occurred");
    }

    if (_PyBytes_Resize(&out, (Py_ssize_t)nwritten) < 0) // Realloc
        return NULL;                                 

    return out;
}

static PyObject *pyzxc_get_decompressed_size(PyObject *self, PyObject *args,
                                  PyObject *kwargs){
    Py_buffer view;
    
    if (!PyArg_ParseTuple(args, "y*", &view)) {
        return NULL;
    }

    size_t n = zxc_get_decompressed_size(view.buf, view.len);
    
    PyBuffer_Release(&view); 
    
    return Py_BuildValue("k", n);
}

static PyObject *pyzxc_decompress(PyObject *self, PyObject *args,
                                  PyObject *kwargs) {
    Py_buffer view;
    int checksum = 0;

    Py_ssize_t decompress_size;
    static char *kwlist[] = {"data", "decompress_size", "checksum", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "y*n|p", kwlist, &view,
                                     &decompress_size, &checksum)) {
        return NULL;
    }

    if (view.itemsize != 1) {
        PyBuffer_Release(&view);
        PyErr_SetString(PyExc_TypeError,
                        "expected a byte buffer (itemsize==1)");
        return NULL;
    }

    size_t src_size = (size_t)view.len;
    
    PyObject *out = PyBytes_FromStringAndSize(NULL, (Py_ssize_t)decompress_size);
    if (!out) {
        PyBuffer_Release(&view);
        return NULL;
    }

    char *dst = PyBytes_AsString(out); // Return a pointer to the contents
    size_t nwritten;                   // The number of bytes written to dst

    Py_BEGIN_ALLOW_THREADS
    nwritten = zxc_decompress(view.buf,      // Source buffer
                                src_size,      // Source size
                                dst,           // Destination buffer
                                decompress_size, // Destination capacity
                                checksum       // Verify checksum
    );
    Py_END_ALLOW_THREADS

    PyBuffer_Release(&view);

    if (nwritten == 0) {
        Py_DECREF(out);
        if (src_size == 0)
            Py_Return_Err(PyExc_ValueError, "invalid header");
        else 
            Py_Return_Err(PyExc_RuntimeError, "data corrupted or invalid header");
    }
    
    return out;
}

static PyObject *pyzxc_stream_compress(PyObject *self, PyObject *args,
                                       PyObject *kwargs) {
    PyObject *src, *dst;
    int nthreads = 0;
    int level = ZXC_LEVEL_DEFAULT;
    int checksum = 0;

    static char *kwlist[] = {"src",   "dst",      "n_threads",
                             "level", "checksum", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|iip", kwlist, &src, &dst,
                                     &nthreads, &level, &checksum)) {
        return NULL;
    }

    int src_fd = PyObject_AsFileDescriptor(src);
    int dst_fd = PyObject_AsFileDescriptor(dst);

    if (src_fd == -1 || dst_fd == -1)
        Py_Return_Err(PyExc_RuntimeError, "couldn't get file descriptor");

    int src_dup = zxc_dup(src_fd);
    if (src_dup == -1) {
        Py_Return_Errno(PyExc_OSError);
    }
    int dst_dup = zxc_dup(dst_fd);
    if (dst_dup == -1) {
        zxc_close(src_dup);
        Py_Return_Errno(PyExc_OSError);
    }

    FILE *fsrc = zxc_fdopen(src_dup, "rb");
    if (!fsrc) {
        zxc_close(src_dup);
        zxc_close(dst_dup);
        Py_Return_Errno(PyExc_OSError);
    }

    FILE *fdst = zxc_fdopen(dst_dup, "wb");
    if (!fdst) {
        fclose(fsrc);
        zxc_close(dst_dup);
        Py_Return_Errno(PyExc_OSError);
    }

    int64_t nwritten;

    Py_BEGIN_ALLOW_THREADS 
    nwritten = zxc_stream_compress(fsrc, fdst, nthreads, level, checksum);
    Py_END_ALLOW_THREADS

    fclose(fdst);
    fclose(fsrc);

    if (nwritten < 0)
        Py_Return_Err(PyExc_RuntimeError, "an error occurred");

    Py_BuildValue("L", nwritten);
}

static PyObject *pyzxc_stream_decompress(PyObject *self, PyObject *args,
                                         PyObject *kwargs) {
    PyObject *src, *dst;
    int nthreads = 0;
    int checksum = 0;

    static char *kwlist[] = {"src", "dst", "n_threads", "checksum", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|ip", kwlist, &src, &dst,
                                     &nthreads, &checksum)) {
        return NULL;
    }

    int src_fd = PyObject_AsFileDescriptor(src);
    int dst_fd = PyObject_AsFileDescriptor(dst);

    if (src_fd == -1 || dst_fd == -1)
        Py_Return_Err(PyExc_RuntimeError, "couldn't get file descriptor");

    int src_dup = zxc_dup(src_fd);
    if (src_dup == -1) {
        Py_Return_Errno(PyExc_OSError);
    }
    int dst_dup = zxc_dup(dst_fd);
    if (dst_dup == -1) {
        zxc_close(src_dup);
        Py_Return_Errno(PyExc_OSError);
    }

    FILE *fsrc = zxc_fdopen(src_dup, "rb");
    if (!fsrc) {
        zxc_close(src_dup);
        zxc_close(dst_dup);
        Py_Return_Errno(PyExc_OSError);
    }

    FILE *fdst = zxc_fdopen(dst_dup, "wb");
    if (!fdst) {
        fclose(fsrc);
        zxc_close(dst_dup);
        Py_Return_Errno(PyExc_OSError);
    }

    int64_t nwritten;

    Py_BEGIN_ALLOW_THREADS 
    nwritten = zxc_stream_decompress(fsrc, fdst, nthreads, checksum);
    Py_END_ALLOW_THREADS

    fclose(fdst);
    fclose(fsrc);

    if (nwritten < 0)
        Py_Return_Err(PyExc_RuntimeError, "an error occurred");

    Py_BuildValue("L", nwritten);
}
