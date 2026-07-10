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
#include "zxc_seekable.h"
#include "zxc_stream.h"

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

static int grow_output(uint8_t** buf, size_t* cap, size_t out_len, size_t want) {
    const size_t limit = (size_t)PY_SSIZE_T_MAX;
    if (out_len > limit || want > limit - out_len) return -1;
    const size_t needed = out_len + want;
    size_t new_cap = (*cap > limit / 2) ? limit : (*cap * 2);
    if (new_cap < needed) new_cap = needed;
    uint8_t* nb = (uint8_t*)realloc(*buf, new_cap);
    if (!nb) return -2;
    *buf = nb;
    *cap = new_cap;
    return 0;
}

// =============================================================================
// Wrapper functions
// =============================================================================

static PyObject* pyzxc_compress(PyObject* self, PyObject* args, PyObject* kwargs);
static PyObject* pyzxc_decompress(PyObject* self, PyObject* args, PyObject* kwargs);
static PyObject* pyzxc_stream_compress(PyObject* self, PyObject* args, PyObject* kwargs);
static PyObject* pyzxc_stream_decompress(PyObject* self, PyObject* args, PyObject* kwargs);
static PyObject* pyzxc_get_decompressed_size(PyObject* self, PyObject* arg);
static PyObject* pyzxc_min_level(PyObject* self, PyObject* args);
static PyObject* pyzxc_max_level(PyObject* self, PyObject* args);
static PyObject* pyzxc_default_level(PyObject* self, PyObject* args);
static PyObject* pyzxc_version_string(PyObject* self, PyObject* args);

static PyObject* pyzxc_train_dict(PyObject* self, PyObject* args, PyObject* kwargs);
static PyObject* pyzxc_dict_id(PyObject* self, PyObject* arg);
static PyObject* pyzxc_get_dict_id(PyObject* self, PyObject* arg);
static PyObject* pyzxc_dict_get_id(PyObject* self, PyObject* arg);
static PyObject* pyzxc_dict_save(PyObject* self, PyObject* args);
static PyObject* pyzxc_train_dict_huf(PyObject* self, PyObject* args, PyObject* kwargs);
static PyObject* pyzxc_dict_huf(PyObject* self, PyObject* arg);
static PyObject* pyzxc_dict_train(PyObject* self, PyObject* args, PyObject* kwargs);
static PyObject* pyzxc_dict_load(PyObject* self, PyObject* arg);
static PyObject* pyzxc_seekable_set_dict(PyObject* self, PyObject* args);

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

static PyObject* pyzxc_seekable_open(PyObject* self, PyObject* args);
static PyObject* pyzxc_seekable_open_reader(PyObject* self, PyObject* args);
static PyObject* pyzxc_seekable_num_blocks(PyObject* self, PyObject* args);
static PyObject* pyzxc_seekable_decompressed_size(PyObject* self, PyObject* args);
static PyObject* pyzxc_seekable_block_comp_size(PyObject* self, PyObject* args);
static PyObject* pyzxc_seekable_block_decomp_size(PyObject* self, PyObject* args);
static PyObject* pyzxc_seekable_decompress_range(PyObject* self, PyObject* args, PyObject* kwargs);
static PyObject* pyzxc_seekable_free(PyObject* self, PyObject* args);
static PyObject* pyzxc_seek_table_size(PyObject* self, PyObject* args);
static PyObject* pyzxc_write_seek_table(PyObject* self, PyObject* args);

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
    {"pyzxc_get_decompressed_size", (PyCFunction)pyzxc_get_decompressed_size, METH_O, NULL},
    {"pyzxc_min_level", (PyCFunction)pyzxc_min_level, METH_NOARGS, NULL},
    {"pyzxc_max_level", (PyCFunction)pyzxc_max_level, METH_NOARGS, NULL},
    {"pyzxc_default_level", (PyCFunction)pyzxc_default_level, METH_NOARGS, NULL},
    {"pyzxc_version_string", (PyCFunction)pyzxc_version_string, METH_NOARGS, NULL},

    /* Dictionary API. */
    {"pyzxc_train_dict", (PyCFunction)pyzxc_train_dict, METH_VARARGS | METH_KEYWORDS, NULL},
    {"pyzxc_dict_id", (PyCFunction)pyzxc_dict_id, METH_O, NULL},
    {"pyzxc_get_dict_id", (PyCFunction)pyzxc_get_dict_id, METH_O, NULL},
    {"pyzxc_dict_get_id", (PyCFunction)pyzxc_dict_get_id, METH_O, NULL},
    {"pyzxc_dict_save", (PyCFunction)pyzxc_dict_save, METH_VARARGS, NULL},
    {"pyzxc_train_dict_huf", (PyCFunction)pyzxc_train_dict_huf, METH_VARARGS | METH_KEYWORDS, NULL},
    {"pyzxc_dict_huf", (PyCFunction)pyzxc_dict_huf, METH_O, NULL},
    {"pyzxc_dict_train", (PyCFunction)pyzxc_dict_train, METH_VARARGS | METH_KEYWORDS, NULL},
    {"pyzxc_dict_load", (PyCFunction)pyzxc_dict_load, METH_O, NULL},

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

    /* Seekable random-access decompression API. */
    {"pyzxc_seekable_open", (PyCFunction)pyzxc_seekable_open, METH_O, NULL},
    {"pyzxc_seekable_open_reader", (PyCFunction)pyzxc_seekable_open_reader, METH_O, NULL},
    {"pyzxc_seekable_num_blocks", (PyCFunction)pyzxc_seekable_num_blocks, METH_O, NULL},
    {"pyzxc_seekable_decompressed_size", (PyCFunction)pyzxc_seekable_decompressed_size, METH_O, NULL},
    {"pyzxc_seekable_block_comp_size", (PyCFunction)pyzxc_seekable_block_comp_size,
     METH_VARARGS, NULL},
    {"pyzxc_seekable_block_decomp_size", (PyCFunction)pyzxc_seekable_block_decomp_size,
     METH_VARARGS, NULL},
    {"pyzxc_seekable_decompress_range", (PyCFunction)pyzxc_seekable_decompress_range,
     METH_VARARGS | METH_KEYWORDS, NULL},
    {"pyzxc_seekable_free", (PyCFunction)pyzxc_seekable_free, METH_O, NULL},
    {"pyzxc_seekable_set_dict", (PyCFunction)pyzxc_seekable_set_dict, METH_VARARGS, NULL},
    {"pyzxc_seek_table_size", (PyCFunction)pyzxc_seek_table_size, METH_O, NULL},
    {"pyzxc_write_seek_table", (PyCFunction)pyzxc_write_seek_table, METH_O, NULL},

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
    PyModule_AddIntConstant(m, "LEVEL_DENSITY", ZXC_LEVEL_DENSITY);
    PyModule_AddIntConstant(m, "LEVEL_ULTRA", ZXC_LEVEL_ULTRA);

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
    PyModule_AddIntConstant(m, "ERROR_DICT_REQUIRED", ZXC_ERROR_DICT_REQUIRED);
    PyModule_AddIntConstant(m, "ERROR_DICT_MISMATCH", ZXC_ERROR_DICT_MISMATCH);
    PyModule_AddIntConstant(m, "ERROR_DICT_TOO_LARGE", ZXC_ERROR_DICT_TOO_LARGE);

    return m;
}

// =============================================================================
// Functions definitions
// =============================================================================

static PyObject* pyzxc_compress(PyObject* self, PyObject* args, PyObject* kwargs) {
    (void)self;
    Py_buffer view;
    int level = ZXC_LEVEL_DEFAULT;
    int checksum = 0;
    Py_buffer dict_view = {0};
    PyObject* dict_obj = NULL;
    int have_dict = 0;
    PyObject* dict_huf_obj = NULL;
    uint8_t huf_local[ZXC_HUF_TABLE_SIZE];
    const void* dict_huf = NULL;

    static char* kwlist[] = {"data", "level", "checksum", "dict", "dict_huf", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "y*|ipOO", kwlist, &view, &level, &checksum,
                                     &dict_obj, &dict_huf_obj)) {
        return NULL;
    }

    if (view.itemsize != 1) {
        PyBuffer_Release(&view);
        PyErr_SetString(PyExc_TypeError, "expected a byte buffer (itemsize==1)");
        return NULL;
    }

    if (level < zxc_min_level() || level > zxc_max_level()) {
        PyBuffer_Release(&view);
        PyErr_Format(PyExc_ValueError, "level must be in [%d, %d], got %d", zxc_min_level(),
                     zxc_max_level(), level);
        return NULL;
    }

    if (dict_obj && dict_obj != Py_None) {
        if (PyObject_GetBuffer(dict_obj, &dict_view, PyBUF_SIMPLE) < 0) {
            PyBuffer_Release(&view);
            return NULL;
        }
        have_dict = 1;
    }

    if (dict_huf_obj && dict_huf_obj != Py_None) {
        Py_buffer hv;
        if (PyObject_GetBuffer(dict_huf_obj, &hv, PyBUF_SIMPLE) < 0) {
            PyBuffer_Release(&view);
            if (have_dict) PyBuffer_Release(&dict_view);
            return NULL;
        }
        if (hv.len != ZXC_HUF_TABLE_SIZE) {
            PyBuffer_Release(&hv);
            PyBuffer_Release(&view);
            if (have_dict) PyBuffer_Release(&dict_view);
            PyErr_SetString(PyExc_ValueError, "dict_huf must be exactly 128 bytes");
            return NULL;
        }
        memcpy(huf_local, hv.buf, ZXC_HUF_TABLE_SIZE);
        PyBuffer_Release(&hv);
        dict_huf = huf_local;
    }

    size_t src_size = (size_t)view.len;

    uint64_t bound = zxc_compress_bound(src_size);

    PyObject* out = PyBytes_FromStringAndSize(NULL, (Py_ssize_t)bound);
    if (!out) {
        PyBuffer_Release(&view);
        if (have_dict) PyBuffer_Release(&dict_view);
        return NULL;
    }

    char* dst = PyBytes_AsString(out);  // Return a pointer to the contents
    int64_t nwritten;                   // The number of bytes written to dst

    zxc_compress_opts_t copts = {0};
    copts.level = level;
    copts.checksum_enabled = checksum;
    if (have_dict) {
        copts.dict = dict_view.buf;
        copts.dict_size = (size_t)dict_view.len;
        copts.dict_huf = dict_huf;
    }

    Py_BEGIN_ALLOW_THREADS nwritten = zxc_compress(view.buf,  // Source buffer
                                                   src_size,  // Source size
                                                   dst,       // Destination buffer
                                                   bound,     // Destination capacity
                                                   &copts     // Options
    );
    Py_END_ALLOW_THREADS

        PyBuffer_Release(&view);
    if (have_dict) PyBuffer_Release(&dict_view);

    if (nwritten < 0) {
        Py_DECREF(out);
        Py_Return_Err(PyExc_RuntimeError, zxc_error_name((int)nwritten));
    }

    if (_PyBytes_Resize(&out, (Py_ssize_t)nwritten) < 0)  // Realloc
        return NULL;

    return out;
}

static PyObject* pyzxc_get_decompressed_size(PyObject* self, PyObject* arg) {
    (void)self;
    Py_buffer view;

    if (PyObject_GetBuffer(arg, &view, PyBUF_SIMPLE) < 0) return NULL;

    uint64_t n = zxc_get_decompressed_size(view.buf, (size_t)view.len);

    PyBuffer_Release(&view);

    return Py_BuildValue("K", n);
}

static PyObject* pyzxc_decompress(PyObject* self, PyObject* args, PyObject* kwargs) {
    (void)self;
    Py_buffer view;
    int checksum = 0;
    Py_buffer dict_view = {0};
    PyObject* dict_obj = NULL;
    int have_dict = 0;

    Py_ssize_t decompress_size;
    PyObject* dict_huf_obj = NULL;
    uint8_t huf_local[ZXC_HUF_TABLE_SIZE];
    const void* dict_huf = NULL;
    static char* kwlist[] = {"data", "decompress_size", "checksum", "dict", "dict_huf", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "y*n|pOO", kwlist, &view, &decompress_size,
                                     &checksum, &dict_obj, &dict_huf_obj)) {
        return NULL;
    }

    if (view.itemsize != 1) {
        PyBuffer_Release(&view);
        PyErr_SetString(PyExc_TypeError, "expected a byte buffer (itemsize==1)");
        return NULL;
    }

    if (decompress_size < 0) {
        PyBuffer_Release(&view);
        PyErr_SetString(PyExc_ValueError, "decompress_size must be non-negative");
        return NULL;
    }

    if (dict_obj && dict_obj != Py_None) {
        if (PyObject_GetBuffer(dict_obj, &dict_view, PyBUF_SIMPLE) < 0) {
            PyBuffer_Release(&view);
            return NULL;
        }
        have_dict = 1;
    }

    if (dict_huf_obj && dict_huf_obj != Py_None) {
        Py_buffer hv;
        if (PyObject_GetBuffer(dict_huf_obj, &hv, PyBUF_SIMPLE) < 0) {
            PyBuffer_Release(&view);
            if (have_dict) PyBuffer_Release(&dict_view);
            return NULL;
        }
        if (hv.len != ZXC_HUF_TABLE_SIZE) {
            PyBuffer_Release(&hv);
            PyBuffer_Release(&view);
            if (have_dict) PyBuffer_Release(&dict_view);
            PyErr_SetString(PyExc_ValueError, "dict_huf must be exactly 128 bytes");
            return NULL;
        }
        memcpy(huf_local, hv.buf, ZXC_HUF_TABLE_SIZE);
        PyBuffer_Release(&hv);
        dict_huf = huf_local;
    }

    size_t src_size = (size_t)view.len;

    PyObject* out = PyBytes_FromStringAndSize(NULL, (Py_ssize_t)decompress_size);
    if (!out) {
        PyBuffer_Release(&view);
        if (have_dict) PyBuffer_Release(&dict_view);
        return NULL;
    }

    char* dst = PyBytes_AsString(out);  // Return a pointer to the contents
    int64_t nwritten;                   // The number of bytes written to dst

    zxc_decompress_opts_t dopts = {0};
    dopts.checksum_enabled = checksum;
    if (have_dict) {
        dopts.dict = dict_view.buf;
        dopts.dict_size = (size_t)dict_view.len;
        dopts.dict_huf = dict_huf;
    }

    Py_BEGIN_ALLOW_THREADS nwritten = zxc_decompress(view.buf,         // Source buffer
                                                     src_size,         // Source size
                                                     dst,              // Destination buffer
                                                     decompress_size,  // Destination capacity
                                                     &dopts            // Options
    );
    Py_END_ALLOW_THREADS

        PyBuffer_Release(&view);
    if (have_dict) PyBuffer_Release(&dict_view);

    if (nwritten < 0) {
        Py_DECREF(out);
        Py_Return_Err(PyExc_RuntimeError, zxc_error_name((int)nwritten));
    }

    /* decompress_size is an upper bound from the caller; shrink to the bytes
     * actually written so no uninitialized tail is ever exposed. */
    if (nwritten != (int64_t)decompress_size &&
        _PyBytes_Resize(&out, (Py_ssize_t)nwritten) < 0)
        return NULL;

    return out;
}

static PyObject* pyzxc_stream_compress(PyObject* self, PyObject* args, PyObject* kwargs) {
    (void)self;
    PyObject *src;
    PyObject *dst;
    int nthreads = 0;
    int level = ZXC_LEVEL_DEFAULT;
    int checksum = 0;
    int seekable = 0;
    Py_buffer dict_view = {0};
    PyObject* dict_obj = NULL;
    int have_dict = 0;

    static char* kwlist[] = {"src",      "dst",      "n_threads", "level",
                             "checksum", "seekable", "dict",      "dict_huf", NULL};
    PyObject* dict_huf_obj = NULL;
    uint8_t huf_local[ZXC_HUF_TABLE_SIZE];
    const void* dict_huf = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|iippOO", kwlist, &src, &dst, &nthreads,
                                     &level, &checksum, &seekable, &dict_obj, &dict_huf_obj)) {
        return NULL;
    }

    if (level < zxc_min_level() || level > zxc_max_level()) {
        PyErr_Format(PyExc_ValueError, "level must be in [%d, %d], got %d", zxc_min_level(),
                     zxc_max_level(), level);
        return NULL;
    }

    if (dict_obj && dict_obj != Py_None) {
        if (PyObject_GetBuffer(dict_obj, &dict_view, PyBUF_SIMPLE) < 0) return NULL;
        have_dict = 1;
    }

    if (dict_huf_obj && dict_huf_obj != Py_None) {
        Py_buffer hv;
        if (PyObject_GetBuffer(dict_huf_obj, &hv, PyBUF_SIMPLE) < 0) {
            if (have_dict) PyBuffer_Release(&dict_view);
            return NULL;
        }
        if (hv.len != ZXC_HUF_TABLE_SIZE) {
            PyBuffer_Release(&hv);
            if (have_dict) PyBuffer_Release(&dict_view);
            PyErr_SetString(PyExc_ValueError, "dict_huf must be exactly 128 bytes");
            return NULL;
        }
        memcpy(huf_local, hv.buf, ZXC_HUF_TABLE_SIZE);
        PyBuffer_Release(&hv);
        dict_huf = huf_local;
    }

    int src_fd = PyObject_AsFileDescriptor(src);
    int dst_fd = PyObject_AsFileDescriptor(dst);

    if (src_fd == -1 || dst_fd == -1) {
        if (have_dict) PyBuffer_Release(&dict_view);
        Py_Return_Err(PyExc_RuntimeError, "couldn't get file descriptor");
    }

    int src_dup = zxc_dup(src_fd);
    if (src_dup == -1) {
        if (have_dict) PyBuffer_Release(&dict_view);
        Py_Return_Errno(PyExc_OSError);
    }
    int dst_dup = zxc_dup(dst_fd);
    if (dst_dup == -1) {
        zxc_close(src_dup);
        if (have_dict) PyBuffer_Release(&dict_view);
        Py_Return_Errno(PyExc_OSError);
    }

    FILE* fsrc = zxc_fdopen(src_dup, "rb");
    if (!fsrc) {
        zxc_close(src_dup);
        zxc_close(dst_dup);
        if (have_dict) PyBuffer_Release(&dict_view);
        Py_Return_Errno(PyExc_OSError);
    }

    FILE* fdst = zxc_fdopen(dst_dup, "wb");
    if (!fdst) {
        fclose(fsrc);
        zxc_close(dst_dup);
        if (have_dict) PyBuffer_Release(&dict_view);
        Py_Return_Errno(PyExc_OSError);
    }

    int64_t nwritten;

    zxc_compress_opts_t scopts = {0};
    scopts.n_threads = nthreads;
    scopts.level = level;
    scopts.checksum_enabled = checksum;
    scopts.seekable = seekable;
    if (have_dict) {
        scopts.dict = dict_view.buf;
        scopts.dict_size = (size_t)dict_view.len;
        scopts.dict_huf = dict_huf;
    }

    Py_BEGIN_ALLOW_THREADS nwritten = zxc_stream_compress(fsrc, fdst, &scopts);
    Py_END_ALLOW_THREADS

        fclose(fdst);
    fclose(fsrc);
    if (have_dict) PyBuffer_Release(&dict_view);

    if (nwritten < 0) Py_Return_Err(PyExc_RuntimeError, zxc_error_name((int)nwritten));

    return Py_BuildValue("L", nwritten);
}

static PyObject* pyzxc_stream_decompress(PyObject* self, PyObject* args, PyObject* kwargs) {
    (void)self;
    PyObject *src;
    PyObject *dst;
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
// Dictionary API
// =============================================================================

/* Train a dictionary from a sequence of sample byte buffers. */
static PyObject* pyzxc_train_dict(PyObject* self, PyObject* args, PyObject* kwargs) {
    (void)self;
    PyObject* samples_obj;
    Py_ssize_t max_size = ZXC_DICT_SIZE_MAX;

    static char* kwlist[] = {"samples", "max_size", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|n", kwlist, &samples_obj, &max_size)) {
        return NULL;
    }

    if (max_size <= 0 || max_size > ZXC_DICT_SIZE_MAX) {
        Py_Return_Err(PyExc_ValueError, "max_size must be in (0, ZXC_DICT_SIZE_MAX]");
    }

    PyObject* seq = PySequence_Fast(samples_obj, "samples must be a sequence of bytes-like objects");
    if (!seq) return NULL;

    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
    if (n <= 0) {
        Py_DECREF(seq);
        Py_Return_Err(PyExc_ValueError, "samples must be non-empty");
    }

    const void** ptrs = (const void**)PyMem_Malloc(sizeof(void*) * (size_t)n);
    size_t* sizes = (size_t*)PyMem_Malloc(sizeof(size_t) * (size_t)n);
    Py_buffer* views = (Py_buffer*)PyMem_Malloc(sizeof(Py_buffer) * (size_t)n);
    if (!ptrs || !sizes || !views) {
        PyMem_Free(ptrs);
        PyMem_Free(sizes);
        PyMem_Free(views);
        Py_DECREF(seq);
        return PyErr_NoMemory();
    }

    Py_ssize_t acquired = 0;
    for (; acquired < n; acquired++) {
        PyObject* item = PySequence_Fast_GET_ITEM(seq, acquired);
        if (PyObject_GetBuffer(item, &views[acquired], PyBUF_SIMPLE) < 0) {
            break;
        }
        ptrs[acquired] = views[acquired].buf;
        sizes[acquired] = (size_t)views[acquired].len;
    }

    if (acquired != n) {
        for (Py_ssize_t i = 0; i < acquired; i++) PyBuffer_Release(&views[i]);
        PyMem_Free(ptrs);
        PyMem_Free(sizes);
        PyMem_Free(views);
        Py_DECREF(seq);
        return NULL;
    }

    void* dict_buf = malloc((size_t)max_size);
    if (!dict_buf) {
        for (Py_ssize_t i = 0; i < n; i++) PyBuffer_Release(&views[i]);
        PyMem_Free(ptrs);
        PyMem_Free(sizes);
        PyMem_Free(views);
        Py_DECREF(seq);
        return PyErr_NoMemory();
    }

    int64_t dict_size;
    Py_BEGIN_ALLOW_THREADS dict_size =
        zxc_train_dict(ptrs, sizes, (size_t)n, dict_buf, (size_t)max_size);
    Py_END_ALLOW_THREADS

        for (Py_ssize_t i = 0; i < n; i++) PyBuffer_Release(&views[i]);
    PyMem_Free(ptrs);
    PyMem_Free(sizes);
    PyMem_Free(views);
    Py_DECREF(seq);

    PyObject* out = NULL;
    if (dict_size < 0) {
        PyErr_SetString(PyExc_RuntimeError, zxc_error_name((int)dict_size));
    } else {
        out = PyBytes_FromStringAndSize((const char*)dict_buf, (Py_ssize_t)dict_size);
    }
    free(dict_buf);
    return out;
}


/* Train the shared literal Huffman table for an already-trained dictionary. */
static PyObject* pyzxc_train_dict_huf(PyObject* self, PyObject* args, PyObject* kwargs) {
    (void)self;
    PyObject* samples_obj;
    Py_buffer dict_view;

    static char* kwlist[] = {"samples", "dict", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "Oy*", kwlist, &samples_obj, &dict_view)) {
        return NULL;
    }

    PyObject* seq = PySequence_Fast(samples_obj, "samples must be a sequence of bytes-like objects");
    if (!seq) {
        PyBuffer_Release(&dict_view);
        return NULL;
    }

    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
    if (n <= 0) {
        Py_DECREF(seq);
        PyBuffer_Release(&dict_view);
        Py_Return_Err(PyExc_ValueError, "samples must be non-empty");
    }

    const void** ptrs = (const void**)PyMem_Malloc(sizeof(void*) * (size_t)n);
    size_t* sizes = (size_t*)PyMem_Malloc(sizeof(size_t) * (size_t)n);
    Py_buffer* views = (Py_buffer*)PyMem_Malloc(sizeof(Py_buffer) * (size_t)n);
    if (!ptrs || !sizes || !views) {
        PyMem_Free(ptrs);
        PyMem_Free(sizes);
        PyMem_Free(views);
        Py_DECREF(seq);
        PyBuffer_Release(&dict_view);
        return PyErr_NoMemory();
    }

    Py_ssize_t acquired = 0;
    for (; acquired < n; acquired++) {
        PyObject* item = PySequence_Fast_GET_ITEM(seq, acquired);
        if (PyObject_GetBuffer(item, &views[acquired], PyBUF_SIMPLE) < 0) {
            break;
        }
        ptrs[acquired] = views[acquired].buf;
        sizes[acquired] = (size_t)views[acquired].len;
    }

    int rc = ZXC_ERROR_NULL_INPUT;
    uint8_t huf[ZXC_HUF_TABLE_SIZE];
    if (acquired == n) {
        Py_BEGIN_ALLOW_THREADS rc =
            zxc_train_dict_huf(ptrs, sizes, (size_t)n, dict_view.buf, (size_t)dict_view.len, huf);
        Py_END_ALLOW_THREADS
    }

    for (Py_ssize_t i = 0; i < acquired; i++) PyBuffer_Release(&views[i]);
    PyMem_Free(ptrs);
    PyMem_Free(sizes);
    PyMem_Free(views);
    Py_DECREF(seq);
    PyBuffer_Release(&dict_view);

    if (acquired != n) return NULL;
    if (rc != ZXC_OK) Py_Return_Err(PyExc_RuntimeError, zxc_error_name(rc));
    return PyBytes_FromStringAndSize((const char*)huf, ZXC_HUF_TABLE_SIZE);
}

/* Dictionary ID from raw dictionary content. */
static PyObject* pyzxc_dict_id(PyObject* self, PyObject* arg) {
    (void)self;
    Py_buffer view;
    if (PyObject_GetBuffer(arg, &view, PyBUF_SIMPLE) < 0) return NULL;
    uint32_t id = zxc_dict_id(view.buf, (size_t)view.len, NULL);
    PyBuffer_Release(&view);
    return PyLong_FromUnsignedLong(id);
}

/* Dictionary ID required by a .zxc archive (0 if none). */
static PyObject* pyzxc_get_dict_id(PyObject* self, PyObject* arg) {
    (void)self;
    Py_buffer view;
    if (PyObject_GetBuffer(arg, &view, PyBUF_SIMPLE) < 0) return NULL;
    uint32_t id = zxc_get_dict_id(view.buf, (size_t)view.len);
    PyBuffer_Release(&view);
    return PyLong_FromUnsignedLong(id);
}

/* Dictionary ID stored in a .zxd file (0 if not a valid .zxd). */
static PyObject* pyzxc_dict_get_id(PyObject* self, PyObject* arg) {
    (void)self;
    Py_buffer view;
    if (PyObject_GetBuffer(arg, &view, PyBUF_SIMPLE) < 0) return NULL;
    uint32_t id = zxc_dict_get_id(view.buf, (size_t)view.len);
    PyBuffer_Release(&view);
    return PyLong_FromUnsignedLong(id);
}

/* Serialize dictionary content + shared Huffman table into the .zxd format. */
static PyObject* pyzxc_dict_save(PyObject* self, PyObject* args) {
    (void)self;
    Py_buffer view;
    Py_buffer huf_view;
    if (!PyArg_ParseTuple(args, "y*y*", &view, &huf_view)) return NULL;
    if (huf_view.len != ZXC_HUF_TABLE_SIZE) {
        PyBuffer_Release(&view);
        PyBuffer_Release(&huf_view);
        PyErr_SetString(PyExc_ValueError, "huf_lengths must be exactly 128 bytes");
        return NULL;
    }

    size_t bound = zxc_dict_save_bound((size_t)view.len);
    PyObject* out = PyBytes_FromStringAndSize(NULL, (Py_ssize_t)bound);
    if (!out) {
        PyBuffer_Release(&view);
        PyBuffer_Release(&huf_view);
        return NULL;
    }

    int64_t written = zxc_dict_save(view.buf, (size_t)view.len, huf_view.buf,
                                    PyBytes_AS_STRING(out), bound);
    PyBuffer_Release(&view);
    PyBuffer_Release(&huf_view);

    if (written < 0) {
        Py_DECREF(out);
        Py_Return_Err(PyExc_RuntimeError, zxc_error_name((int)written));
    }
    if ((size_t)written != bound) {
        if (_PyBytes_Resize(&out, (Py_ssize_t)written) < 0) return NULL;
    }
    return out;
}

/* Parse a .zxd file; returns (content_bytes, dict_id). Content is copied out
 * because zxc_dict_load returns a pointer INTO the input buffer. */
static PyObject* pyzxc_dict_load(PyObject* self, PyObject* arg) {
    (void)self;
    Py_buffer view;
    if (PyObject_GetBuffer(arg, &view, PyBUF_SIMPLE) < 0) return NULL;

    const void* content = NULL;
    size_t content_size = 0;
    const void* huf = NULL;
    uint32_t dict_id = 0;

    int rc = zxc_dict_load(view.buf, (size_t)view.len, &content, &content_size, &huf, &dict_id);
    if (rc != 0) {
        PyBuffer_Release(&view);
        Py_Return_Err(PyExc_RuntimeError, zxc_error_name(rc));
    }

    /* Copy out before releasing the input buffer (zero-copy pointers). */
    PyObject* content_obj =
        PyBytes_FromStringAndSize((const char*)content, (Py_ssize_t)content_size);
    PyObject* huf_obj =
        PyBytes_FromStringAndSize((const char*)huf, (Py_ssize_t)ZXC_HUF_TABLE_SIZE);
    PyBuffer_Release(&view);
    if (!content_obj || !huf_obj) {
        Py_XDECREF(content_obj);
        Py_XDECREF(huf_obj);
        return NULL;
    }

    /* 'N' steals the references (consumed even on failure). */
    return Py_BuildValue("(NNk)", content_obj, huf_obj, (unsigned long)dict_id);
}

/* One-call dictionary creation: samples -> .zxd bytes. */
static PyObject* pyzxc_dict_train(PyObject* self, PyObject* args, PyObject* kwargs) {
    (void)self;
    PyObject* samples_obj;

    static char* kwlist[] = {"samples", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O", kwlist, &samples_obj)) {
        return NULL;
    }

    PyObject* seq = PySequence_Fast(samples_obj, "samples must be a sequence of bytes-like objects");
    if (!seq) return NULL;

    Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
    if (n <= 0) {
        Py_DECREF(seq);
        Py_Return_Err(PyExc_ValueError, "samples must be non-empty");
    }

    const void** ptrs = (const void**)PyMem_Malloc(sizeof(void*) * (size_t)n);
    size_t* sizes = (size_t*)PyMem_Malloc(sizeof(size_t) * (size_t)n);
    Py_buffer* views = (Py_buffer*)PyMem_Malloc(sizeof(Py_buffer) * (size_t)n);
    const size_t zxd_cap = zxc_dict_save_bound(ZXC_DICT_SIZE_MAX);
    uint8_t* zxd = (uint8_t*)PyMem_Malloc(zxd_cap);
    if (!ptrs || !sizes || !views || !zxd) {
        PyMem_Free(ptrs);
        PyMem_Free(sizes);
        PyMem_Free(views);
        PyMem_Free(zxd);
        Py_DECREF(seq);
        return PyErr_NoMemory();
    }

    Py_ssize_t acquired = 0;
    for (; acquired < n; acquired++) {
        PyObject* item = PySequence_Fast_GET_ITEM(seq, acquired);
        if (PyObject_GetBuffer(item, &views[acquired], PyBUF_SIMPLE) < 0) {
            break;
        }
        ptrs[acquired] = views[acquired].buf;
        sizes[acquired] = (size_t)views[acquired].len;
    }

    int64_t zxd_sz = ZXC_ERROR_NULL_INPUT;
    if (acquired == n) {
        Py_BEGIN_ALLOW_THREADS zxd_sz = zxc_dict_train(ptrs, sizes, (size_t)n, zxd, zxd_cap);
        Py_END_ALLOW_THREADS
    }

    for (Py_ssize_t i = 0; i < acquired; i++) PyBuffer_Release(&views[i]);
    PyMem_Free(ptrs);
    PyMem_Free(sizes);
    PyMem_Free(views);
    Py_DECREF(seq);

    PyObject* out = NULL;
    if (acquired != n) {
        /* GetBuffer already set the exception. */
    } else if (zxd_sz <= 0) {
        PyErr_SetString(PyExc_RuntimeError, zxc_error_name((int)zxd_sz));
    } else {
        out = PyBytes_FromStringAndSize((const char*)zxd, (Py_ssize_t)zxd_sz);
    }
    PyMem_Free(zxd);
    return out;
}


/* Shared Huffman table stored in a .zxd buffer, or None when invalid. */
static PyObject* pyzxc_dict_huf(PyObject* self, PyObject* arg) {
    (void)self;
    Py_buffer view;
    if (PyObject_GetBuffer(arg, &view, PyBUF_SIMPLE) < 0) return NULL;
    const void* huf = zxc_dict_huf(view.buf, (size_t)view.len);
    PyObject* out = huf ? PyBytes_FromStringAndSize((const char*)huf, ZXC_HUF_TABLE_SIZE)
                        : (Py_INCREF(Py_None), Py_None);
    PyBuffer_Release(&view);
    return out;
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

typedef struct {
    zxc_cstream* cs;
} pyzxc_cstream_holder_t;

typedef struct {
    zxc_dstream* ds;
} pyzxc_dstream_holder_t;

static void cstream_capsule_destructor(PyObject* capsule) {
    pyzxc_cstream_holder_t* h =
        (pyzxc_cstream_holder_t*)PyCapsule_GetPointer(capsule, ZXC_CSTREAM_CAPSULE);
    if (h) {
        if (h->cs) zxc_cstream_free(h->cs);
        PyMem_Free(h);
    }
}

static void dstream_capsule_destructor(PyObject* capsule) {
    pyzxc_dstream_holder_t* h =
        (pyzxc_dstream_holder_t*)PyCapsule_GetPointer(capsule, ZXC_DSTREAM_CAPSULE);
    if (h) {
        if (h->ds) zxc_dstream_free(h->ds);
        PyMem_Free(h);
    }
}

static zxc_cstream* cstream_from_capsule(PyObject* capsule) {
    pyzxc_cstream_holder_t* h =
        (pyzxc_cstream_holder_t*)PyCapsule_GetPointer(capsule, ZXC_CSTREAM_CAPSULE);
    if (!h || !h->cs) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_ValueError, "cstream is closed or invalid");
        return NULL;
    }
    return h->cs;
}

static zxc_dstream* dstream_from_capsule(PyObject* capsule) {
    pyzxc_dstream_holder_t* h =
        (pyzxc_dstream_holder_t*)PyCapsule_GetPointer(capsule, ZXC_DSTREAM_CAPSULE);
    if (!h || !h->ds) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_ValueError, "dstream is closed or invalid");
        return NULL;
    }
    return h->ds;
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
    /* 0 = library default; otherwise must be a power of two in
     * [ZXC_BLOCK_SIZE_MIN, ZXC_BLOCK_SIZE_MAX] — zxc_cstream_create returns
     * NULL for invalid values, which must not surface as MemoryError. */
    if (block_size != 0 &&
        (block_size < (Py_ssize_t)ZXC_BLOCK_SIZE_MIN || block_size > (Py_ssize_t)ZXC_BLOCK_SIZE_MAX ||
         (block_size & (block_size - 1)) != 0)) {
        PyErr_Format(PyExc_ValueError,
                     "block_size must be 0 (default) or a power of two in [%u, %u], got %zd",
                     ZXC_BLOCK_SIZE_MIN, ZXC_BLOCK_SIZE_MAX, block_size);
        return NULL;
    }
    if (level < zxc_min_level() || level > zxc_max_level()) {
        PyErr_Format(PyExc_ValueError, "level must be in [%d, %d], got %d", zxc_min_level(),
                     zxc_max_level(), level);
        return NULL;
    }

    zxc_compress_opts_t copts = {0};
    copts.level = level;
    copts.checksum_enabled = checksum;
    copts.block_size = (size_t)block_size;

    zxc_cstream* cs = zxc_cstream_create(&copts);
    if (!cs) Py_Return_Err(PyExc_MemoryError, "zxc_cstream_create failed");

    pyzxc_cstream_holder_t* h = (pyzxc_cstream_holder_t*)PyMem_Malloc(sizeof(*h));
    if (!h) {
        zxc_cstream_free(cs);
        return PyErr_NoMemory();
    }
    h->cs = cs;
    PyObject* cap = PyCapsule_New(h, ZXC_CSTREAM_CAPSULE, cstream_capsule_destructor);
    if (!cap) {
        zxc_cstream_free(cs);
        PyMem_Free(h);
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
    uint8_t* out_buf = (uint8_t*)malloc(out_cap);
    if (!out_buf) {
        PyBuffer_Release(&view);
        return PyErr_NoMemory();
    }

    zxc_inbuf_t in = {.src = view.buf, .size = (size_t)view.len, .pos = 0};
    int err_code = 0;
    int oom = 0;
    int overflow = 0;

    Py_BEGIN_ALLOW_THREADS for (;;) {
        /* Make sure there's room to write at least one block worth. */
        size_t want = zxc_cstream_out_size(cs);
        if (want < 4096) want = 4096;
        if (out_cap - out_len < want) {
            int rc = grow_output(&out_buf, &out_cap, out_len, want);
            if (rc == -1) {
                overflow = 1;
                break;
            }
            if (rc == -2) {
                oom = 1;
                break;
            }
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

    if (oom) PyErr_NoMemory();
    else if (overflow)
        PyErr_SetString(PyExc_OverflowError, "compressed output exceeds PY_SSIZE_T_MAX");
    else if (err_code) PyErr_SetString(PyExc_RuntimeError, zxc_error_name(err_code));

    if (PyErr_Occurred()) {
        free(out_buf);
        return NULL;
    }

    PyObject* result = PyBytes_FromStringAndSize((const char*)out_buf, (Py_ssize_t)out_len);
    free(out_buf);
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
    uint8_t* out_buf = (uint8_t*)malloc(out_cap);
    if (!out_buf) return PyErr_NoMemory();

    int err_code = 0;
    int oom = 0;
    int overflow = 0;

    Py_BEGIN_ALLOW_THREADS for (;;) {
        if (out_cap - out_len < 4096) {
            int rc = grow_output(&out_buf, &out_cap, out_len, 4096);
            if (rc == -1) {
                overflow = 1;
                break;
            }
            if (rc == -2) {
                oom = 1;
                break;
            }
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

    if (oom) PyErr_NoMemory();
    else if (overflow)
        PyErr_SetString(PyExc_OverflowError, "compressed output exceeds PY_SSIZE_T_MAX");
    else if (err_code) PyErr_SetString(PyExc_RuntimeError, zxc_error_name(err_code));

    if (PyErr_Occurred()) {
        free(out_buf);
        return NULL;
    }
    PyObject* result = PyBytes_FromStringAndSize((const char*)out_buf, (Py_ssize_t)out_len);
    free(out_buf);
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
    pyzxc_cstream_holder_t* h =
        (pyzxc_cstream_holder_t*)PyCapsule_GetPointer(capsule, ZXC_CSTREAM_CAPSULE);
    if (h && h->cs) {
        zxc_cstream_free(h->cs);
        h->cs = NULL;
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

    pyzxc_dstream_holder_t* h = (pyzxc_dstream_holder_t*)PyMem_Malloc(sizeof(*h));
    if (!h) {
        zxc_dstream_free(ds);
        return PyErr_NoMemory();
    }
    h->ds = ds;
    PyObject* cap = PyCapsule_New(h, ZXC_DSTREAM_CAPSULE, dstream_capsule_destructor);
    if (!cap) {
        zxc_dstream_free(ds);
        PyMem_Free(h);
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
    uint8_t* out_buf = (uint8_t*)malloc(out_cap);
    if (!out_buf) {
        PyBuffer_Release(&view);
        return PyErr_NoMemory();
    }

    zxc_inbuf_t in = {.src = view.buf, .size = (size_t)view.len, .pos = 0};
    int err_code = 0;
    int oom = 0;
    int overflow = 0;

    Py_BEGIN_ALLOW_THREADS for (;;) {
        size_t want = zxc_dstream_out_size(ds);
        if (want < 4096) want = 4096;
        if (out_cap - out_len < want) {
            int rc = grow_output(&out_buf, &out_cap, out_len, want);
            if (rc == -1) {
                overflow = 1;
                break;
            }
            if (rc == -2) {
                oom = 1;
                break;
            }
        }
        zxc_outbuf_t out = {.dst = out_buf + out_len, .size = out_cap - out_len, .pos = 0};
        zxc_inbuf_t empty_in = {.src = NULL, .size = 0, .pos = 0};
        zxc_inbuf_t* cur_in = (in.pos < in.size) ? &in : &empty_in;
        const size_t before_in = cur_in->pos;
        const size_t before_out = out.pos;
        const int64_t r = zxc_dstream_decompress(ds, &out, cur_in);
        out_len += out.pos;
        if (r < 0) {
            err_code = (int)r;
            break;
        }
        /* Keep draining even after input is exhausted; stop only when no
         * progress was made (no input consumed AND no output produced). */
        if (cur_in->pos == before_in && out.pos == before_out) break;
    }
    Py_END_ALLOW_THREADS

        PyBuffer_Release(&view);

    if (oom) PyErr_NoMemory();
    else if (overflow)
        PyErr_SetString(PyExc_OverflowError, "decompressed output exceeds PY_SSIZE_T_MAX");
    else if (err_code) PyErr_SetString(PyExc_RuntimeError, zxc_error_name(err_code));

    if (PyErr_Occurred()) {
        free(out_buf);
        return NULL;
    }

    PyObject* result = PyBytes_FromStringAndSize((const char*)out_buf, (Py_ssize_t)out_len);
    free(out_buf);
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
    pyzxc_dstream_holder_t* h =
        (pyzxc_dstream_holder_t*)PyCapsule_GetPointer(capsule, ZXC_DSTREAM_CAPSULE);
    if (h && h->ds) {
        zxc_dstream_free(h->ds);
        h->ds = NULL;
    }
    Py_RETURN_NONE;
}

// =============================================================================
// Seekable API (random-access decompression)
// =============================================================================

#define ZXC_SEEKABLE_CAPSULE "zxc_seekable"

typedef struct {
    zxc_seekable* s;
    Py_buffer src_view;   /* held view on the source when opened from bytes:
                             pins the exporter for the handle's lifetime so no
                             copy of the archive is needed */
    int has_view;         /* 1 when src_view must be released */
    PyObject* reader_obj; /* strong ref to the Python reader when using callback */
    /* First exception raised by the reader callback, stashed under the GIL by
     * the trampoline (which may run on a library worker thread) and re-raised
     * on the calling thread once the C call returns. */
    PyObject* exc_type;
    PyObject* exc_value;
    PyObject* exc_tb;
} pyzxc_seekable_holder_t;

/* Stores the currently-raised exception into the holder (first one wins) so
 * the user's traceback survives the round-trip through the C library.
 * GIL must be held. */
static void seekable_stash_exception(pyzxc_seekable_holder_t* h) {
    if (h->exc_type) {
        PyErr_Clear();
        return;
    }
    PyErr_Fetch(&h->exc_type, &h->exc_value, &h->exc_tb);
}

/* Re-raises a stashed exception (if any) and clears the slots.
 * GIL must be held. Returns 1 when an exception was restored. */
static int seekable_restore_exception(pyzxc_seekable_holder_t* h) {
    if (!h->exc_type) return 0;
    PyErr_Restore(h->exc_type, h->exc_value, h->exc_tb);
    h->exc_type = NULL;
    h->exc_value = NULL;
    h->exc_tb = NULL;
    return 1;
}

static void seekable_capsule_destructor(PyObject* capsule) {
    pyzxc_seekable_holder_t* h =
        (pyzxc_seekable_holder_t*)PyCapsule_GetPointer(capsule, ZXC_SEEKABLE_CAPSULE);
    if (h) {
        if (h->s) zxc_seekable_free(h->s);
        if (h->has_view) PyBuffer_Release(&h->src_view);
        Py_XDECREF(h->reader_obj);
        Py_XDECREF(h->exc_type);
        Py_XDECREF(h->exc_value);
        Py_XDECREF(h->exc_tb);
        PyMem_Free(h);
    }
}

static zxc_seekable* seekable_from_capsule(PyObject* capsule) {
    pyzxc_seekable_holder_t* h =
        (pyzxc_seekable_holder_t*)PyCapsule_GetPointer(capsule, ZXC_SEEKABLE_CAPSULE);
    if (!h || !h->s) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_ValueError, "Seekable is closed or invalid");
        return NULL;
    }
    return h->s;
}

static PyObject* pyzxc_seekable_open(PyObject* self, PyObject* arg) {
    (void)self;
    Py_buffer view;
    if (PyObject_GetBuffer(arg, &view, PyBUF_SIMPLE) < 0) return NULL;

    if (view.len == 0) {
        PyBuffer_Release(&view);
        Py_Return_Err(PyExc_ValueError, "seekable buffer must be non-empty");
    }

    /* The view pins the exporter (and, for a bytearray, blocks resizing)
     * for the handle's lifetime, so the library can reference the caller's
     * buffer directly — no copy of the archive. */
    zxc_seekable* s;
    Py_BEGIN_ALLOW_THREADS s = zxc_seekable_open(view.buf, (size_t)view.len);
    Py_END_ALLOW_THREADS

    if (!s) {
        PyBuffer_Release(&view);
        Py_Return_Err(PyExc_RuntimeError, "zxc_seekable_open failed (not a valid seekable archive)");
    }

    pyzxc_seekable_holder_t* h = (pyzxc_seekable_holder_t*)PyMem_Malloc(sizeof(*h));
    if (!h) {
        zxc_seekable_free(s);
        PyBuffer_Release(&view);
        return PyErr_NoMemory();
    }
    h->s = s;
    h->src_view = view;
    h->has_view = 1;
    h->reader_obj = NULL;
    h->exc_type = NULL;
    h->exc_value = NULL;
    h->exc_tb = NULL;

    PyObject* cap = PyCapsule_New(h, ZXC_SEEKABLE_CAPSULE, seekable_capsule_destructor);
    if (!cap) {
        zxc_seekable_free(s);
        PyBuffer_Release(&h->src_view);
        PyMem_Free(h);
        return NULL;
    }
    return cap;
}

/* C trampoline for the Python reader callback.  May be invoked from the
 * calling thread (single-threaded decode, with or without the GIL held) or
 * from library-spawned worker threads (multi-threaded decode), so it always
 * attaches to the interpreter with PyGILState_Ensure.  A failing callback
 * stashes its exception into the holder for re-raise by the caller. */
static int64_t seekable_read_at_trampoline(void* ctx, void* dst, size_t len, uint64_t offset) {
    pyzxc_seekable_holder_t* h = (pyzxc_seekable_holder_t*)ctx;
    PyGILState_STATE gstate = PyGILState_Ensure();
    int64_t rc = ZXC_ERROR_IO;

    PyObject* result = PyObject_CallMethod(h->reader_obj, "read_at", "nK", (Py_ssize_t)len,
                                           (unsigned long long)offset);
    if (!result) {
        seekable_stash_exception(h);
        goto done;
    }

    Py_buffer view;
    if (PyObject_GetBuffer(result, &view, PyBUF_SIMPLE) < 0) {
        Py_DECREF(result);
        PyErr_Clear();
        goto done;
    }
    if ((size_t)view.len < len) {
        PyBuffer_Release(&view);
        Py_DECREF(result);
        goto done;
    }
    memcpy(dst, view.buf, len);
    PyBuffer_Release(&view);
    Py_DECREF(result);
    rc = (int64_t)len;

done:
    PyGILState_Release(gstate);
    return rc;
}

static PyObject* pyzxc_seekable_open_reader(PyObject* self, PyObject* arg) {
    (void)self;
    if (!PyObject_HasAttrString(arg, "size") || !PyObject_HasAttrString(arg, "read_at")) {
        Py_Return_Err(PyExc_TypeError, "reader must have 'size' (int) and 'read_at' (callable)");
    }

    PyObject* size_obj = PyObject_GetAttrString(arg, "size");
    if (!size_obj) return NULL;
    long long sz = PyLong_AsLongLong(size_obj);
    Py_DECREF(size_obj);
    if (sz == -1 && PyErr_Occurred()) return NULL;
    if (sz <= 0) Py_Return_Err(PyExc_ValueError, "reader.size must be > 0");

    /* The holder is created before the open call because it doubles as the
     * trampoline context (reader object + stashed-exception slots). */
    pyzxc_seekable_holder_t* h = (pyzxc_seekable_holder_t*)PyMem_Malloc(sizeof(*h));
    if (!h) return PyErr_NoMemory();
    h->s = NULL;
    h->has_view = 0;
    Py_INCREF(arg);
    h->reader_obj = arg;
    h->exc_type = NULL;
    h->exc_value = NULL;
    h->exc_tb = NULL;

    zxc_reader_t r;
    r.read_at = seekable_read_at_trampoline;
    r.ctx = h;
    r.size = (uint64_t)sz;

    zxc_seekable* s = zxc_seekable_open_reader(&r);
    if (!s) {
        int restored = seekable_restore_exception(h);
        Py_DECREF(arg);
        PyMem_Free(h);
        if (restored) return NULL; /* re-raise the reader's own exception */
        Py_Return_Err(PyExc_RuntimeError,
                      "zxc_seekable_open_reader failed (not a valid seekable archive)");
    }
    h->s = s;

    PyObject* cap = PyCapsule_New(h, ZXC_SEEKABLE_CAPSULE, seekable_capsule_destructor);
    if (!cap) {
        zxc_seekable_free(s);
        Py_DECREF(arg);
        PyMem_Free(h);
        return NULL;
    }
    return cap;
}

static PyObject* pyzxc_seekable_num_blocks(PyObject* self, PyObject* capsule) {
    (void)self;
    zxc_seekable* s = seekable_from_capsule(capsule);
    if (!s) return NULL;
    return PyLong_FromUnsignedLong(zxc_seekable_get_num_blocks(s));
}

static PyObject* pyzxc_seekable_decompressed_size(PyObject* self, PyObject* capsule) {
    (void)self;
    zxc_seekable* s = seekable_from_capsule(capsule);
    if (!s) return NULL;
    return PyLong_FromUnsignedLongLong(zxc_seekable_get_decompressed_size(s));
}

static PyObject* pyzxc_seekable_block_comp_size(PyObject* self, PyObject* args) {
    (void)self;
    PyObject* capsule;
    unsigned int idx;
    if (!PyArg_ParseTuple(args, "OI", &capsule, &idx)) return NULL;

    zxc_seekable* s = seekable_from_capsule(capsule);
    if (!s) return NULL;

    if (idx >= zxc_seekable_get_num_blocks(s)) Py_RETURN_NONE;
    return PyLong_FromUnsignedLong(zxc_seekable_get_block_comp_size(s, idx));
}

static PyObject* pyzxc_seekable_block_decomp_size(PyObject* self, PyObject* args) {
    (void)self;
    PyObject* capsule;
    unsigned int idx;
    if (!PyArg_ParseTuple(args, "OI", &capsule, &idx)) return NULL;

    zxc_seekable* s = seekable_from_capsule(capsule);
    if (!s) return NULL;

    if (idx >= zxc_seekable_get_num_blocks(s)) Py_RETURN_NONE;
    return PyLong_FromUnsignedLong(zxc_seekable_get_block_decomp_size(s, idx));
}

static PyObject* pyzxc_seekable_decompress_range(PyObject* self, PyObject* args, PyObject* kwargs) {
    (void)self;
    PyObject* capsule;
    unsigned long long offset;
    Py_ssize_t length;
    int n_threads = 0;

    static char* kwlist[] = {"handle", "offset", "length", "n_threads", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OKn|i", kwlist, &capsule, &offset, &length,
                                     &n_threads)) {
        return NULL;
    }

    if (length < 0) Py_Return_Err(PyExc_ValueError, "length must be non-negative");

    zxc_seekable* s = seekable_from_capsule(capsule);
    if (!s) return NULL;

    if (length == 0) return PyBytes_FromStringAndSize(NULL, 0);

    PyObject* out = PyBytes_FromStringAndSize(NULL, length);
    if (!out) return NULL;
    char* dst = PyBytes_AS_STRING(out);

    int64_t r;
    pyzxc_seekable_holder_t* h =
        (pyzxc_seekable_holder_t*)PyCapsule_GetPointer(capsule, ZXC_SEEKABLE_CAPSULE);

    /* The GIL is released on every path: the reader trampoline re-attaches
     * with PyGILState_Ensure, which also makes the multi-threaded decode safe
     * when the library invokes the Python callback from its worker threads
     * (holding the GIL here would let workers call into Python without it). */
    Py_BEGIN_ALLOW_THREADS if (n_threads > 1) {
        r = zxc_seekable_decompress_range_mt(s, dst, (size_t)length, (uint64_t)offset,
                                             (size_t)length, n_threads);
    }
    else {
        r = zxc_seekable_decompress_range(s, dst, (size_t)length, (uint64_t)offset,
                                          (size_t)length);
    }
    Py_END_ALLOW_THREADS

    if (r < 0) {
        Py_DECREF(out);
        /* Prefer the reader's own exception (with traceback) when it caused
         * the failure; otherwise report the library error code. */
        if (h && seekable_restore_exception(h)) return NULL;
        Py_Return_Err(PyExc_RuntimeError, zxc_error_name((int)r));
    }
    /* A callback may have failed on one worker while another satisfied the
     * range; drop any stale stashed exception so it cannot leak into an
     * unrelated later call. */
    if (h && h->exc_type) {
        Py_CLEAR(h->exc_type);
        Py_CLEAR(h->exc_value);
        Py_CLEAR(h->exc_tb);
    }

    if (r != (int64_t)length && _PyBytes_Resize(&out, (Py_ssize_t)r) < 0) return NULL;
    return out;
}

static PyObject* pyzxc_seekable_free(PyObject* self, PyObject* capsule) {
    (void)self;
    if (!PyCapsule_IsValid(capsule, ZXC_SEEKABLE_CAPSULE)) Py_RETURN_NONE;
    pyzxc_seekable_holder_t* h =
        (pyzxc_seekable_holder_t*)PyCapsule_GetPointer(capsule, ZXC_SEEKABLE_CAPSULE);
    if (h && h->s) {
        zxc_seekable_free(h->s);
        h->s = NULL;
    }
    if (h && h->has_view) {
        PyBuffer_Release(&h->src_view);
        h->has_view = 0;
    }
    if (h && h->reader_obj) {
        Py_DECREF(h->reader_obj);
        h->reader_obj = NULL;
    }
    Py_RETURN_NONE;
}

static PyObject* pyzxc_seekable_set_dict(PyObject* self, PyObject* args) {
    (void)self;
    PyObject* capsule;
    Py_buffer view;
    PyObject* dict_huf_obj = NULL;
    if (!PyArg_ParseTuple(args, "Oy*|O", &capsule, &view, &dict_huf_obj)) return NULL;

    zxc_seekable* s = seekable_from_capsule(capsule);
    if (!s) {
        PyBuffer_Release(&view);
        return NULL;
    }

    uint8_t huf_local[ZXC_HUF_TABLE_SIZE];
    const void* dict_huf = NULL;
    if (dict_huf_obj && dict_huf_obj != Py_None) {
        Py_buffer hv;
        if (PyObject_GetBuffer(dict_huf_obj, &hv, PyBUF_SIMPLE) < 0) {
            PyBuffer_Release(&view);
            return NULL;
        }
        if (hv.len != ZXC_HUF_TABLE_SIZE) {
            PyBuffer_Release(&hv);
            PyBuffer_Release(&view);
            PyErr_SetString(PyExc_ValueError, "dict_huf must be exactly 128 bytes");
            return NULL;
        }
        memcpy(huf_local, hv.buf, ZXC_HUF_TABLE_SIZE);
        PyBuffer_Release(&hv);
        dict_huf = huf_local;
    }

    int rc = zxc_seekable_set_dict(s, view.buf, (size_t)view.len, dict_huf);
    PyBuffer_Release(&view);

    if (rc != 0) Py_Return_Err(PyExc_RuntimeError, zxc_error_name(rc));
    Py_RETURN_NONE;
}

static PyObject* pyzxc_seek_table_size(PyObject* self, PyObject* arg) {
    (void)self;
    unsigned int num_blocks = (unsigned int)PyLong_AsUnsignedLong(arg);
    if (num_blocks == (unsigned int)-1 && PyErr_Occurred()) return NULL;
    return PyLong_FromSize_t(zxc_seek_table_size(num_blocks));
}

static PyObject* pyzxc_write_seek_table(PyObject* self, PyObject* arg) {
    (void)self;
    if (!PyList_Check(arg)) Py_Return_Err(PyExc_TypeError, "expected a list of integers");

    Py_ssize_t n = PyList_GET_SIZE(arg);
    if (n <= 0) Py_Return_Err(PyExc_ValueError, "comp_sizes must be non-empty");

    uint32_t* sizes = (uint32_t*)PyMem_Malloc(sizeof(uint32_t) * (size_t)n);
    if (!sizes) return PyErr_NoMemory();

    for (Py_ssize_t i = 0; i < n; i++) {
        unsigned long v = PyLong_AsUnsignedLong(PyList_GET_ITEM(arg, i));
        if (v == (unsigned long)-1 && PyErr_Occurred()) {
            PyMem_Free(sizes);
            return NULL;
        }
        sizes[i] = (uint32_t)v;
    }

    size_t cap = zxc_seek_table_size((uint32_t)n);
    PyObject* out = PyBytes_FromStringAndSize(NULL, (Py_ssize_t)cap);
    if (!out) {
        PyMem_Free(sizes);
        return NULL;
    }

    int64_t written =
        zxc_write_seek_table((uint8_t*)PyBytes_AS_STRING(out), cap, sizes, (uint32_t)n);
    PyMem_Free(sizes);

    if (written < 0) {
        Py_DECREF(out);
        Py_Return_Err(PyExc_RuntimeError, zxc_error_name((int)written));
    }
    if ((size_t)written != cap && _PyBytes_Resize(&out, (Py_ssize_t)written) < 0) return NULL;
    return out;
}
