/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <napi.h>

#include <vector>

extern "C" {
#include "zxc.h"
}

// =============================================================================
// compressBound(inputSize: number): number
// =============================================================================
static Napi::Value CompressBound(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Expected a number (inputSize)").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    uint64_t input_size = info[0].As<Napi::Number>().Int64Value();
    uint64_t bound = zxc_compress_bound(static_cast<size_t>(input_size));

    return Napi::Number::New(env, static_cast<double>(bound));
}

// =============================================================================
// compress(buffer: Buffer, level?: number, checksum?: boolean, seekable?: boolean): Buffer
// =============================================================================
static Napi::Value Compress(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsBuffer()) {
        Napi::TypeError::New(env, "Expected a Buffer as first argument")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Buffer<uint8_t> src_buf = info[0].As<Napi::Buffer<uint8_t>>();
    const void* src = src_buf.Data();
    size_t src_size = src_buf.Length();

    int level = ZXC_LEVEL_DEFAULT;
    if (info.Length() >= 2 && info[1].IsNumber()) {
        level = info[1].As<Napi::Number>().Int32Value();
    }

    int checksum = 0;
    if (info.Length() >= 3 && info[2].IsBoolean()) {
        checksum = info[2].As<Napi::Boolean>().Value() ? 1 : 0;
    }

    int seekable = 0;
    if (info.Length() >= 4 && info[3].IsBoolean()) {
        seekable = info[3].As<Napi::Boolean>().Value() ? 1 : 0;
    }

    // Handle empty input
    if (src_size == 0 && !checksum) {
        return Napi::Buffer<uint8_t>::New(env, 0);
    }

    uint64_t bound = zxc_compress_bound(src_size);
    Napi::Buffer<uint8_t> dst_buf = Napi::Buffer<uint8_t>::New(env, static_cast<size_t>(bound));

    zxc_compress_opts_t opts = {0};
    opts.level = level;
    opts.checksum_enabled = checksum;
    opts.seekable = seekable;

    int64_t nwritten =
        zxc_compress(src, src_size, dst_buf.Data(), static_cast<size_t>(bound), &opts);

    if (nwritten < 0) {
        int err_code = static_cast<int>(nwritten);
        Napi::Error err = Napi::Error::New(env, zxc_error_name(err_code));
        err.Set("code", Napi::Number::New(env, err_code));
        err.ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (nwritten == 0) {
        Napi::Error::New(env, "Input is too small to be compressed").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    // Return a slice of the buffer with the actual size
    return Napi::Buffer<uint8_t>::Copy(env, dst_buf.Data(), static_cast<size_t>(nwritten));
}

// =============================================================================
// decompress(buffer: Buffer, decompressSize: number, checksum?: boolean): Buffer
// =============================================================================
static Napi::Value Decompress(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2 || !info[0].IsBuffer() || !info[1].IsNumber()) {
        Napi::TypeError::New(env, "Expected (Buffer, number) as arguments")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Buffer<uint8_t> src_buf = info[0].As<Napi::Buffer<uint8_t>>();
    const void* src = src_buf.Data();
    size_t src_size = src_buf.Length();
    size_t decompress_size = static_cast<size_t>(info[1].As<Napi::Number>().Int64Value());

    int checksum = 0;
    if (info.Length() >= 3 && info[2].IsBoolean()) {
        checksum = info[2].As<Napi::Boolean>().Value() ? 1 : 0;
    }

    Napi::Buffer<uint8_t> dst_buf = Napi::Buffer<uint8_t>::New(env, decompress_size);

    zxc_decompress_opts_t dopts = {0};
    dopts.checksum_enabled = checksum;

    int64_t nwritten = zxc_decompress(src, src_size, dst_buf.Data(), decompress_size, &dopts);

    if (nwritten < 0) {
        int err_code = static_cast<int>(nwritten);
        Napi::Error err = Napi::Error::New(env, zxc_error_name(err_code));
        err.Set("code", Napi::Number::New(env, err_code));
        err.ThrowAsJavaScriptException();
        return env.Undefined();
    }

    return dst_buf;
}

// =============================================================================
// getDecompressedSize(buffer: Buffer): number
// =============================================================================
static Napi::Value GetDecompressedSize(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsBuffer()) {
        Napi::TypeError::New(env, "Expected a Buffer as first argument")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    Napi::Buffer<uint8_t> src_buf = info[0].As<Napi::Buffer<uint8_t>>();
    uint64_t size = zxc_get_decompressed_size(src_buf.Data(), src_buf.Length());

    return Napi::Number::New(env, static_cast<double>(size));
}

// =============================================================================
// minLevel(): number
// maxLevel(): number
// defaultLevel(): number
// libraryVersion(): string
// =============================================================================
static Napi::Value MinLevel(const Napi::CallbackInfo& info) {
    return Napi::Number::New(info.Env(), zxc_min_level());
}

static Napi::Value MaxLevel(const Napi::CallbackInfo& info) {
    return Napi::Number::New(info.Env(), zxc_max_level());
}

static Napi::Value DefaultLevel(const Napi::CallbackInfo& info) {
    return Napi::Number::New(info.Env(), zxc_default_level());
}

static Napi::Value LibraryVersion(const Napi::CallbackInfo& info) {
    const char* v = zxc_version_string();
    return Napi::String::New(info.Env(), v ? v : "");
}

// =============================================================================
// Push Streaming API (single-threaded, caller-driven)
// =============================================================================
//
// The C contract drives input/output via mutable structs and reentrant
// calls. We expose this to JS as two classes (CStream / DStream) whose
// methods take a Buffer in and return a Buffer out, hiding the loop. A
// thin Buffer over std::vector<uint8_t> growing on demand is used to
// accumulate the output across multiple drain rounds.

static Napi::Value ThrowZxcError(Napi::Env env, int code) {
    Napi::Error err = Napi::Error::New(env, zxc_error_name(code));
    err.Set("code", Napi::Number::New(env, code));
    err.ThrowAsJavaScriptException();
    return env.Undefined();
}

/* Grow `out` so at least `out_len + want` bytes are addressable. Doubles when
 * possible, caps at vector::max_size(), and throws if the request can't fit. */
static bool GrowOutput(Napi::Env env, std::vector<uint8_t>& out,
                       size_t out_len, size_t want) {
    const size_t cap = out.max_size();
    if (want > cap - out_len) {
        Napi::Error::New(env, "output buffer size overflow")
            .ThrowAsJavaScriptException();
        return false;
    }
    const size_t needed = out_len + want;
    size_t new_size = (out.size() > cap / 2) ? cap : out.size() * 2;
    if (new_size < needed) new_size = needed;
    out.resize(new_size);
    return true;
}

class CStreamWrap : public Napi::ObjectWrap<CStreamWrap> {
   public:
    static Napi::Function GetClass(Napi::Env env) {
        return DefineClass(env, "CStream",
                           {
                               InstanceMethod("compress", &CStreamWrap::Compress),
                               InstanceMethod("end", &CStreamWrap::End),
                               InstanceMethod("close", &CStreamWrap::Close),
                               InstanceMethod("inSize", &CStreamWrap::InSize),
                               InstanceMethod("outSize", &CStreamWrap::OutSize),
                           });
    }

    CStreamWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<CStreamWrap>(info) {
        Napi::Env env = info.Env();
        zxc_compress_opts_t opts = {0};
        opts.level = ZXC_LEVEL_DEFAULT;

        if (info.Length() >= 1 && info[0].IsObject()) {
            Napi::Object o = info[0].As<Napi::Object>();
            if (o.Has("level") && o.Get("level").IsNumber()) {
                opts.level = o.Get("level").As<Napi::Number>().Int32Value();
            }
            if (o.Has("checksum") && o.Get("checksum").IsBoolean()) {
                opts.checksum_enabled = o.Get("checksum").As<Napi::Boolean>().Value() ? 1 : 0;
            }
            if (o.Has("blockSize") && o.Get("blockSize").IsNumber()) {
                opts.block_size =
                    static_cast<size_t>(o.Get("blockSize").As<Napi::Number>().Int64Value());
            }
        }

        cs_ = zxc_cstream_create(&opts);
        if (!cs_) {
            Napi::Error::New(env, "zxc_cstream_create failed").ThrowAsJavaScriptException();
        }
    }

    ~CStreamWrap() {
        if (cs_) zxc_cstream_free(cs_);
    }

   private:
    zxc_cstream* cs_ = nullptr;

    bool requireOpen(Napi::Env env) {
        if (!cs_) {
            Napi::Error::New(env, "CStream is closed").ThrowAsJavaScriptException();
            return false;
        }
        return true;
    }

    /* Drains from `cs` until either a stop condition is reached. The
     * caller-supplied `step` decides whether one iteration of
     * zxc_cstream_compress / _end has fully drained. */
    Napi::Value DrainCompress(Napi::Env env, const uint8_t* src, size_t srcLen) {
        std::vector<uint8_t> out;
        size_t cap = zxc_cstream_out_size(cs_);
        if (cap < 4096) cap = 4096;
        out.resize(cap);
        size_t out_len = 0;

        zxc_inbuf_t in = {src, srcLen, 0};
        for (;;) {
            size_t want = zxc_cstream_out_size(cs_);
            if (want < 4096) want = 4096;
            if (out.size() - out_len < want) {
                if (!GrowOutput(env, out, out_len, want)) return env.Undefined();
            }
            zxc_outbuf_t obuf = {out.data() + out_len, out.size() - out_len, 0};
            int64_t r = zxc_cstream_compress(cs_, &obuf, &in);
            out_len += obuf.pos;
            if (r < 0) return ThrowZxcError(env, static_cast<int>(r));
            if (r == 0 && in.pos == in.size) break;
        }
        return Napi::Buffer<uint8_t>::Copy(env, out.data(), out_len);
    }

    Napi::Value DrainEnd(Napi::Env env) {
        std::vector<uint8_t> out;
        size_t cap = zxc_cstream_out_size(cs_);
        if (cap < 4096) cap = 4096;
        out.resize(cap);
        size_t out_len = 0;

        for (;;) {
            if (out.size() - out_len < 4096) {
                if (!GrowOutput(env, out, out_len, 4096)) return env.Undefined();
            }
            zxc_outbuf_t obuf = {out.data() + out_len, out.size() - out_len, 0};
            int64_t r = zxc_cstream_end(cs_, &obuf);
            out_len += obuf.pos;
            if (r < 0) return ThrowZxcError(env, static_cast<int>(r));
            if (r == 0) break;
        }
        return Napi::Buffer<uint8_t>::Copy(env, out.data(), out_len);
    }

    Napi::Value Compress(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!requireOpen(env)) return env.Undefined();
        if (info.Length() < 1 || !info[0].IsBuffer()) {
            Napi::TypeError::New(env, "Expected a Buffer").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();
        return DrainCompress(env, buf.Data(), buf.Length());
    }

    Napi::Value End(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!requireOpen(env)) return env.Undefined();
        return DrainEnd(env);
    }

    Napi::Value Close(const Napi::CallbackInfo& info) {
        if (cs_) {
            zxc_cstream_free(cs_);
            cs_ = nullptr;
        }
        return info.Env().Undefined();
    }

    Napi::Value InSize(const Napi::CallbackInfo& info) {
        return Napi::Number::New(info.Env(),
                                 static_cast<double>(cs_ ? zxc_cstream_in_size(cs_) : 0));
    }

    Napi::Value OutSize(const Napi::CallbackInfo& info) {
        return Napi::Number::New(info.Env(),
                                 static_cast<double>(cs_ ? zxc_cstream_out_size(cs_) : 0));
    }
};

class DStreamWrap : public Napi::ObjectWrap<DStreamWrap> {
   public:
    static Napi::Function GetClass(Napi::Env env) {
        return DefineClass(env, "DStream",
                           {
                               InstanceMethod("decompress", &DStreamWrap::Decompress),
                               InstanceMethod("close", &DStreamWrap::Close),
                               InstanceMethod("finished", &DStreamWrap::Finished),
                               InstanceMethod("inSize", &DStreamWrap::InSize),
                               InstanceMethod("outSize", &DStreamWrap::OutSize),
                           });
    }

    DStreamWrap(const Napi::CallbackInfo& info) : Napi::ObjectWrap<DStreamWrap>(info) {
        Napi::Env env = info.Env();
        zxc_decompress_opts_t opts = {0};
        if (info.Length() >= 1 && info[0].IsObject()) {
            Napi::Object o = info[0].As<Napi::Object>();
            if (o.Has("checksum") && o.Get("checksum").IsBoolean()) {
                opts.checksum_enabled = o.Get("checksum").As<Napi::Boolean>().Value() ? 1 : 0;
            }
        }
        ds_ = zxc_dstream_create(&opts);
        if (!ds_) {
            Napi::Error::New(env, "zxc_dstream_create failed").ThrowAsJavaScriptException();
        }
    }

    ~DStreamWrap() {
        if (ds_) zxc_dstream_free(ds_);
    }

   private:
    zxc_dstream* ds_ = nullptr;

    bool requireOpen(Napi::Env env) {
        if (!ds_) {
            Napi::Error::New(env, "DStream is closed").ThrowAsJavaScriptException();
            return false;
        }
        return true;
    }

    Napi::Value Decompress(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        if (!requireOpen(env)) return env.Undefined();
        if (info.Length() < 1 || !info[0].IsBuffer()) {
            Napi::TypeError::New(env, "Expected a Buffer").ThrowAsJavaScriptException();
            return env.Undefined();
        }
        Napi::Buffer<uint8_t> buf = info[0].As<Napi::Buffer<uint8_t>>();

        std::vector<uint8_t> out;
        size_t cap = zxc_dstream_out_size(ds_);
        if (cap < 4096) cap = 4096;
        out.resize(cap);
        size_t out_len = 0;

        zxc_inbuf_t in = {buf.Data(), buf.Length(), 0};
        for (;;) {
            size_t want = zxc_dstream_out_size(ds_);
            if (want < 4096) want = 4096;
            if (out.size() - out_len < want) {
                if (!GrowOutput(env, out, out_len, want)) return env.Undefined();
            }
            zxc_outbuf_t obuf = {out.data() + out_len, out.size() - out_len, 0};
            zxc_inbuf_t empty_in = {nullptr, 0, 0};
            zxc_inbuf_t* cur_in = (in.pos < in.size) ? &in : &empty_in;
            const size_t before_in = cur_in->pos;
            const size_t before_out = obuf.pos;
            const int64_t r = zxc_dstream_decompress(ds_, &obuf, cur_in);
            out_len += obuf.pos;
            if (r < 0) return ThrowZxcError(env, static_cast<int>(r));
            /* Keep draining even after input is exhausted; stop only when
             * no progress was made (no input consumed AND no output produced). */
            if (cur_in->pos == before_in && obuf.pos == before_out) break;
        }
        return Napi::Buffer<uint8_t>::Copy(env, out.data(), out_len);
    }

    Napi::Value Finished(const Napi::CallbackInfo& info) {
        return Napi::Boolean::New(info.Env(), ds_ ? zxc_dstream_finished(ds_) != 0 : false);
    }

    Napi::Value Close(const Napi::CallbackInfo& info) {
        if (ds_) {
            zxc_dstream_free(ds_);
            ds_ = nullptr;
        }
        return info.Env().Undefined();
    }

    Napi::Value InSize(const Napi::CallbackInfo& info) {
        return Napi::Number::New(info.Env(),
                                 static_cast<double>(ds_ ? zxc_dstream_in_size(ds_) : 0));
    }

    Napi::Value OutSize(const Napi::CallbackInfo& info) {
        return Napi::Number::New(info.Env(),
                                 static_cast<double>(ds_ ? zxc_dstream_out_size(ds_) : 0));
    }
};

// =============================================================================
// errorName(code: number): string
// =============================================================================
static Napi::Value ErrorName(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Expected a number (error code)").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    int code = info[0].As<Napi::Number>().Int32Value();
    const char* name = zxc_error_name(code);

    return Napi::String::New(env, name);
}

// =============================================================================
// Module initialization
// =============================================================================
static Napi::Object Init(Napi::Env env, Napi::Object exports) {
    // Functions
    exports.Set("compressBound", Napi::Function::New(env, CompressBound, "compressBound"));
    exports.Set("compress", Napi::Function::New(env, Compress, "compress"));
    exports.Set("decompress", Napi::Function::New(env, Decompress, "decompress"));
    exports.Set("getDecompressedSize",
                Napi::Function::New(env, GetDecompressedSize, "getDecompressedSize"));
    exports.Set("errorName", Napi::Function::New(env, ErrorName, "errorName"));

    // Library info helpers
    exports.Set("minLevel", Napi::Function::New(env, MinLevel, "minLevel"));
    exports.Set("maxLevel", Napi::Function::New(env, MaxLevel, "maxLevel"));
    exports.Set("defaultLevel", Napi::Function::New(env, DefaultLevel, "defaultLevel"));
    exports.Set("libraryVersion", Napi::Function::New(env, LibraryVersion, "libraryVersion"));

    // Push streaming classes
    exports.Set("CStream", CStreamWrap::GetClass(env));
    exports.Set("DStream", DStreamWrap::GetClass(env));

    // Compression level constants
    exports.Set("LEVEL_FASTEST", Napi::Number::New(env, ZXC_LEVEL_FASTEST));
    exports.Set("LEVEL_FAST", Napi::Number::New(env, ZXC_LEVEL_FAST));
    exports.Set("LEVEL_DEFAULT", Napi::Number::New(env, ZXC_LEVEL_DEFAULT));
    exports.Set("LEVEL_BALANCED", Napi::Number::New(env, ZXC_LEVEL_BALANCED));
    exports.Set("LEVEL_COMPACT", Napi::Number::New(env, ZXC_LEVEL_COMPACT));

    // Error constants
    exports.Set("ERROR_MEMORY", Napi::Number::New(env, ZXC_ERROR_MEMORY));
    exports.Set("ERROR_DST_TOO_SMALL", Napi::Number::New(env, ZXC_ERROR_DST_TOO_SMALL));
    exports.Set("ERROR_SRC_TOO_SMALL", Napi::Number::New(env, ZXC_ERROR_SRC_TOO_SMALL));
    exports.Set("ERROR_BAD_MAGIC", Napi::Number::New(env, ZXC_ERROR_BAD_MAGIC));
    exports.Set("ERROR_BAD_VERSION", Napi::Number::New(env, ZXC_ERROR_BAD_VERSION));
    exports.Set("ERROR_BAD_HEADER", Napi::Number::New(env, ZXC_ERROR_BAD_HEADER));
    exports.Set("ERROR_BAD_CHECKSUM", Napi::Number::New(env, ZXC_ERROR_BAD_CHECKSUM));
    exports.Set("ERROR_CORRUPT_DATA", Napi::Number::New(env, ZXC_ERROR_CORRUPT_DATA));
    exports.Set("ERROR_BAD_OFFSET", Napi::Number::New(env, ZXC_ERROR_BAD_OFFSET));
    exports.Set("ERROR_OVERFLOW", Napi::Number::New(env, ZXC_ERROR_OVERFLOW));
    exports.Set("ERROR_IO", Napi::Number::New(env, ZXC_ERROR_IO));
    exports.Set("ERROR_NULL_INPUT", Napi::Number::New(env, ZXC_ERROR_NULL_INPUT));
    exports.Set("ERROR_BAD_BLOCK_TYPE", Napi::Number::New(env, ZXC_ERROR_BAD_BLOCK_TYPE));
    exports.Set("ERROR_BAD_BLOCK_SIZE", Napi::Number::New(env, ZXC_ERROR_BAD_BLOCK_SIZE));

    return exports;
}

NODE_API_MODULE(zxc_nodejs, Init)
