/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <napi.h>

extern "C" {
#include "zxc.h"
}

// =============================================================================
// compressBound(inputSize: number): number
// =============================================================================
static Napi::Value CompressBound(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Expected a number (inputSize)")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    uint64_t input_size = info[0].As<Napi::Number>().Int64Value();
    uint64_t bound = zxc_compress_bound(static_cast<size_t>(input_size));

    return Napi::Number::New(env, static_cast<double>(bound));
}

// =============================================================================
// compress(buffer: Buffer, level?: number, checksum?: boolean): Buffer
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

    // Handle empty input
    if (src_size == 0 && !checksum) {
        return Napi::Buffer<uint8_t>::New(env, 0);
    }

    uint64_t bound = zxc_compress_bound(src_size);
    Napi::Buffer<uint8_t> dst_buf = Napi::Buffer<uint8_t>::New(env, static_cast<size_t>(bound));

    int64_t nwritten = zxc_compress(src, src_size,
                                     dst_buf.Data(), static_cast<size_t>(bound),
                                     level, checksum);

    if (nwritten < 0) {
        Napi::Error::New(env, zxc_error_name(static_cast<int>(nwritten)))
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    if (nwritten == 0) {
        Napi::Error::New(env, "Input is too small to be compressed")
            .ThrowAsJavaScriptException();
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

    int64_t nwritten = zxc_decompress(src, src_size,
                                       dst_buf.Data(), decompress_size,
                                       checksum);

    if (nwritten < 0) {
        Napi::Error::New(env, zxc_error_name(static_cast<int>(nwritten)))
            .ThrowAsJavaScriptException();
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
// errorName(code: number): string
// =============================================================================
static Napi::Value ErrorName(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Expected a number (error code)")
            .ThrowAsJavaScriptException();
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
    exports.Set("compressBound",
                Napi::Function::New(env, CompressBound, "compressBound"));
    exports.Set("compress",
                Napi::Function::New(env, Compress, "compress"));
    exports.Set("decompress",
                Napi::Function::New(env, Decompress, "decompress"));
    exports.Set("getDecompressedSize",
                Napi::Function::New(env, GetDecompressedSize, "getDecompressedSize"));
    exports.Set("errorName",
                Napi::Function::New(env, ErrorName, "errorName"));

    // Compression level constants
    exports.Set("LEVEL_FASTEST",  Napi::Number::New(env, ZXC_LEVEL_FASTEST));
    exports.Set("LEVEL_FAST",     Napi::Number::New(env, ZXC_LEVEL_FAST));
    exports.Set("LEVEL_DEFAULT",  Napi::Number::New(env, ZXC_LEVEL_DEFAULT));
    exports.Set("LEVEL_BALANCED", Napi::Number::New(env, ZXC_LEVEL_BALANCED));
    exports.Set("LEVEL_COMPACT",  Napi::Number::New(env, ZXC_LEVEL_COMPACT));

    return exports;
}

NODE_API_MODULE(zxc_nodejs, Init)
