/*
 * Copyright (c) 2025-2026, Bertrand Lebonnois
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#define PY_SSIZE_T_CLEAN

#include <Python.h>

#include "zxc.h"

#define Py_Return_Errno(err)     \
    do {                         \
        PyErr_SetFromErrno(err); \
        return NULL;             \
    } while (0)
#define Py_Return_Err(err, str)    \
    do {                           \
        PyErr_SetString(err, str); \
        return NULL;               \
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

static inline FILE* zxc_fdopen(int fd, const char* mode) {
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

static PyObject* pyzxc_compress(PyObject* self, PyObject* args, PyObject* kwargs);
static PyObject* pyzxc_decompress(PyObject* self, PyObject* args, PyObject* kwargs);
static PyObject* pyzxc_stream_compress(PyObject* self, PyObject* args, PyObject* kwargs);
static PyObject* pyzxc_stream_decompress(PyObject* self, PyObject* args, PyObject* kwargs);
static PyObject* pyzxc_get_decompressed_size(PyObject* self, PyObject* args, PyObject* kwargs);
static PyObject* pyzxc_min_level(PyObject* self, PyObject* args);
static PyObject* pyzxc_max_level(PyObject* self, PyObject* args);
static PyObject* pyzxc_default_level(PyObject* self, PyObject* args);
static PyObject* pyzxc_version_string(PyObject* self, PyObject* args);

static PyObject* pyzxc_cstream_create(PyObject* self, PyObject* args, PyObject* kwargs);
static PyObject* pyzxc_cstream_compress(PyObject* self, PyObject* args, PyObject* kwargs);
static PyObject* pyzxc_cstream_end(PyObject* self, PyObject* args, PyObject* kwargs);
static PyObject* pyzxc_cstream_in_size(PyObject* self, PyObject* args);
static PyObject* pyzxc_cstream_out_size(PyObject* self, PyObject* args);
static PyObject* pyzxc_cstream_free(PyObject* self, PyObject* args);

static PyObject* pyzxc_dstream_create(PyObject* self, PyObject* args, PyObject* kwargs);
static PyObject* pyzxc_dstream_decompress(PyObject* self, PyObject* args, PyObject* kwargs);
static PyObject* pyzxc_dstream_finished(PyObject* self, PyObject* args);
static PyObject* pyzxc_dstream_in_size(PyObject* self, PyObject* args);
static PyObject* pyzxc_dstream_out_size(PyObject* self, PyObject* args);
static PyObject* pyzxc_dstream_free(PyObject* self, PyObject* args);

// =============================================================================
// Initialize python module
// =============================================================================

PyDoc_STRVAR(
    zxc_doc,
    "ZXC: High-performance, lossless asymmetric compression.\n"
    "\n"
    "Functions:\n"
    "  compress(data: bytes, level: int = LEVEL_DEFAULT, checksum: bool = False) -> bytes\n"
    "      Compress a bytes object.\n"
    "\n"
    "  decompress(data: bytes, decompress_size: int, checksum: bool = False) -> bytes\n"
    "      Decompress a bytes object to its original size.\n"
    "\n"
    "  stream_compress(src: file-like, dst: file-like, n_threads: int = 0, level: int = "
    "LEVEL_DEFAULT, checksum: bool = False) -> None\n"
    "      Compress data from a readable file-like object to a writable file-like object.\n"
    "\n"
    "  stream_decompress(src: file-like, dst: file-like, n_threads: int = 0, checksum: bool = "
    "False) -> None\n"
    "      Decompress data from a readable file-like object to a writable file-like object.\n"
    "\n"
    "Notes:\n"
    "  - File-like objects must support fileno(), readable(), and writable().\n"
    "  - Stream functions release the GIL for multi-threaded compression/decompression.\n");

static PyMethodDef zxc_methods[] = {
    {"pyzxc_compress", (PyCFunction)pyzxc_compress, METH_VARARGS | METH_KEYWORDS, NULL},
    {"pyzxc_decompress", (PyCFunction)pyzxc_decompress, METH_VARARGS | METH_KEYWORDS, NULL},
    {"pyzxc_stream_compress", (PyCFunction)pyzxc_stream_compress, METH_VARARGS | METH_KEYWORDS,
     NULL},
    {"pyzxc_stream_decompress", (PyCFunction)pyzxc_stream_decompress, METH_VARARGS | METH_KEYWORDS,
     NULL},
    {"pyzxc_get_decompressed_size", (PyCFunction)pyzxc_get_decompressed_size,
     METH_VARARGS | METH_KEYWORDS, NULL},
    {"pyzxc_min_level", (PyCFunction)pyzxc_min_level, METH_NOARGS, NULL},
    {"pyzxc_max_level", (PyCFunction)pyzxc_max_level, METH_NOARGS, NULL},
    {"pyzxc_default_level", (PyCFunction)pyzxc_default_level, METH_NOARGS, NULL},
    {"pyzxc_version_string", (PyCFunction)pyzxc_version_string, METH_NOARGS, NULL},

    /* Push streaming API (single-threaded, caller-driven). */
    {"pyzxc_cstream_create", (PyCFunction)pyzxc_cstream_create, METH_VARARGS | METH_KEYWORDS, NULL},
    {"pyzxc_cstream_compress", (PyCFunction)pyzxc_cstream_compress, METH_VARARGS | METH_KEYWORDS,
     NULL},
    {"pyzxc_cstream_end", (PyCFunction)pyzxc_cstream_end, METH_VARARGS | METH_KEYWORDS, NULL},
    {"pyzxc_cstream_in_size", (PyCFunction)pyzxc_cstream_in_size, METH_O, NULL},
    {"pyzxc_cstream_out_size", (PyCFunction)pyzxc_cstream_out_size, METH_O, NULL},
    {"pyzxc_cstream_free", (PyCFunction)pyzxc_cstream_free, METH_O, NULL},

    {"pyzxc_dstream_create", (PyCFunction)pyzxc_dstream_create, METH_VARARGS | METH_KEYWORDS, NULL},
    {"pyzxc_dstream_decompress", (PyCFunction)pyzxc_dstream_decompress,
     METH_VARARGS | METH_KEYWORDS, NULL},
    {"pyzxc_dstream_finished", (PyCFunction)pyzxc_dstream_finished, METH_O, NULL},
    {"pyzxc_dstream_in_size", (PyCFunction)pyzxc_dstream_in_size, METH_O, NULL},
    {"pyzxc_dstream_out_size", (PyCFunction)pyzxc_dstream_out_size, METH_O, NULL},
    {"pyzxc_dstream_free", (PyCFunction)pyzxc_dstream_free, METH_O, NULL},

    {NULL, NULL, 0, NULL}  // sentinel
};

static struct PyModuleDef zxc_module = {PyModuleDef_HEAD_INIT, "_zxc", zxc_doc, 0, zxc_methods};

PyMODINIT_FUNC PyInit__zxc(void) {
    PyObject* m = PyModule_Create(&zxc_module);
    if (!m) return NULL;

    PyModule_AddIntConstant(m, "LEVEL_FASTEST", ZXC_LEVEL_FASTEST);
    PyModule_AddIntConstant(m, "LEVEL_FAST", ZXC_LEVEL_FAST);
    PyModule_AddIntConstant(m, "LEVEL_DEFAULT", ZXC_LEVEL_DEFAULT);
    PyModule_AddIntConstant(m, "LEVEL_BALANCED", ZXC_LEVEL_BALANCED);
    PyModule_AddIntConstant(m, "LEVEL_COMPACT", ZXC_LEVEL_COMPACT);

    /* Error Enums */
    PyModule_AddIntConstant(m, "ERROR_MEMORY", ZXC_ERROR_MEMORY);
    PyModule_AddIntConstant(m, "ERROR_DST_TOO_SMALL", ZXC_ERROR_DST_TOO_SMALL);
    PyModule_AddIntConstant(m, "ERROR_SRC_TOO_SMALL", ZXC_ERROR_SRC_TOO_SMALL);
    PyModule_AddIntConstant(m, "ERROR_BAD_MAGIC", ZXC_ERROR_BAD_MAGIC);
    PyModule_AddIntConstant(m, "ERROR_BAD_VERSION", ZXC_ERROR_BAD_VERSION);
    PyModule_AddIntConstant(m, "ERROR_BAD_HEADER", ZXC_ERROR_BAD_HEADER);
    PyModule_AddIntConstant(m, "ERROR_BAD_CHECKSUM", ZXC_ERROR_BAD_CHECKSUM);
    PyModule_AddIntConstant(m, "ERROR_CORRUPT_DATA", ZXC_ERROR_CORRUPT_DATA);
    PyModule_AddIntConstant(m, "ERROR_BAD_OFFSET", ZXC_ERROR_BAD_OFFSET);
    PyModule_AddIntConstant(m, "ERROR_OVERFLOW", ZXC_ERROR_OVERFLOW);
    PyModule_AddIntConstant(m, "ERROR_IO", ZXC_ERROR_IO);
    PyModule_AddIntConstant(m, "ERROR_NULL_INPUT", ZXC_ERROR_NULL_INPUT);
    PyModule_AddIntConstant(m, "ERROR_BAD_BLOCK_TYPE", ZXC_ERROR_BAD_BLOCK_TYPE);
    PyModule_AddIntConstant(m, "ERROR_BAD_BLOCK_SIZE", ZXC_ERROR_BAD_BLOCK_SIZE);

    return m;
}

// =============================================================================
// Functions definitions
// =============================================================================

static PyObject* pyzxc_compress(PyObject* self, PyObject* args, PyObject* kwargs) {
    Py_buffer view;
    int level = ZXC_LEVEL_DEFAULT;
    int checksum = 0;

    static char* kwlist[] = {"data", "level", "checksum", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "y*|ip", kwlist, &view, &level, &checksum)) {
        return NULL;
    }

    if (view.itemsize != 1) {
        PyBuffer_Release(&view);
        PyErr_SetString(PyExc_TypeError, "expected a byte buffer (itemsize==1)");
        return NULL;
    }

    size_t src_size = (size_t)view.len;

    uint64_t bound = zxc_compress_bound(src_size);

    PyObject* out = PyBytes_FromStringAndSize(NULL, (Py_ssize_t)bound);
    if (!out) {
        PyBuffer_Release(&view);
        return NULL;
    }

    char* dst = PyBytes_AsString(out);  // Return a pointer to the contents
    int64_t nwritten;                   // The number of bytes written to dst

    zxc_compress_opts_t copts = {0};
    copts.level = level;
    copts.checksum_enabled = checksum;

    Py_BEGIN_ALLOW_THREADS nwritten = zxc_compress(view.buf,  // Source buffer
                                                   src_size,  // Source size
                                                   dst,       // Destination buffer
                                                   bound,     // Destination capacity
                                                   &copts     // Options
    );
    Py_END_ALLOW_THREADS

        PyBuffer_Release(&view);

    if (nwritten < 0) {
        Py_DECREF(out);
        Py_Return_Err(PyExc_RuntimeError, zxc_error_name((int)nwritten));
    }

    if (nwritten == 0) {
        Py_DECREF(out);
        Py_Return_Err(PyExc_ValueError, "input is too small to be compressed");
    }

    if (_PyBytes_Resize(&out, (Py_ssize_t)nwritten) < 0)  // Realloc
        return NULL;

    return out;
}

static PyObject* pyzxc_get_decompressed_size(PyObject* self, PyObject* args, PyObject* kwargs) {
    Py_buffer view;

    if (!PyArg_ParseTuple(args, "y*", &view)) {
        return NULL;
    }

    uint64_t n = zxc_get_decompressed_size(view.buf, view.len);

    PyBuffer_Release(&view);

    return Py_BuildValue("K", n);
}

static PyObject* pyzxc_decompress(PyObject* self, PyObject* args, PyObject* kwargs) {
    Py_buffer view;
    int checksum = 0;

    Py_ssize_t decompress_size;
    static char* kwlist[] = {"data", "decompress_size", "checksum", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "y*n|p", kwlist, &view, &decompress_size,
                                     &checksum)) {
        return NULL;
    }

    if (view.itemsize != 1) {
        PyBuffer_Release(&view);
        PyErr_SetString(PyExc_TypeError, "expected a byte buffer (itemsize==1)");
        return NULL;
    }

    size_t src_size = (size_t)view.len;

    PyObject* out = PyBytes_FromStringAndSize(NULL, (Py_ssize_t)decompress_size);
    if (!out) {
        PyBuffer_Release(&view);
        return NULL;
    }

    char* dst = PyBytes_AsString(out);  // Return a pointer to the contents
    int64_t nwritten;                   // The number of bytes written to dst

    zxc_decompress_opts_t dopts = {0};
    dopts.checksum_enabled = checksum;

    Py_BEGIN_ALLOW_THREADS nwritten = zxc_decompress(view.buf,         // Source buffer
                                                     src_size,         // Source size
                                                     dst,              // Destination buffer
                                                     decompress_size,  // Destination capacity
                                                     &dopts            // Options
    );
    Py_END_ALLOW_THREADS

        PyBuffer_Release(&view);

    if (nwritten < 0) {
        Py_DECREF(out);
        Py_Return_Err(PyExc_RuntimeError, zxc_error_name((int)nwritten));
    }

    return out;
}

static PyObject* pyzxc_stream_compress(PyObject* self, PyObject* args, PyObject* kwargs) {
    PyObject *src, *dst;
    int nthreads = 0;
    int level = ZXC_LEVEL_DEFAULT;
    int checksum = 0;
    int seekable = 0;

    static char* kwlist[] = {"src", "dst", "n_threads", "level", "checksum", "seekable", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|iipp", kwlist, &src, &dst, &nthreads, &level,
                                     &checksum, &seekable)) {
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

    FILE* fsrc = zxc_fdopen(src_dup, "rb");
    if (!fsrc) {
        zxc_close(src_dup);
        zxc_close(dst_dup);
        Py_Return_Errno(PyExc_OSError);
    }

    FILE* fdst = zxc_fdopen(dst_dup, "wb");
    if (!fdst) {
        fclose(fsrc);
        zxc_close(dst_dup);
        Py_Return_Errno(PyExc_OSError);
    }

    int64_t nwritten;

    zxc_compress_opts_t scopts = {0};
    scopts.n_threads = nthreads;
    scopts.level = level;
    scopts.checksum_enabled = checksum;
    scopts.seekable = seekable;

    Py_BEGIN_ALLOW_THREADS nwritten = zxc_stream_compress(fsrc, fdst, &scopts);
    Py_END_ALLOW_THREADS

        fclose(fdst);
    fclose(fsrc);

    if (nwritten < 0) Py_Return_Err(PyExc_RuntimeError, zxc_error_name((int)nwritten));

    return Py_BuildValue("L", nwritten);
}

static PyObject* pyzxc_stream_decompress(PyObject* self, PyObject* args, PyObject* kwargs) {
    PyObject *src, *dst;
    int nthreads = 0;
    int checksum = 0;

    static char* kwlist[] = {"src", "dst", "n_threads", "checksum", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|ip", kwlist, &src, &dst, &nthreads,
                                     &checksum)) {
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

    FILE* fsrc = zxc_fdopen(src_dup, "rb");
    if (!fsrc) {
        zxc_close(src_dup);
        zxc_close(dst_dup);
        Py_Return_Errno(PyExc_OSError);
    }

    FILE* fdst = zxc_fdopen(dst_dup, "wb");
    if (!fdst) {
        fclose(fsrc);
        zxc_close(dst_dup);
        Py_Return_Errno(PyExc_OSError);
    }

    int64_t nwritten;

    zxc_decompress_opts_t sdopts = {0};
    sdopts.n_threads = nthreads;
    sdopts.checksum_enabled = checksum;

    Py_BEGIN_ALLOW_THREADS nwritten = zxc_stream_decompress(fsrc, fdst, &sdopts);
    Py_END_ALLOW_THREADS

        fclose(fdst);
    fclose(fsrc);

    if (nwritten < 0) Py_Return_Err(PyExc_RuntimeError, zxc_error_name((int)nwritten));

    return Py_BuildValue("L", nwritten);
}

// =============================================================================
// Library Info Helpers
// =============================================================================

static PyObject* pyzxc_min_level(PyObject* self, PyObject* args) {
    (void)self;
    (void)args;
    return PyLong_FromLong(zxc_min_level());
}

static PyObject* pyzxc_max_level(PyObject* self, PyObject* args) {
    (void)self;
    (void)args;
    return PyLong_FromLong(zxc_max_level());
}

static PyObject* pyzxc_default_level(PyObject* self, PyObject* args) {
    (void)self;
    (void)args;
    return PyLong_FromLong(zxc_default_level());
}

static PyObject* pyzxc_version_string(PyObject* self, PyObject* args) {
    (void)self;
    (void)args;
    const char* const v = zxc_version_string();
    return PyUnicode_FromString(v ? v : "");
}

// =============================================================================
// Push Streaming API (single-threaded, caller-driven)
// =============================================================================
//
// Streams are exposed as PyCapsule handles. A small Python class in
// __init__.py wraps these capsules into idiomatic CStream / DStream
// classes; users typically never see the capsules directly.

#define ZXC_CSTREAM_CAPSULE "zxc_cstream"
#define ZXC_DSTREAM_CAPSULE "zxc_dstream"

static void cstream_capsule_destructor(PyObject* capsule) {
    zxc_cstream* cs = (zxc_cstream*)PyCapsule_GetPointer(capsule, ZXC_CSTREAM_CAPSULE);
    if (cs) zxc_cstream_free(cs);
}

static void dstream_capsule_destructor(PyObject* capsule) {
    zxc_dstream* ds = (zxc_dstream*)PyCapsule_GetPointer(capsule, ZXC_DSTREAM_CAPSULE);
    if (ds) zxc_dstream_free(ds);
}

static zxc_cstream* cstream_from_capsule(PyObject* capsule) {
    zxc_cstream* cs = (zxc_cstream*)PyCapsule_GetPointer(capsule, ZXC_CSTREAM_CAPSULE);
    if (!cs) {
        PyErr_SetString(PyExc_ValueError, "cstream is closed or invalid");
        return NULL;
    }
    return cs;
}

static zxc_dstream* dstream_from_capsule(PyObject* capsule) {
    zxc_dstream* ds = (zxc_dstream*)PyCapsule_GetPointer(capsule, ZXC_DSTREAM_CAPSULE);
    if (!ds) {
        PyErr_SetString(PyExc_ValueError, "dstream is closed or invalid");
        return NULL;
    }
    return ds;
}

static PyObject* pyzxc_cstream_create(PyObject* self, PyObject* args, PyObject* kwargs) {
    (void)self;
    int level = ZXC_LEVEL_DEFAULT;
    int checksum = 0;
    Py_ssize_t block_size = 0;

    static char* kwlist[] = {"level", "checksum", "block_size", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|ipn", kwlist, &level, &checksum,
                                     &block_size)) {
        return NULL;
    }
    if (block_size < 0) {
        Py_Return_Err(PyExc_ValueError, "block_size must be non-negative");
    }

    zxc_compress_opts_t copts = {0};
    copts.level = level;
    copts.checksum_enabled = checksum;
    copts.block_size = (size_t)block_size;

    zxc_cstream* cs = zxc_cstream_create(&copts);
    if (!cs) Py_Return_Err(PyExc_MemoryError, "zxc_cstream_create failed");

    PyObject* cap = PyCapsule_New(cs, ZXC_CSTREAM_CAPSULE, cstream_capsule_destructor);
    if (!cap) {
        zxc_cstream_free(cs);
        return NULL;
    }
    return cap;
}

/* Drains the cstream by calling zxc_cstream_compress repeatedly until the
 * input is fully consumed and no compressed bytes remain pending. Returns
 * the accumulated compressed bytes. */
static PyObject* pyzxc_cstream_compress(PyObject* self, PyObject* args, PyObject* kwargs) {
    (void)self;
    PyObject* capsule;
    Py_buffer view;
    static char* kwlist[] = {"cs", "data", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Oy*", kwlist, &capsule, &view)) {
        return NULL;
    }

    zxc_cstream* cs = cstream_from_capsule(capsule);
    if (!cs) {
        PyBuffer_Release(&view);
        return NULL;
    }

    /* Output buffer grows on demand. Start at the suggested chunk size. */
    size_t out_cap = zxc_cstream_out_size(cs);
    if (out_cap < 4096) out_cap = 4096;
    size_t out_len = 0;
    uint8_t* out_buf = (uint8_t*)PyMem_Malloc(out_cap);
    if (!out_buf) {
        PyBuffer_Release(&view);
        return PyErr_NoMemory();
    }

    zxc_inbuf_t in = {.src = view.buf, .size = (size_t)view.len, .pos = 0};
    int err_code = 0;
    int oom = 0;

    Py_BEGIN_ALLOW_THREADS for (;;) {
        /* Make sure there's room to write at least one block worth. */
        size_t want = zxc_cstream_out_size(cs);
        if (want < 4096) want = 4096;
        if (out_cap - out_len < want) {
            size_t new_cap = out_cap * 2;
            if (new_cap < out_len + want) new_cap = out_len + want;
            uint8_t* nb = (uint8_t*)PyMem_Realloc(out_buf, new_cap);
            if (!nb) {
                oom = 1;
                break;
            }
            out_buf = nb;
            out_cap = new_cap;
        }

        zxc_outbuf_t out = {.dst = out_buf + out_len, .size = out_cap - out_len, .pos = 0};
        const int64_t r = zxc_cstream_compress(cs, &out, &in);
        out_len += out.pos;

        if (r < 0) {
            err_code = (int)r;
            break;
        }
        if (r == 0 && in.pos == in.size) break; /* fully drained */
    }
    Py_END_ALLOW_THREADS

        PyBuffer_Release(&view);

    if (oom) {
        PyMem_Free(out_buf);
        return PyErr_NoMemory();
    }
    if (err_code) {
        PyMem_Free(out_buf);
        Py_Return_Err(PyExc_RuntimeError, zxc_error_name(err_code));
    }

    PyObject* result = PyBytes_FromStringAndSize((const char*)out_buf, (Py_ssize_t)out_len);
    PyMem_Free(out_buf);
    return result;
}

/* Drains the cstream's finalisation phase: residual block + EOF + footer. */
static PyObject* pyzxc_cstream_end(PyObject* self, PyObject* args, PyObject* kwargs) {
    (void)self;
    PyObject* capsule;
    static char* kwlist[] = {"cs", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O", kwlist, &capsule)) return NULL;

    zxc_cstream* cs = cstream_from_capsule(capsule);
    if (!cs) return NULL;

    size_t out_cap = zxc_cstream_out_size(cs);
    if (out_cap < 4096) out_cap = 4096;
    size_t out_len = 0;
    uint8_t* out_buf = (uint8_t*)PyMem_Malloc(out_cap);
    if (!out_buf) return PyErr_NoMemory();

    int err_code = 0;
    int oom = 0;

    Py_BEGIN_ALLOW_THREADS for (;;) {
        if (out_cap - out_len < 4096) {
            size_t new_cap = out_cap * 2;
            uint8_t* nb = (uint8_t*)PyMem_Realloc(out_buf, new_cap);
            if (!nb) {
                oom = 1;
                break;
            }
            out_buf = nb;
            out_cap = new_cap;
        }
        zxc_outbuf_t out = {.dst = out_buf + out_len, .size = out_cap - out_len, .pos = 0};
        const int64_t r = zxc_cstream_end(cs, &out);
        out_len += out.pos;
        if (r < 0) {
            err_code = (int)r;
            break;
        }
        if (r == 0) break;
    }
    Py_END_ALLOW_THREADS

        if (oom) {
        PyMem_Free(out_buf);
        return PyErr_NoMemory();
    }
    if (err_code) {
        PyMem_Free(out_buf);
        Py_Return_Err(PyExc_RuntimeError, zxc_error_name(err_code));
    }
    PyObject* result = PyBytes_FromStringAndSize((const char*)out_buf, (Py_ssize_t)out_len);
    PyMem_Free(out_buf);
    return result;
}

static PyObject* pyzxc_cstream_in_size(PyObject* self, PyObject* capsule) {
    (void)self;
    zxc_cstream* cs = cstream_from_capsule(capsule);
    if (!cs) return NULL;
    return PyLong_FromSize_t(zxc_cstream_in_size(cs));
}

static PyObject* pyzxc_cstream_out_size(PyObject* self, PyObject* capsule) {
    (void)self;
    zxc_cstream* cs = cstream_from_capsule(capsule);
    if (!cs) return NULL;
    return PyLong_FromSize_t(zxc_cstream_out_size(cs));
}

static PyObject* pyzxc_cstream_free(PyObject* self, PyObject* capsule) {
    (void)self;
    if (!PyCapsule_IsValid(capsule, ZXC_CSTREAM_CAPSULE)) Py_RETURN_NONE;
    zxc_cstream* cs = (zxc_cstream*)PyCapsule_GetPointer(capsule, ZXC_CSTREAM_CAPSULE);
    if (cs) {
        zxc_cstream_free(cs);
        PyCapsule_SetPointer(capsule, NULL);
        PyCapsule_SetDestructor(capsule, NULL);
    }
    Py_RETURN_NONE;
}

static PyObject* pyzxc_dstream_create(PyObject* self, PyObject* args, PyObject* kwargs) {
    (void)self;
    int checksum = 0;
    static char* kwlist[] = {"checksum", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|p", kwlist, &checksum)) return NULL;

    zxc_decompress_opts_t dopts = {0};
    dopts.checksum_enabled = checksum;

    zxc_dstream* ds = zxc_dstream_create(&dopts);
    if (!ds) Py_Return_Err(PyExc_MemoryError, "zxc_dstream_create failed");

    PyObject* cap = PyCapsule_New(ds, ZXC_DSTREAM_CAPSULE, dstream_capsule_destructor);
    if (!cap) {
        zxc_dstream_free(ds);
        return NULL;
    }
    return cap;
}

static PyObject* pyzxc_dstream_decompress(PyObject* self, PyObject* args, PyObject* kwargs) {
    (void)self;
    PyObject* capsule;
    Py_buffer view;
    static char* kwlist[] = {"ds", "data", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Oy*", kwlist, &capsule, &view)) {
        return NULL;
    }

    zxc_dstream* ds = dstream_from_capsule(capsule);
    if (!ds) {
        PyBuffer_Release(&view);
        return NULL;
    }

    size_t out_cap = zxc_dstream_out_size(ds);
    if (out_cap < 4096) out_cap = 4096;
    size_t out_len = 0;
    uint8_t* out_buf = (uint8_t*)PyMem_Malloc(out_cap);
    if (!out_buf) {
        PyBuffer_Release(&view);
        return PyErr_NoMemory();
    }

    zxc_inbuf_t in = {.src = view.buf, .size = (size_t)view.len, .pos = 0};
    int err_code = 0;
    int oom = 0;

    Py_BEGIN_ALLOW_THREADS for (;;) {
        size_t want = zxc_dstream_out_size(ds);
        if (want < 4096) want = 4096;
        if (out_cap - out_len < want) {
            size_t new_cap = out_cap * 2;
            if (new_cap < out_len + want) new_cap = out_len + want;
            uint8_t* nb = (uint8_t*)PyMem_Realloc(out_buf, new_cap);
            if (!nb) {
                oom = 1;
                break;
            }
            out_buf = nb;
            out_cap = new_cap;
        }
        zxc_outbuf_t out = {.dst = out_buf + out_len, .size = out_cap - out_len, .pos = 0};
        const size_t before_in = in.pos;
        const size_t before_out = out.pos;
        const int64_t r = zxc_dstream_decompress(ds, &out, &in);
        out_len += out.pos;
        if (r < 0) {
            err_code = (int)r;
            break;
        }
        /* Stop when no progress was made or the input is exhausted. */
        if (in.pos == before_in && out.pos == before_out) break;
        if (in.pos == in.size) break;
    }
    Py_END_ALLOW_THREADS

        PyBuffer_Release(&view);

    if (oom) {
        PyMem_Free(out_buf);
        return PyErr_NoMemory();
    }
    if (err_code) {
        PyMem_Free(out_buf);
        Py_Return_Err(PyExc_RuntimeError, zxc_error_name(err_code));
    }

    PyObject* result = PyBytes_FromStringAndSize((const char*)out_buf, (Py_ssize_t)out_len);
    PyMem_Free(out_buf);
    return result;
}

static PyObject* pyzxc_dstream_finished(PyObject* self, PyObject* capsule) {
    (void)self;
    zxc_dstream* ds = dstream_from_capsule(capsule);
    if (!ds) return NULL;
    return PyBool_FromLong(zxc_dstream_finished(ds));
}

static PyObject* pyzxc_dstream_in_size(PyObject* self, PyObject* capsule) {
    (void)self;
    zxc_dstream* ds = dstream_from_capsule(capsule);
    if (!ds) return NULL;
    return PyLong_FromSize_t(zxc_dstream_in_size(ds));
}

static PyObject* pyzxc_dstream_out_size(PyObject* self, PyObject* capsule) {
    (void)self;
    zxc_dstream* ds = dstream_from_capsule(capsule);
    if (!ds) return NULL;
    return PyLong_FromSize_t(zxc_dstream_out_size(ds));
}

static PyObject* pyzxc_dstream_free(PyObject* self, PyObject* capsule) {
    (void)self;
    if (!PyCapsule_IsValid(capsule, ZXC_DSTREAM_CAPSULE)) Py_RETURN_NONE;
    zxc_dstream* ds = (zxc_dstream*)PyCapsule_GetPointer(capsule, ZXC_DSTREAM_CAPSULE);
    if (ds) {
        zxc_dstream_free(ds);
        PyCapsule_SetPointer(capsule, NULL);
        PyCapsule_SetDestructor(capsule, NULL);
    }
    Py_RETURN_NONE;
}
