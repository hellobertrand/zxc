// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

/**
 * @file list.c
 * @brief Listing (-l) of the zxc command line tool: walks the frames of a
 * container and reports them, or a .zxd dictionary.
 */

#include "cli.h"

/**
 * @brief Lists the contents of a ZXC archive.
 *
 * Reads the file header and the stored size to display:
 * - Compressed size
 * - Uncompressed size
 * - Compression ratio
 * - Checksum method
 * - Filename
 *
 * In verbose mode, displays additional header information.
 *
 * @param[in] path Path to the ZXC archive file.
 * @param[in] json_output If 1, output JSON format.
 * @return 0 on success, 1 on error.
 */
// Report a .zxd dictionary file: its dict_id (to match against a .zxc's
// "Dict ID") and content size. `buf` holds the whole .zxd file.
static int zxc_list_dict(const char* path, const uint8_t* buf, size_t buf_size, long long file_size,
                         int json_output) {
    const void* content = NULL;
    size_t content_size = 0;
    uint32_t id = 0;
    const int rc = zxc_dict_load(buf, buf_size, &content, &content_size, NULL, &id);
    if (rc != ZXC_OK) {
        zxc_err("Error: invalid dictionary '%s': %s\n", path, zxc_error_name(rc));
        return 1;
    }
    if (json_output) {
        printf(
            "{\n"
            "  \"type\": \"dictionary\",\n"
            "  \"filename\": \"%s\",\n"
            "  \"dict_id\": \"0x%08X\",\n"
            "  \"content_size_bytes\": %zu,\n"
            "  \"file_size_bytes\": %lld\n"
            "}\n",
            path, id, content_size, file_size);
    } else {
        printf(
            "\n  Dictionary file (.zxd)\n"
            "  Dict ID:       0x%08X\n"
            "  Content size:  %zu bytes\n"
            "  File:          %s\n",
            id, content_size, path);
    }
    return 0;
}

// Marks a frame that needs a dictionary: its id may be 0.
#define CLI_DICT_FLAG (1ULL << 32)

/** @brief What a walk of a container finds. */
typedef struct {
    zxc_frame_info_t first; /* the first frame */
    int first_has_dict;     /* it needs a dictionary, whose id may be 0 */
    uint64_t frames;        /* ZXC frames */
    uint64_t checksummed;   /* frames carrying checksums */
    uint64_t seekable;      /* frames carrying a seek table */
    uint32_t block_sizes;   /* OR of every block size, each a power of 2 */
    uint64_t* dict_ids;     /* distinct dictionaries in file order: id, bit 32 set; 0 for none */
    size_t num_dict_ids;
    uint64_t dsize;  /* their decompressed sizes, summed */
    uint64_t digest; /* the last frame's digest, when it has one */
} cli_container_t;

/** @brief A run of frames sharing one dictionary, numbered as the walk meets them. */
typedef struct {
    uint64_t dict; /* id with CLI_DICT_FLAG, or 0 for none */
    uint64_t rank;
} cli_dict_run_t;

static int cli_cmp_u64(const uint64_t x, const uint64_t y) { return (x > y) - (x < y); }

// By dictionary, then by rank.
static int cli_cmp_run_dict(const void* a, const void* b) {
    const cli_dict_run_t* const x = (const cli_dict_run_t*)a;
    const cli_dict_run_t* const y = (const cli_dict_run_t*)b;
    const int d = cli_cmp_u64(x->dict, y->dict);
    return d ? d : cli_cmp_u64(x->rank, y->rank);
}

// By falling rank: the walk goes back, so this is file order.
static int cli_cmp_run_rank(const void* a, const void* b) {
    return cli_cmp_u64(((const cli_dict_run_t*)b)->rank, ((const cli_dict_run_t*)a)->rank);
}

/**
 * @brief Walks the frames concatenated in @p f, from the last back to the first,
 *        through zxc_stream_get_last_frame_info(). Rewinds @p f; free
 *        @c c->dict_ids afterwards, whatever the result.
 *
 * @return @ref ZXC_OK, or the library's verdict on the first frame that fails.
 */
static int zxc_cli_walk(FILE* f, const uint64_t file_size, cli_container_t* c) {
    memset(c, 0, sizeof(*c));
    uint64_t end = file_size;
    int rc = ZXC_OK;
    cli_dict_run_t* runs = NULL;
    size_t num = 0;
    size_t cap = 0;
    do {
        zxc_frame_info_t fi;
        rc = zxc_stream_get_last_frame_info(f, end, &fi, sizeof(fi));
        if (rc != ZXC_OK) break;
        if (c->frames++ == 0) c->digest = fi.digest;
        c->checksummed += fi.has_checksum;
        c->seekable += fi.has_seek_table;
        if (fi.decompressed_size > UINT64_MAX - c->dsize) {
            rc = ZXC_ERROR_CORRUPT_DATA;
            break;
        }
        c->dsize += fi.decompressed_size;
        c->block_sizes |= (uint32_t)fi.block_size;
        uint8_t h[ZXC_FILE_HEADER_SIZE];
        if (fseeko(f, (int64_t)(end - fi.compressed_size), SEEK_SET) != 0 ||
            fread(h, 1, sizeof(h), f) != sizeof(h)) {
            rc = ZXC_ERROR_IO;
            break;
        }
        const int needs_dict = zxc_requires_dict(h, sizeof(h));
        if (needs_dict < 0) {
            rc = needs_dict;
            break;
        }
        const uint64_t dict = needs_dict ? CLI_DICT_FLAG | fi.dict_id : 0;
        // One entry per run of frames sharing a dictionary, deduplicated after the walk.
        if (num == 0 || runs[num - 1].dict != dict) {
            if (num == cap) {
                cap = cap ? 2 * cap : 4;
                cli_dict_run_t* const grown =
                    (cli_dict_run_t*)realloc(runs, cap * sizeof(cli_dict_run_t));
                if (!grown) {
                    rc = ZXC_ERROR_MEMORY;
                    break;
                }
                runs = grown;
            }
            runs[num].dict = dict;
            runs[num].rank = num;
            num++;
        }
        c->first = fi;  // the first frame's, once the walk is done
        c->first_has_dict = needs_dict;
        end -= fi.compressed_size;
    } while (end > 0);
    // Each dictionary keeps its highest rank, its first frame in the file.
    if (rc == ZXC_OK) {
        qsort(runs, num, sizeof(cli_dict_run_t), cli_cmp_run_dict);
        size_t n = 0;
        for (size_t i = 0; i < num; i++)
            if (i + 1 == num || runs[i].dict != runs[i + 1].dict) runs[n++] = runs[i];
        qsort(runs, n, sizeof(cli_dict_run_t), cli_cmp_run_rank);
        c->dict_ids = (uint64_t*)malloc(n * sizeof(uint64_t));
        if (c->dict_ids) {
            for (size_t i = 0; i < n; i++) c->dict_ids[i] = runs[i].dict;
            c->num_dict_ids = n;
        } else {
            rc = ZXC_ERROR_MEMORY;
        }
    }
    free(runs);
    if (fseeko(f, 0, SEEK_SET) != 0 && rc == ZXC_OK) rc = ZXC_ERROR_IO;
    return rc;
}

/** @brief Prints the block sizes, ascending; in JSON an array when they differ. */
static void cli_print_block_sizes(const cli_container_t* c, const int json) {
    const int many = (c->block_sizes & (c->block_sizes - 1)) != 0;
    if (json && many) printf("[");
    for (uint32_t b = c->block_sizes; b; b &= b - 1)
        printf(json ? "%s%u" : "%s%u KB", b == c->block_sizes ? "" : ", ", (b & (0U - b)) / 1024);
    if (json && many) printf("]");
}

/** @brief Prints the dictionary ids in file order, "none" (JSON null) for frames
 *  without one; in JSON an array when they differ. */
static void cli_print_dict_ids(const cli_container_t* c, const int json) {
    if (json && c->num_dict_ids > 1) printf("[");
    for (size_t i = 0; i < c->num_dict_ids; i++) {
        const uint64_t dict = c->dict_ids[i];
        if (i) printf(", ");
        if (dict)
            printf(json ? "\"0x%08X\"" : "0x%08X", (uint32_t)dict);
        else
            printf(json ? "null" : "none");
    }
    if (json && c->num_dict_ids > 1) printf("]");
}

int zxc_list_archive(const char* path, int json_output, int show_name) {
    char resolved_path[4096];
    if (zxc_validate_input_path(path, resolved_path, sizeof(resolved_path)) != 0) {
        zxc_err("Error: Invalid input file '%s': %s\n", path, strerror(errno));
        return 1;
    }

    FILE* f = fopen(resolved_path, "rb");
    if (!f) {
        zxc_err("Error: Cannot open '%s': %s\n", path, strerror(errno));
        return 1;
    }

    // Get file size
    if (fseeko(f, 0, SEEK_END) != 0) {
        fclose(f);
        zxc_err("Error: Cannot seek in file\n");
        return 1;
    }
    const long long file_size = ftello(f);

    // A .zxd dictionary file has its own magic word; recognise it and report
    // its dict_id (for matching against a .zxc's "Dict ID") instead of failing
    // as a non-archive. The upper bound is the largest possible .zxd file.
    if (file_size >= (long long)ZXC_DICT_HEADER_SIZE &&
        file_size <= (long long)zxc_dict_save_bound(ZXC_DICT_SIZE_MAX)) {
        uint8_t probe[ZXC_DICT_HEADER_SIZE];
        if (fseeko(f, 0, SEEK_SET) == 0 &&
            fread(probe, 1, ZXC_DICT_HEADER_SIZE, f) == ZXC_DICT_HEADER_SIZE &&
            ((uint32_t)probe[0] | (uint32_t)probe[1] << 8 | (uint32_t)probe[2] << 16 |
             (uint32_t)probe[3] << 24) == ZXC_DICT_MAGIC) {
            uint8_t* dbuf = (uint8_t*)malloc((size_t)file_size);
            int r = 1;
            if (dbuf && fseeko(f, 0, SEEK_SET) == 0 &&
                fread(dbuf, 1, (size_t)file_size, f) == (size_t)file_size)
                r = zxc_list_dict(path, dbuf, (size_t)file_size, file_size, json_output);
            else
                zxc_err("Error: Cannot read '%s'\n", path);
            free(dbuf);
            fclose(f);
            return r;
        }
        fseeko(f, 0, SEEK_SET);
    }

    // Every frame, checked by the library.
    cli_container_t cont;
    const int walked = zxc_cli_walk(f, (uint64_t)file_size, &cont);
    fclose(f);
    if (walked != ZXC_OK) {
        free(cont.dict_ids);
        zxc_err("Error: Not a valid ZXC archive (%s)\n", zxc_error_name(walked));
        return 1;
    }
    const uint64_t uncompressed_size = cont.dsize;
    const unsigned format_version = cont.first.format_version;
    const int mixed_dict = cont.num_dict_ids > 1;
    // Checksums when every frame carries them; a digest only describes one frame.
    const int has_checksum = cont.checksummed == cont.frames;
    const int mixed_checksum = cont.checksummed != 0 && !has_checksum;
    const char* checksum_state = has_checksum ? "enabled" : mixed_checksum ? "mixed" : "disabled";
    const char* seekable_state = cont.seekable == cont.frames ? "enabled"
                                 : cont.seekable              ? "mixed"
                                                              : "disabled";
    const int has_digest = has_checksum && cont.frames == 1;
    char digest_str[24] = "";  // quoted in JSON only
    char digest_json[sizeof(digest_str) + 2] = "null";
    if (has_digest) {
        snprintf(digest_str, sizeof(digest_str), "0x%016llX", (unsigned long long)cont.digest);
        snprintf(digest_json, sizeof(digest_json), "\"%s\"", digest_str);
    }
    const uint32_t dict_id = cont.first.dict_id;
    const int has_dict = cont.first_has_dict;

    // Calculate ratio (uncompressed / compressed, e.g., 2.5 means 2.5x compression)
    const double ratio = (file_size > 0) ? ((double)uncompressed_size / (double)file_size) : 0.0;

    // Format sizes
    char comp_str[32];
    char uncomp_str[32];
    char dict_id_str[16];

    format_size_decimal((uint64_t)file_size, comp_str, sizeof(comp_str));
    format_size_decimal(uncompressed_size, uncomp_str, sizeof(uncomp_str));

    if (mixed_dict)
        snprintf(dict_id_str, sizeof(dict_id_str), "mixed");
    else if (has_dict)
        snprintf(dict_id_str, sizeof(dict_id_str), "0x%08X", dict_id);
    else
        snprintf(dict_id_str, sizeof(dict_id_str), "-");
    const int show_dict = has_dict || mixed_dict;

    if (json_output) {
        printf(
            "{\n"
            "  \"filename\": \"%s\",\n"
            "  \"compressed_size_bytes\": %lld,\n"
            "  \"uncompressed_size_bytes\": %llu,\n"
            "  \"compression_ratio\": %.3f,\n"
            "  \"format_version\": %u,\n"
            "  \"block_size_kb\": ",
            path, file_size, (unsigned long long)uncompressed_size, ratio, format_version);
        cli_print_block_sizes(&cont, 1);
        printf(
            ",\n"
            "  \"checksum\": \"%s\",\n"
            "  \"seekable\": \"%s\",\n"
            "  \"digest\": %s,\n"
            "  \"dict_id\": ",
            checksum_state, seekable_state, digest_json);
        cli_print_dict_ids(&cont, 1);
        printf(
            ",\n"
            "  \"frames\": %llu\n"
            "}\n",
            (unsigned long long)cont.frames);
    } else if (g_verbose) {
        // Verbose mode: detailed vertical layout
        printf(
            "\nFile: %s\n"
            "-----------------------\n"
            "Block Format:  %u\n"
            "Block Size:    ",
            path, format_version);
        cli_print_block_sizes(&cont, 0);
        printf(
            "\n"
            "Checksum:      %s\n"
            "Seekable:      %s\n",
            checksum_state, seekable_state);

        if (has_digest) printf("Digest:        %s\n", digest_str);
        if (show_dict) {
            printf("Dictionary ID: ");
            cli_print_dict_ids(&cont, 0);
            printf("\n");
        }
        if (cont.frames > 1) printf("Frames:        %llu\n", (unsigned long long)cont.frames);

        printf(
            "-----------------------\n"
            "Comp. Size:    %s\n"
            "Uncomp. Size:  %s\n"
            "Ratio:         %.2f\n",
            comp_str, uncomp_str, ratio);
    } else {
        // Normal mode: table format, the file named only when several are listed
        printf("\n  %12s   %12s   %5s   %-10s   %-10s   %-10s   %6s%s\n", "Compressed",
               "Uncompressed", "Ratio", "Checksum", "Seekable", "Dict ID", "Frames",
               show_name ? "   Filename" : "");
        printf("  %12s   %12s   %5.2f   %-10s   %-10s   %-10s   %6llu%s%s\n", comp_str, uncomp_str,
               ratio, checksum_state, seekable_state, dict_id_str, (unsigned long long)cont.frames,
               show_name ? "   " : "", show_name ? path : "");
    }

    free(cont.dict_ids);
    return 0;
}

/**
 * @brief Reports whether an archive's frames declare checksums.
 *
 * Walks the container in @p f and rewinds it to the start.
 *
 * @param[in] f Seekable input stream for the archive (not stdin).
 * @return 1 if every frame declares one, 2 if only some do, 0 if none does,
 *         -1 if the layout could not be read.
 */
int zxc_archive_has_checksum(FILE* f) {
    cli_container_t c = {0};
    if (fseeko(f, 0, SEEK_END) != 0) return -1;
    const long long size = ftello(f);
    const int rc = size < 0 ? ZXC_ERROR_IO : zxc_cli_walk(f, (uint64_t)size, &c);
    free(c.dict_ids);
    if (rc != ZXC_OK) {
        fseeko(f, 0, SEEK_SET);
        return -1;
    }
    return c.checksummed == 0 ? 0 : c.checksummed == c.frames ? 1 : 2;
}
