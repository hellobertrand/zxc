/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

"use strict";

const zxc = require("../lib/index");

// =============================================================================
// Seekable random-access decompression
// =============================================================================

function buildPayload(size) {
  const buf = Buffer.alloc(size);
  for (let i = 0; i < size; i++) buf[i] = (i * 31) & 0xff;
  return buf;
}

function buildSeekableArchive(payload) {
  return zxc.compress(payload, { seekable: true, checksum: true });
}

describe("Seekable: queries", () => {
  const payload = buildPayload(64 * 1024);
  const compressed = buildSeekableArchive(payload);

  test("reports total decompressed size and at least one block", () => {
    const s = new zxc.Seekable(compressed);
    try {
      expect(s.decompressedSize()).toBe(payload.length);
      expect(s.numBlocks()).toBeGreaterThanOrEqual(1);
    } finally {
      s.close();
    }
  });

  test("reports per-block sizes and rejects out-of-range indices", () => {
    const s = new zxc.Seekable(compressed);
    try {
      const numBlocks = s.numBlocks();
      expect(s.blockCompressedSize(0)).toBeGreaterThan(0);
      expect(s.blockDecompressedSize(0)).toBeGreaterThan(0);
      expect(s.blockCompressedSize(numBlocks)).toBeNull();
      expect(s.blockDecompressedSize(numBlocks)).toBeNull();
    } finally {
      s.close();
    }
  });
});

describe("Seekable: decompressRange", () => {
  const payload = buildPayload(64 * 1024);
  const compressed = buildSeekableArchive(payload);

  test("full-range round trip", () => {
    const s = new zxc.Seekable(compressed);
    try {
      const out = s.decompressRange(0, payload.length);
      expect(out.length).toBe(payload.length);
      expect(out.equals(payload)).toBe(true);
    } finally {
      s.close();
    }
  });

  test("mid-range slice is byte-exact", () => {
    const s = new zxc.Seekable(compressed);
    try {
      const off = 1024;
      const len = 8192;
      const out = s.decompressRange(off, len);
      expect(out.length).toBe(len);
      expect(out.equals(payload.subarray(off, off + len))).toBe(true);
    } finally {
      s.close();
    }
  });

  test("zero-length range returns empty buffer", () => {
    const s = new zxc.Seekable(compressed);
    try {
      const out = s.decompressRange(0, 0);
      expect(out.length).toBe(0);
    } finally {
      s.close();
    }
  });
});

describe("Seekable: error handling", () => {
  test("constructor throws on garbage buffer", () => {
    expect(
      () => new zxc.Seekable(Buffer.from([1, 2, 3, 4, 5, 6, 7, 8])),
    ).toThrow();
  });

  test("constructor throws on empty buffer", () => {
    expect(() => new zxc.Seekable(Buffer.alloc(0))).toThrow();
  });

  test("methods after close throw", () => {
    const s = new zxc.Seekable(buildSeekableArchive(buildPayload(4096)));
    s.close();
    expect(() => s.numBlocks()).toThrow();
    // close is idempotent
    expect(() => s.close()).not.toThrow();
  });
});

describe("Seekable: reader callback", () => {
  const payload = buildPayload(128 * 1024);
  const compressed = buildSeekableArchive(payload);

  test("roundtrip via in-memory readAt", () => {
    let calls = 0;
    const reader = {
      size: compressed.length,
      readAt(dst, offset) {
        calls++;
        compressed.copy(dst, 0, offset, offset + dst.length);
      },
    };
    const s = new zxc.Seekable(reader);
    try {
      // 3 reads at open: header, footer, seek table.
      expect(calls).toBe(3);
      expect(s.decompressedSize()).toBe(payload.length);
      const out = s.decompressRange(0, payload.length);
      expect(Buffer.compare(out, payload)).toBe(0);

      const before = calls;
      const chunk = s.decompressRange(2048, 1024);
      expect(Buffer.compare(chunk, payload.subarray(2048, 2048 + 1024))).toBe(
        0,
      );
      // Single-block sub-range must trigger exactly one extra read.
      expect(calls - before).toBe(1);
    } finally {
      s.close();
    }
  });

  test("readAt throwing maps to decompress_range error", () => {
    let attempted = 0;
    const reader = {
      size: compressed.length,
      readAt(dst, offset) {
        attempted++;
        if (attempted > 3) throw new Error("boom");
        compressed.copy(dst, 0, offset, offset + dst.length);
      },
    };
    const s = new zxc.Seekable(reader);
    try {
      expect(() => s.decompressRange(0, payload.length)).toThrow();
    } finally {
      s.close();
    }
  });

  test("rejects missing size / readAt", () => {
    expect(() => new zxc.Seekable({})).toThrow();
    expect(() => new zxc.Seekable({ size: 100 })).toThrow();
    expect(() => new zxc.Seekable({ readAt: () => {} })).toThrow();
    expect(() => new zxc.Seekable({ size: 0, readAt: () => {} })).toThrow();
  });

  test("rejects garbage reader", () => {
    const reader = {
      size: 64,
      readAt(dst /* , offset */) {
        dst.fill(0);
      },
    };
    expect(() => new zxc.Seekable(reader)).toThrow();
  });
});

describe("Seekable: low-level seek table helpers", () => {
  test("seekTableSize / writeSeekTable round trip", () => {
    const compSizes = [128, 256, 200, 4];
    const sz = zxc.seekTableSize(compSizes.length);
    expect(sz).toBeGreaterThan(0);
    const buf = zxc.writeSeekTable(compSizes);
    expect(buf.length).toBe(sz);
  });

  test("writeSeekTable rejects empty array", () => {
    expect(() => zxc.writeSeekTable([])).toThrow();
  });
});

describe("Seekable: checksum switch", () => {
  const payload = buildPayload(256 * 1024);

  test("the switch toggles cleanly at any time on an intact archive", () => {
    const s = new zxc.Seekable(buildSeekableArchive(payload));
    try {
      expect(s.decompressRange(0, 512)).toEqual(payload.subarray(0, 512));
      s.setChecksum(false);
      expect(s.decompressRange(0, 512)).toEqual(payload.subarray(0, 512));
      s.setChecksum(true);
      expect(s.decompressRange(0, 512)).toEqual(payload.subarray(0, 512));
    } finally {
      s.close();
    }
  });

  // Incompressible on purpose: a RAW block memcpys a flipped byte straight
  // through, so the decode succeeds and only the checksum objects. A
  // compressible payload would pin this test to today's encoder output.
  test("a corrupted block is silent without verification", () => {
    let rng = 0x2e5b9a17;
    const raw = Buffer.alloc(256 * 1024);
    for (let i = 0; i < raw.length; i++) {
      rng = (Math.imul(rng, 1103515245) + 12345) >>> 0;
      raw[i] = (rng >>> 16) & 0xff;
    }
    const bad = Buffer.from(
      zxc.compress(raw, { seekable: true, checksum: true }),
    );
    expect(bad[16]).toBe(0); // block 0 must be RAW
    bad[16 + 8 + 4] ^= 0xff;

    const on = new zxc.Seekable(bad);
    try {
      on.setChecksum(true);
      expect(() => on.decompressRange(0, 512)).toThrow(/BAD_CHECKSUM/);
    } finally {
      on.close();
    }

    // Default is off: bytes come back, wrong, with no error.
    const off = new zxc.Seekable(bad);
    try {
      expect(off.decompressRange(0, 512)).not.toEqual(raw.subarray(0, 512));
    } finally {
      off.close();
    }
  });

  test("setChecksum rejects a non-boolean", () => {
    const s = new zxc.Seekable(buildSeekableArchive(payload));
    try {
      expect(() => s.setChecksum("yes")).toThrow(TypeError);
    } finally {
      s.close();
    }
  });
});
