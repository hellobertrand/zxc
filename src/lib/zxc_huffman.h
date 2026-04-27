/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file zxc_huffman.h
 * @brief Internal canonical Huffman codec used by GLO blocks at level >= 6.
 *
 * Layout of a Huffman-encoded literal section:
 *   [128 B] 256 x 4-bit packed code lengths (LE nibble pairs).
 *   [  6 B] 3 x uint16_t LE: sub-stream sizes s1, s2, s3 (s4 is implied).
 *   [  N B] 4 concatenated LSB-first bit-streams covering literal indices
 *           [0, Q), [Q, 2Q), [2Q, 3Q), [3Q, n_literals) where Q = ceil(n/4).
 *
 * Code lengths are length-limited canonical Huffman with max length 11
 * (see ZXC_HUF_MAX_CODE_LEN). Codes are emitted LSB-first and decoded with a
 * single 2048-entry lookup table.
 */

#ifndef ZXC_HUFFMAN_H
#define ZXC_HUFFMAN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZXC_HUF_MAX_CODE_LEN 11
#define ZXC_HUF_NUM_SYMBOLS 256
#define ZXC_HUF_NUM_STREAMS 4
#define ZXC_HUF_TABLE_LOG ZXC_HUF_MAX_CODE_LEN
#define ZXC_HUF_TABLE_SIZE (1u << ZXC_HUF_TABLE_LOG)
#define ZXC_HUF_LENGTHS_HEADER_SIZE 128
#define ZXC_HUF_STREAM_SIZES_HEADER_SIZE 6
#define ZXC_HUF_HEADER_SIZE (ZXC_HUF_LENGTHS_HEADER_SIZE + ZXC_HUF_STREAM_SIZES_HEADER_SIZE)

/* Below this literal count Huffman is never profitable vs. RAW after the
 * 134-byte header overhead and the 4 sub-stream tail-byte slack. */
#define ZXC_HUF_MIN_LITERALS 1024

/**
 * @brief 2048-entry decoder lookup table entry.
 *
 * The encoder/decoder reads ZXC_HUF_MAX_CODE_LEN bits and indexes this table.
 * Each entry packs `{symbol, code_length}`: low byte = symbol, high byte =
 * code length (0 means malformed/unused — used to validate input).
 */
typedef struct {
    uint16_t entry;
} zxc_huf_dec_entry_t;

/**
 * @brief Compute length-limited canonical Huffman code lengths.
 *
 * Uses the boundary package-merge algorithm with limit ZXC_HUF_MAX_CODE_LEN.
 * Symbols with `freq[i] == 0` get `code_len[i] == 0`. Other symbols receive
 * a code length in [1, ZXC_HUF_MAX_CODE_LEN].
 *
 * @return 0 on success, negative on failure (e.g. all zero, OOM).
 */
int zxc_huf_build_code_lengths(const uint32_t freq[ZXC_HUF_NUM_SYMBOLS],
                               uint8_t code_len[ZXC_HUF_NUM_SYMBOLS]);

/**
 * @brief Estimate the compressed size of the literal section.
 *
 * Includes the 134-byte header and per-sub-stream tail-byte slack. Used by
 * the encoder to decide between RAW / RLE / HUFFMAN.
 */
size_t zxc_huf_estimate_size(const uint32_t freq[ZXC_HUF_NUM_SYMBOLS],
                             const uint8_t code_len[ZXC_HUF_NUM_SYMBOLS], size_t n_literals);

/**
 * @brief Encode the literal stream into a Huffman section payload.
 *
 * Writes the 128-byte length header, the 6-byte sub-stream size table, and
 * the 4 concatenated LSB-first bit-streams.
 *
 * @return Total bytes written on success, negative on failure.
 */
int zxc_huf_encode_section(const uint8_t* literals, size_t n_literals,
                           const uint8_t code_len[ZXC_HUF_NUM_SYMBOLS], uint8_t* dst,
                           size_t dst_cap);

/**
 * @brief Decode a Huffman literal section payload.
 *
 * `payload` points at the start of the section (the 128-byte length header).
 * `payload_size` is the section's compressed size from the descriptor.
 * Writes exactly `n_literals` decoded bytes into `dst`.
 *
 * @return 0 on success, negative `zxc_error_t` code on failure.
 */
int zxc_huf_decode_section(const uint8_t* payload, size_t payload_size, uint8_t* dst,
                           size_t n_literals);

#ifdef __cplusplus
}
#endif

#endif /* ZXC_HUFFMAN_H */
