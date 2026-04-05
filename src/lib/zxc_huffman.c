/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file zxc_huffman.c
 * @brief Canonical Huffman encoder and decoder for literal stream compression.
 *
 * This module implements a canonical Huffman codec used by Level 6 to
 * encode the literal byte stream within GLO blocks. The encoder builds
 * a frequency-based Huffman tree, normalises it to canonical form (codes
 * sorted by length then symbol value), and emits a compact 257-byte header
 * (ZXC_HUF_NUM_SYMBOLS bit-lengths + max_bits). The decoder reconstructs a 2048-entry
 * lookup table for single-step O(1) symbol resolution.
 *
 * Design choices:
 * - Maximum code length is capped at ZXC_HUF_MAX_BITS (11) to guarantee
 *   the decode table fits in L1 cache (~4 KB).
 * - Length-limiting uses the Kraft inequality redistribution method.
 * - The bitstream is written LSB-first for compatibility with the existing
 *   zxc_bit_reader_t infrastructure.
 */

#include "../../include/zxc_error.h"
#include "zxc_internal.h"

/*
 * ============================================================================
 * INTERNAL HELPERS
 * ============================================================================
 */

/** @brief Node in the Huffman tree construction heap. */
typedef struct {
    uint32_t freq;  /**< Symbol frequency. */
    int16_t left;   /**< Left child index (-1 = leaf). */
    int16_t right;  /**< Right child index (-1 = leaf). */
    uint8_t symbol; /**< Symbol value (valid only for leaves). */
} zxc_huf_node_t;

/**
 * @brief Counts byte frequencies in the source buffer.
 *
 * @param[in]  src    Source data.
 * @param[in]  size   Number of bytes.
 * @param[out] freq   Array of ZXC_HUF_NUM_SYMBOLS frequency counters (caller-zeroed).
 */
static void zxc_huf_count_freq(const uint8_t* src, const size_t size,
                               uint32_t freq[ZXC_HUF_NUM_SYMBOLS]) {
    /* Use 4-way interleaved counting to reduce store-forwarding stalls. */
    uint32_t f0[ZXC_HUF_NUM_SYMBOLS], f1[ZXC_HUF_NUM_SYMBOLS];
    uint32_t f2[ZXC_HUF_NUM_SYMBOLS], f3[ZXC_HUF_NUM_SYMBOLS];
    ZXC_MEMSET(f0, 0, sizeof(f0));
    ZXC_MEMSET(f1, 0, sizeof(f1));
    ZXC_MEMSET(f2, 0, sizeof(f2));
    ZXC_MEMSET(f3, 0, sizeof(f3));

    const size_t n4 = size & ~(size_t)3;
    size_t i = 0;
    for (; i < n4; i += 4) {
        f0[src[i + 0]]++;
        f1[src[i + 1]]++;
        f2[src[i + 2]]++;
        f3[src[i + 3]]++;
    }
    for (; i < size; i++) {
        f0[src[i]]++;
    }
    for (int s = 0; s < ZXC_HUF_NUM_SYMBOLS; s++) {
        freq[s] = f0[s] + f1[s] + f2[s] + f3[s];
    }
}

/**
 * @brief Sifts down a node in the min-heap used during Huffman tree construction.
 *
 * @param[in,out] heap  Array of node indices.
 * @param[in]     nodes Node pool (for frequency comparison).
 * @param[in]     pos   Position to sift down from.
 * @param[in]     size  Current heap size.
 */
static void zxc_huf_sift_down(int16_t* heap, const zxc_huf_node_t* nodes, int pos, const int size) {
    const int16_t val = heap[pos];
    const uint32_t freq = nodes[val].freq;
    while (1) {
        int child = 2 * pos + 1;
        if (child >= size) break;
        if (child + 1 < size && nodes[heap[child + 1]].freq < nodes[heap[child]].freq) child++;
        if (freq <= nodes[heap[child]].freq) break;
        heap[pos] = heap[child];
        pos = child;
    }
    heap[pos] = val;
}

/**
 * @brief Computes bit-lengths for each symbol by walking the Huffman tree.
 *
 * @param[in]  nodes    Node pool.
 * @param[in]  root     Root index of the tree.
 * @param[in]  depth    Current depth.
 * @param[out] lengths  Array of ZXC_HUF_NUM_SYMBOLS bit-lengths (caller-zeroed).
 */
static void zxc_huf_compute_lengths(const zxc_huf_node_t* nodes, const int root, const int depth,
                                    uint8_t lengths[ZXC_HUF_NUM_SYMBOLS]) {
    if (nodes[root].left == -1) {
        /* Leaf node */
        lengths[nodes[root].symbol] = (uint8_t)(depth > 0 ? depth : 1);
        return;
    }
    zxc_huf_compute_lengths(nodes, nodes[root].left, depth + 1, lengths);
    zxc_huf_compute_lengths(nodes, nodes[root].right, depth + 1, lengths);
}

/**
 * @brief Limits all code lengths to max_bits using the Kraft inequality.
 *
 * If any code exceeds max_bits, it is shortened and the deficit is
 * compensated by lengthening shorter codes. This guarantees a valid
 * prefix-free code.
 *
 * @param[in,out] lengths  Array of ZXC_HUF_NUM_SYMBOLS bit-lengths.
 * @param[in]     max_bits Maximum allowed code length.
 */
static void zxc_huf_limit_lengths(uint8_t lengths[ZXC_HUF_NUM_SYMBOLS], const int max_bits) {
    /* Check if limiting is needed */
    int needs_limiting = 0;
    for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i++) {
        if (lengths[i] > max_bits) {
            needs_limiting = 1;
            break;
        }
    }
    if (!needs_limiting) return;

    /* Iterative length-limiting:
     * 1. Clamp all lengths > max_bits to max_bits.
     * 2. Compute the Kraft sum; if > 1.0 (overshoot), lengthen shortest codes.
     * 3. Repeat until the Kraft inequality is satisfied. */
    for (int iter = 0; iter < 64; iter++) {
        /* Clamp */
        for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i++) {
            if (lengths[i] > max_bits) lengths[i] = (uint8_t)max_bits;
        }

        /* Compute Kraft number: sum of 2^(max_bits - len) for each symbol */
        uint32_t kraft = 0;
        for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i++) {
            if (lengths[i] > 0) {
                kraft += (1U << (max_bits - lengths[i]));
            }
        }

        uint32_t target = (1U << max_bits);
        if (kraft == target) return; /* Perfect fit */

        if (kraft > target) {
            /* Overshoot: lengthen the shortest codes by 1 bit each until kraft <= target */
            for (int len = 1; len < max_bits && kraft > target; len++) {
                for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS && kraft > target; i++) {
                    if (lengths[i] == len) {
                        lengths[i]++;
                        kraft -= (1U << (max_bits - len)) - (1U << (max_bits - len - 1));
                    }
                }
            }
        } else {
            /* Undershoot: shorten the longest codes by 1 bit each until kraft >= target */
            for (int len = max_bits; len > 1 && kraft < target; len--) {
                for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS && kraft < target; i++) {
                    if (lengths[i] == len) {
                        uint32_t gain = (1U << (max_bits - len + 1)) - (1U << (max_bits - len));
                        if (kraft + gain <= target) {
                            lengths[i]--;
                            kraft += gain;
                        }
                    }
                }
            }
        }

        if (kraft == target) return;
    }
}

/**
 * @brief Assigns canonical Huffman codes from the computed bit-lengths.
 *
 * Canonical codes are assigned by sorting symbols first by code length,
 * then by symbol value. This allows the decoder to reconstruct the
 * codebook from bit-lengths alone.
 *
 * @param[in]  lengths    Array of ZXC_HUF_NUM_SYMBOLS bit-lengths.
 * @param[out] codes      Array of ZXC_HUF_NUM_SYMBOLS canonical codes.
 * @param[out] out_max    Receives the maximum code length actually used.
 */
static void zxc_huf_assign_canonical(const uint8_t lengths[ZXC_HUF_NUM_SYMBOLS],
                                     uint32_t codes[ZXC_HUF_NUM_SYMBOLS], uint8_t* out_max) {
    /* Count symbols per length */
    uint32_t bl_count[ZXC_HUF_MAX_BITS + 1];
    ZXC_MEMSET(bl_count, 0, sizeof(bl_count));
    uint8_t max_len = 0;
    for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i++) {
        if (lengths[i] > 0) {
            bl_count[lengths[i]]++;
            if (lengths[i] > max_len) max_len = lengths[i];
        }
    }

    /* Compute starting code for each length (DEFLATE algorithm) */
    uint32_t next_code[ZXC_HUF_MAX_BITS + 2];
    next_code[0] = 0;
    next_code[1] = 0;
    for (int bits = 1; bits <= max_len; bits++) {
        next_code[bits + 1] = (next_code[bits] + bl_count[bits]) << 1;
    }

    /* Assign codes */
    ZXC_MEMSET(codes, 0, ZXC_HUF_NUM_SYMBOLS * sizeof(uint32_t));
    for (int sym = 0; sym < ZXC_HUF_NUM_SYMBOLS; sym++) {
        if (lengths[sym] > 0) {
            codes[sym] = next_code[lengths[sym]]++;
        }
    }

    *out_max = max_len;
}

/**
 * @brief Reverses the bits of a value (for LSB-first bitstream writing).
 *
 * @param[in] val  Value to reverse.
 * @param[in] bits Number of bits to reverse.
 * @return The bit-reversed value.
 */
static ZXC_ALWAYS_INLINE uint32_t zxc_huf_reverse_bits(uint32_t val, const int bits) {
    uint32_t result = 0;
    for (int i = 0; i < bits; i++) {
        result = (result << 1) | (val & 1);
        val >>= 1;
    }
    return result;
}

/*
 * ============================================================================
 * PUBLIC API
 * ============================================================================
 */

/**
 * @brief Encodes a literal byte stream using canonical Huffman coding.
 *
 * Output layout:
 * [ZXC_HUF_HEADER_SIZE-byte header: ZXC_HUF_NUM_SYMBOLS × bit-length + 1 × max_bits] [bitstream]
 *
 * The bitstream is written LSB-first to match the zxc_bit_reader_t
 * accumulator convention.
 */
int zxc_huf_encode(const uint8_t* RESTRICT src, const size_t src_size, uint8_t* RESTRICT dst,
                   const size_t dst_cap, uint8_t* out_max_bits) {
    if (UNLIKELY(src_size == 0)) return ZXC_ERROR_SRC_TOO_SMALL;
    if (UNLIKELY(dst_cap < ZXC_HUF_HEADER_SIZE + 1)) return ZXC_ERROR_DST_TOO_SMALL;

    /* --- 1. Frequency counting --- */
    uint32_t freq[ZXC_HUF_NUM_SYMBOLS];
    ZXC_MEMSET(freq, 0, sizeof(freq));
    zxc_huf_count_freq(src, src_size, freq);

    /* Count distinct symbols */
    int n_symbols = 0;
    for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i++) {
        if (freq[i] > 0) n_symbols++;
    }

    /* Single-symbol edge case: entire stream is one byte repeated */
    if (n_symbols <= 1) {
        /* Encode as 1-bit codes: the single symbol gets length 1.
         * Output = header + ceil(src_size / 8) bytes. */
        uint8_t lengths[ZXC_HUF_NUM_SYMBOLS];
        ZXC_MEMSET(lengths, 0, sizeof(lengths));
        for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i++) {
            if (freq[i] > 0) {
                lengths[i] = 1;
                break;
            }
        }

        /* Write header */
        ZXC_MEMCPY(dst, lengths, ZXC_HUF_NUM_SYMBOLS);
        dst[ZXC_HUF_NUM_SYMBOLS] = 1; /* max_bits = 1 */
        *out_max_bits = 1;

        const size_t bitstream_bytes = (src_size + 7) / 8;
        if (dst_cap < ZXC_HUF_HEADER_SIZE + bitstream_bytes) return ZXC_ERROR_DST_TOO_SMALL;

        ZXC_MEMSET(dst + ZXC_HUF_HEADER_SIZE, 0, bitstream_bytes);
        /* All bits are 0 (canonical code for the only symbol is 0) */
        return (int)(ZXC_HUF_HEADER_SIZE + bitstream_bytes);
    }

    /* --- 2. Build Huffman tree via min-heap --- */
    zxc_huf_node_t nodes[2 * ZXC_HUF_NUM_SYMBOLS - 1]; /* max leaves + internal nodes */
    int16_t heap[ZXC_HUF_NUM_SYMBOLS];
    int heap_size = 0;
    int node_count = 0;

    for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i++) {
        if (freq[i] > 0) {
            nodes[node_count].freq = freq[i];
            nodes[node_count].left = -1;
            nodes[node_count].right = -1;
            nodes[node_count].symbol = (uint8_t)i;
            heap[heap_size++] = (int16_t)node_count;
            node_count++;
        }
    }

    /* Build min-heap */
    for (int i = heap_size / 2 - 1; i >= 0; i--) {
        zxc_huf_sift_down(heap, nodes, i, heap_size);
    }

    /* Merge nodes */
    while (heap_size > 1) {
        /* Extract two minimums */
        const int16_t min1 = heap[0];
        heap[0] = heap[--heap_size];
        zxc_huf_sift_down(heap, nodes, 0, heap_size);

        const int16_t min2 = heap[0];

        /* Create internal node */
        nodes[node_count].freq = nodes[min1].freq + nodes[min2].freq;
        nodes[node_count].left = min1;
        nodes[node_count].right = min2;
        nodes[node_count].symbol = 0;

        heap[0] = (int16_t)node_count;
        node_count++;
        zxc_huf_sift_down(heap, nodes, 0, heap_size);
    }

    const int root = heap[0];

    /* --- 3. Compute bit-lengths --- */
    uint8_t lengths[ZXC_HUF_NUM_SYMBOLS];
    ZXC_MEMSET(lengths, 0, sizeof(lengths));
    zxc_huf_compute_lengths(nodes, root, 0, lengths);

    /* --- 4. Length-limit to ZXC_HUF_MAX_BITS --- */
    zxc_huf_limit_lengths(lengths, ZXC_HUF_MAX_BITS);

    /* --- 5. Assign canonical codes --- */
    uint32_t codes[ZXC_HUF_NUM_SYMBOLS];
    uint8_t max_bits = 0;
    zxc_huf_assign_canonical(lengths, codes, &max_bits);
    *out_max_bits = max_bits;

    /* --- 6. Write header --- */
    ZXC_MEMCPY(dst, lengths, ZXC_HUF_NUM_SYMBOLS);
    dst[ZXC_HUF_NUM_SYMBOLS] = max_bits;

    /* --- 7. Estimate output size --- */
    uint64_t total_bits = 0;
    for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i++) {
        total_bits += (uint64_t)freq[i] * lengths[i];
    }
    const size_t bitstream_bytes = (size_t)((total_bits + 7) / 8);
    const size_t total_out = ZXC_HUF_HEADER_SIZE + bitstream_bytes;

    if (UNLIKELY(total_out > dst_cap)) return ZXC_ERROR_DST_TOO_SMALL;

    /* --- 8. Encode bitstream (LSB-first) --- */
    uint8_t* out = dst + ZXC_HUF_HEADER_SIZE;
    ZXC_MEMSET(out, 0, bitstream_bytes + 1); /* +1 for partial-byte safety */

    uint64_t accum = 0;
    int accum_bits = 0;
    size_t out_pos = 0;

    for (size_t i = 0; i < src_size; i++) {
        const uint8_t sym = src[i];
        const uint32_t code = zxc_huf_reverse_bits(codes[sym], lengths[sym]);
        const int nbits = lengths[sym];

        accum |= ((uint64_t)code << accum_bits);
        accum_bits += nbits;

        /* Flush complete bytes */
        while (accum_bits >= 8) {
            out[out_pos++] = (uint8_t)(accum & 0xFF);
            accum >>= 8;
            accum_bits -= 8;
        }
    }
    /* Flush remaining bits */
    if (accum_bits > 0) {
        out[out_pos++] = (uint8_t)(accum & 0xFF);
    }

    return (int)(ZXC_HUF_HEADER_SIZE + out_pos);
}

/**
 * @brief Decodes a canonical Huffman-compressed literal stream.
 *
 * Reads the 257-byte header, builds a 2^max_bits lookup table, and
 * decodes each symbol in O(1) per symbol via table lookup.
 */
int zxc_huf_decode(const uint8_t* RESTRICT src, const size_t src_size, uint8_t* RESTRICT dst,
                   const size_t dst_cap, const size_t raw_size) {
    if (UNLIKELY(src_size < ZXC_HUF_HEADER_SIZE)) return ZXC_ERROR_SRC_TOO_SMALL;
    if (UNLIKELY(raw_size > dst_cap)) return ZXC_ERROR_DST_TOO_SMALL;
    if (UNLIKELY(raw_size == 0)) return 0;

    /* --- 1. Read header --- */
    const uint8_t* lengths = src; /* ZXC_HUF_NUM_SYMBOLS bit-lengths */
    const uint8_t max_bits = src[ZXC_HUF_NUM_SYMBOLS];

    if (UNLIKELY(max_bits == 0 || max_bits > ZXC_HUF_MAX_BITS)) return ZXC_ERROR_CORRUPT_DATA;

    /* --- 2. Build decode table --- */
    /* Each entry: (symbol << 8) | code_length */
    uint16_t table[ZXC_HUF_TABLE_SIZE];
    ZXC_MEMSET(table, 0, sizeof(table));

    /* Reconstruct canonical codes from lengths (same algorithm as encoder) */
    uint32_t bl_count[ZXC_HUF_MAX_BITS + 1];
    ZXC_MEMSET(bl_count, 0, sizeof(bl_count));
    for (int i = 0; i < ZXC_HUF_NUM_SYMBOLS; i++) {
        if (lengths[i] > 0 && lengths[i] <= max_bits) {
            bl_count[lengths[i]]++;
        }
    }

    uint32_t next_code[ZXC_HUF_MAX_BITS + 2];
    next_code[0] = 0;
    next_code[1] = 0;
    for (int bits = 1; bits <= max_bits; bits++) {
        next_code[bits + 1] = (next_code[bits] + bl_count[bits]) << 1;
    }

    for (int sym = 0; sym < ZXC_HUF_NUM_SYMBOLS; sym++) {
        const int len = lengths[sym];
        if (len == 0) continue;
        if (UNLIKELY(len > max_bits)) return ZXC_ERROR_CORRUPT_DATA;

        uint32_t code = next_code[len]++;

        /* Fill all table entries for this symbol.
         * A code of length `len` occupies 2^(max_bits - len) entries. */
        const uint32_t reversed = zxc_huf_reverse_bits(code, len);
        const int fill = 1 << (max_bits - len);
        const uint16_t entry = (uint16_t)(((uint16_t)sym << 8) | (uint16_t)len);

        for (int j = 0; j < fill; j++) {
            const uint32_t idx = reversed | ((uint32_t)j << len);
            if (UNLIKELY(idx >= ZXC_HUF_TABLE_SIZE)) return ZXC_ERROR_CORRUPT_DATA;
            table[idx] = entry;
        }
    }

    /* --- 3. Decode bitstream --- */
    const uint8_t* bitstream = src + ZXC_HUF_HEADER_SIZE;
    const size_t bitstream_size = src_size - ZXC_HUF_HEADER_SIZE;

    zxc_bit_reader_t br;
    zxc_br_init(&br, bitstream, bitstream_size);

    const uint32_t mask = (1U << max_bits) - 1;

    for (size_t i = 0; i < raw_size; i++) {
        zxc_br_ensure(&br, max_bits);
        const uint32_t bits = (uint32_t)(br.accum & mask);
        const uint16_t entry = table[bits];
        const uint8_t sym = (uint8_t)(entry >> 8);
        const uint8_t len = (uint8_t)(entry & 0xFF);

        if (UNLIKELY(len == 0)) return ZXC_ERROR_CORRUPT_DATA;

        br.accum >>= len;
        br.bits -= len;
        dst[i] = sym;
    }

    return (int)raw_size;
}
