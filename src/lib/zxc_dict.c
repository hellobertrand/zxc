/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file zxc_dict.c
 * @brief Pre-trained dictionary: ID computation, .zxd serialization, and training.
 */

#include "../../include/zxc_dict.h"

#include "zxc_internal.h"

/* -------------------------------------------------------------------------
 *  Dictionary ID
 * ------------------------------------------------------------------------- */

uint32_t zxc_dict_id(const void* dict, const size_t dict_size) {
    if (UNLIKELY(!dict || dict_size == 0)) return 0;
    return zxc_checksum(dict, dict_size, 0);
}

/* -------------------------------------------------------------------------
 *  .zxd format: save / load / bound
 *
 *  Layout (ZXC_DICT_HEADER_SIZE = 16 bytes + content):
 *    0x00  4  Magic   (0x9CB0D1C7 LE)
 *    0x04  1  Version (1)
 *    0x05  1  Flags   (reserved, 0)
 *    0x06  2  Content size (u16 LE)
 *    0x08  4  dict_id (u32 LE)
 *    0x0C  4  Header CRC32 (rapidhash-folded, computed with this field zeroed)
 *    0x10  N  Content bytes
 * ------------------------------------------------------------------------- */

size_t zxc_dict_save_bound(const size_t content_size) {
    return ZXC_DICT_HEADER_SIZE + content_size;
}

int64_t zxc_dict_save(const void* content, const size_t content_size, void* buf,
                      const size_t buf_capacity) {
    if (UNLIKELY(!content || content_size == 0)) return ZXC_ERROR_NULL_INPUT;
    if (UNLIKELY(content_size > ZXC_DICT_SIZE_MAX)) return ZXC_ERROR_DICT_TOO_LARGE;

    const size_t total = ZXC_DICT_HEADER_SIZE + content_size;
    if (UNLIKELY(buf_capacity < total)) return ZXC_ERROR_DST_TOO_SMALL;

    uint8_t* dst = (uint8_t*)buf;

    zxc_store_le32(dst + 0, ZXC_DICT_MAGIC);
    dst[4] = ZXC_DICT_VERSION;
    dst[5] = 0; /* flags: reserved */
    zxc_store_le16(dst + 6, (uint16_t)content_size);
    zxc_store_le32(dst + 8, zxc_dict_id(content, content_size));

    /* CRC32 of header with CRC field zeroed */
    zxc_store_le32(dst + 12, 0);
    const uint32_t crc = zxc_checksum(dst, ZXC_DICT_HEADER_SIZE, 0);
    zxc_store_le32(dst + 12, crc);

    ZXC_MEMCPY(dst + ZXC_DICT_HEADER_SIZE, content, content_size);

    return (int64_t)total;
}

int zxc_dict_load(const void* buf, const size_t buf_size, const void** content_out,
                  size_t* content_size_out, uint32_t* dict_id_out) {
    if (UNLIKELY(!buf || !content_out || !content_size_out)) return ZXC_ERROR_NULL_INPUT;
    if (UNLIKELY(buf_size < ZXC_DICT_HEADER_SIZE)) return ZXC_ERROR_SRC_TOO_SMALL;

    const uint8_t* src = (const uint8_t*)buf;

    if (zxc_le32(src) != ZXC_DICT_MAGIC) return ZXC_ERROR_BAD_MAGIC;
    if (src[4] != ZXC_DICT_VERSION) return ZXC_ERROR_BAD_VERSION;

    const size_t content_size = zxc_le16(src + 6);
    if (content_size == 0) return ZXC_ERROR_CORRUPT_DATA;
    if (content_size > ZXC_DICT_SIZE_MAX) return ZXC_ERROR_DICT_TOO_LARGE;
    if (buf_size < ZXC_DICT_HEADER_SIZE + content_size) return ZXC_ERROR_SRC_TOO_SMALL;

    /* Verify header CRC32 */
    uint8_t temp[ZXC_DICT_HEADER_SIZE];
    ZXC_MEMCPY(temp, src, ZXC_DICT_HEADER_SIZE);
    zxc_store_le32(temp + 12, 0);
    const uint32_t expected_crc = zxc_checksum(temp, ZXC_DICT_HEADER_SIZE, 0);
    if (UNLIKELY(zxc_le32(src + 12) != expected_crc)) return ZXC_ERROR_BAD_HEADER;

    /* Verify dict_id matches content */
    const uint8_t* content = src + ZXC_DICT_HEADER_SIZE;
    const uint32_t id = zxc_dict_id(content, content_size);
    if (UNLIKELY(zxc_le32(src + 8) != id)) return ZXC_ERROR_BAD_CHECKSUM;

    *content_out = content;
    *content_size_out = content_size;
    if (dict_id_out) *dict_id_out = id;

    return ZXC_OK;
}

/* -------------------------------------------------------------------------
 *  Dictionary training
 * ------------------------------------------------------------------------- */

int64_t zxc_train_dict(const void* const* samples, const size_t* sample_sizes,
                       const size_t n_samples, void* dict_buf, const size_t dict_capacity) {
    (void)samples;
    (void)sample_sizes;
    (void)n_samples;
    (void)dict_buf;
    (void)dict_capacity;
    /* TODO: implement training algorithm */
    return ZXC_ERROR_NULL_INPUT;
}
