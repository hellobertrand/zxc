// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */

"use strict";

const { spawnSync } = require("node:child_process");
const crypto = require("node:crypto");
const v8 = require("node:v8");
const vm = require("node:vm");
const zxc = require("../lib/index");

v8.setFlagsFromString("--expose-gc");
const gc = vm.runInNewContext("gc");

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

  test("block indices are 64-bit, but must stay exact JS integers", () => {
    const s = new zxc.Seekable(compressed);
    try {
      // Above 2^32: no longer truncated into range, just out of range.
      expect(s.blockCompressedSize(2 ** 40)).toBeNull();
      expect(s.blockDecompressedSize(Number.MAX_SAFE_INTEGER)).toBeNull();
      for (const bad of [-1, 1.5, 2 ** 53, Infinity, NaN]) {
        expect(() => s.blockCompressedSize(bad)).toThrow(RangeError);
        expect(() => s.blockDecompressedSize(bad)).toThrow(RangeError);
      }
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
      // 3 reads at open: file header, footer, EOF and SEK headers.
      expect(calls).toBe(3);
      expect(s.decompressedSize()).toBe(payload.length);
      const out = s.decompressRange(0, payload.length);
      expect(Buffer.compare(out, payload)).toBe(0);

      const before = calls;
      const chunk = s.decompressRange(2048, 1024);
      expect(Buffer.compare(chunk, payload.subarray(2048, 2048 + 1024))).toBe(
        0,
      );
      // Single-block sub-range: its seek table entries, then the block.
      expect(calls - before).toBe(2);
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

  test("setDict() from readAt during decompressRange is refused", () => {
    let refused = null;
    let s;
    const reader = {
      size: compressed.length,
      readAt(dst, offset) {
        if (refused === null && s) {
          try {
            s.setDict(Buffer.alloc(64, 0x78));
            refused = false;
          } catch {
            refused = true;
          }
        }
        compressed.copy(dst, 0, offset, offset + dst.length);
      },
    };
    s = new zxc.Seekable(reader);
    try {
      const out = s.decompressRange(0, payload.length);
      expect(refused).toBe(true);
      expect(Buffer.compare(out, payload)).toBe(0);
    } finally {
      s.close();
    }
  });

  test("close() from readAt during blockCompressedSize cannot free the handle", () => {
    // blockCompressedSize reads the block's entry through readAt, so it
    // re-enters JS like decompressRange: the same guard must hold, or a
    // close() from the callback frees the handle the library is still using.
    let attempted = 0;
    let s;
    const reader = {
      size: compressed.length,
      readAt(dst, offset) {
        attempted++;
        if (attempted > 3) s.close(); // refused: throws inside the callback
        compressed.copy(dst, 0, offset, offset + dst.length);
      },
    };
    s = new zxc.Seekable(reader);
    try {
      expect(() => s.blockCompressedSize(0)).toThrow();
      // The handle survived the attempt and still answers.
      expect(s.numBlocks()).toBeGreaterThanOrEqual(1);
      expect(s.blockDecompressedSize(0)).toBeGreaterThan(0);
    } finally {
      s.close();
    }
  });

  test("setDict() from readAt during decompressRange is refused", () => {
    // The running decode uses the dictionary a setDict would free.
    let refused = null;
    let s;
    const reader = {
      size: compressed.length,
      readAt(dst, offset) {
        if (refused === null && s) {
          try {
            s.setDict(Buffer.alloc(64, 0x78));
            refused = false;
          } catch {
            refused = true;
          }
        }
        compressed.copy(dst, 0, offset, offset + dst.length);
      },
    };
    s = new zxc.Seekable(reader);
    try {
      const out = s.decompressRange(0, payload.length);
      expect(refused).toBe(true);
      expect(Buffer.compare(out, payload)).toBe(0);
    } finally {
      s.close();
    }
  });

  test("readAt throwing maps to a blockCompressedSize error, not a size of 0", () => {
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
      expect(() => s.blockCompressedSize(0)).toThrow();
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

describe("Seekable: reader callback contract", () => {
  // Incompressible, so blocks are stored raw and unwritten bytes reach the output.
  const payload = crypto.randomBytes(600000);
  const raw = zxc.compress(payload, { seekable: true, level: 1 });
  const isBlockRead = (dst) => dst.length > 100000;

  function open(onBlockRead) {
    return new zxc.Seekable({
      size: raw.length,
      readAt(dst, offset) {
        if (isBlockRead(dst)) return onBlockRead(dst, offset);
        raw.copy(dst, 0, offset, offset + dst.length);
      },
    });
  }

  test("detaching the buffer in readAt is an I/O error", () => {
    const s = open((dst, offset) => {
      raw.copy(dst, 0, offset, offset + dst.length);
      structuredClone(dst.buffer, { transfer: [dst.buffer] });
    });
    try {
      expect(() => s.decompressRange(0, 4096)).toThrow(
        expect.objectContaining({ code: zxc.ERROR_IO }),
      );
    } finally {
      s.close();
    }
  });

  test("a returned short count is an I/O error", () => {
    const s = open((dst, offset) => raw.copy(dst, 0, offset, offset + 64));
    try {
      expect(() => s.decompressRange(0, 4096)).toThrow(
        expect.objectContaining({ code: zxc.ERROR_IO }),
      );
    } finally {
      s.close();
    }
  });

  test.each([
    ["a Promise", async () => {}],
    ["a BigInt", () => 5n],
    ["a string", () => "done"],
    ["null", () => null],
    ["an object", () => ({})],
    ["true", () => true],
  ])("returning %s is an I/O error", (_, result) => {
    const s = open((dst, offset) => {
      raw.copy(dst, 0, offset, offset + dst.length);
      return result();
    });
    try {
      expect(() => s.decompressRange(0, 4096)).toThrow(
        expect.objectContaining({ code: zxc.ERROR_IO }),
      );
    } finally {
      s.close();
    }
  });

  test("a full count, and no return value, both succeed", () => {
    for (const fill of [
      (dst, offset) => raw.copy(dst, 0, offset, offset + dst.length),
      (dst, offset) => {
        raw.copy(dst, 0, offset, offset + dst.length);
      },
    ]) {
      const s = open(fill);
      try {
        expect(
          s.decompressRange(0, 4096).equals(payload.subarray(0, 4096)),
        ).toBe(true);
      } finally {
        s.close();
      }
    }
  });

  test("bytes readAt leaves unwritten are zero, not stale heap", () => {
    const s = open((dst, offset) => {
      raw.copy(dst, 0, offset, offset + 64);
    });
    try {
      for (let round = 0; round < 10; round++) {
        for (let i = 0; i < 5; i++)
          Buffer.allocUnsafeSlow(524296).fill("STALE");
        gc();
        const out = s.decompressRange(0, 4096);
        expect(out.subarray(64).every((b) => b === 0)).toBe(true);
      }
    } finally {
      s.close();
    }
  });
});

describe("Seekable: decompressRange arguments", () => {
  const payload = buildPayload(64 * 1024);
  const compressed = buildSeekableArchive(payload);

  test.each([
    [Infinity, 1],
    [NaN, 10],
    [1.5, 10],
    [-1, 1],
    [-1, 0],
    [2 ** 53, 1],
    [0, NaN],
    [0, Infinity],
    [0, 1.5],
    [0, -1],
  ])("range(%s, %s) throws a RangeError", (offset, length) => {
    const s = new zxc.Seekable(compressed);
    try {
      expect(() => s.decompressRange(offset, length)).toThrow(RangeError);
    } finally {
      s.close();
    }
  });
});

describe("Seekable: setDict(Dictionary)", () => {
  test("takes a Dictionary, and raw pieces as before", () => {
    const samples = [];
    for (let i = 0; i < 200; i++) {
      samples.push(
        Buffer.from(
          `{"user":"u${i}","txt":"the quick brown fox ${i % 7}"}`.repeat(4),
        ),
      );
    }
    const d = zxc.Dictionary.train(samples);
    const data = Buffer.concat(samples);
    const archive = zxc.compress(data, { seekable: true, dict: d });
    for (const attach of [
      (s) => s.setDict(d),
      (s) => s.setDict(d.content, d.huf),
    ]) {
      const s = new zxc.Seekable(archive);
      try {
        attach(s);
        expect(s.decompressRange(0, 64).equals(data.subarray(0, 64))).toBe(
          true,
        );
      } finally {
        s.close();
      }
    }
  });
});

describe("Seekable: close() releases memory", () => {
  test("the archive copy goes back at close, not at GC", () => {
    const size = 64 * 1024 * 1024;
    const archive = zxc.compress(crypto.randomBytes(size), {
      seekable: true,
      level: 1,
    });
    // RSS need not drop at free(), but freed memory gets reused. The closed
    // wrappers stay reachable, so only close() can give their copies back.
    const closed = [];
    const before = process.memoryUsage().rss;
    for (let i = 0; i < 8; i++) {
      const s = new zxc.Seekable(archive);
      s.close();
      closed.push(s);
    }
    expect(process.memoryUsage().rss - before).toBeLessThan(4 * size);
    expect(closed.length).toBe(8);
  });

  test("the reader callback goes back at close, not at GC", async () => {
    const payload = buildPayload(64 * 1024);
    const compressed = buildSeekableArchive(payload);
    let readAt = (dst, offset) => {
      compressed.copy(dst, 0, offset, offset + dst.length);
    };
    const weak = new WeakRef(readAt);
    const s = new zxc.Seekable({ size: compressed.length, readAt });
    readAt = null;
    s.close();
    // WeakRef keeps its target alive until the current job ends.
    await new Promise(setImmediate);
    gc();
    expect(weak.deref()).toBeUndefined();
    expect(() => s.numBlocks()).toThrow(/closed/);
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

  test.each([2 ** 32 + 5, 2 ** 32, -1, 1.5, NaN, Infinity])(
    "writeSeekTable refuses the entry %s",
    (bad) => {
      expect(() => zxc.writeSeekTable([100, bad])).toThrow(RangeError);
    },
  );

  test("writeSeekTable takes the whole u32 range", () => {
    expect(zxc.writeSeekTable([0, 0xffffffff]).length).toBe(
      zxc.seekTableSize(2),
    );
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

    // Opted out: bytes come back, wrong, with no error.
    const off = new zxc.Seekable(bad);
    try {
      off.setChecksum(false);
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

describe("Checksum defaults", () => {
  // Incompressible, so the block is stored raw: a flipped payload byte decodes
  // fine and only the checksum can object.
  const payload = crypto.randomBytes(64 * 1024);
  const flipPayload = (archive) => {
    const bad = Buffer.from(archive);
    bad[16 + 8 + 100] ^= 0xff;
    return bad;
  };
  const signed = (seekable) =>
    zxc.compress(payload, { level: 1, checksum: true, seekable });
  const unsigned = (seekable) => zxc.compress(payload, { level: 1, seekable });
  const pump = (stream, input) =>
    new Promise((resolve, reject) => {
      const out = [];
      stream.on("data", (c) => out.push(c));
      stream.on("end", () => resolve(Buffer.concat(out)));
      stream.on("error", reject);
      stream.end(input);
    });
  const framePaths = {
    decompress: (a, o) => zxc.decompress(a, o),
    Dctx: (a, o) => {
      const d = new zxc.Dctx(o);
      try {
        return d.decompress(a);
      } finally {
        d.close();
      }
    },
    DStream: (a, o) => {
      const d = new zxc.DStream(o);
      try {
        return d.decompress(a);
      } finally {
        d.close();
      }
    },
    DecompressStream: (a, o) => pump(zxc.createDecompressStream(o), a),
  };
  const seekRead = (open) => (a, o) => {
    const s = new zxc.Seekable(open(a));
    try {
      if (o) s.setChecksum(o.checksum);
      return s.decompressRange(0, payload.length);
    } finally {
      s.close();
    }
  };
  const seekPaths = {
    "Seekable(Buffer)": seekRead((a) => a),
    "Seekable(reader)": seekRead((a) => ({
      size: a.length,
      readAt: (b, o) => a.copy(b, 0, o, o + b.length),
    })),
  };
  const paths = [
    ...Object.entries(framePaths).map(([n, read]) => [n, false, read]),
    ...Object.entries(seekPaths).map(([n, read]) => [n, true, read]),
  ];

  test("compression writes none unless asked", async () => {
    const cctx = new zxc.Cctx();
    const cs = new zxc.CStream();
    const archives = [
      zxc.compress(payload),
      cctx.compress(payload),
      Buffer.concat([cs.compress(payload), cs.end()]),
      await pump(zxc.createCompressStream(), payload),
    ];
    cctx.close();
    cs.close();
    for (const a of archives) {
      expect(zxc.getFrameInfo(a).hasChecksum).toBe(false);
    }
  });

  test.each(paths)(
    "%s rejects a corrupted archive by default",
    async (_, seekable, read) => {
      const bad = flipPayload(signed(seekable));
      await expect((async () => read(bad))()).rejects.toMatchObject({
        code: zxc.ERROR_BAD_CHECKSUM,
      });
    },
  );

  test.each(paths)("%s opts out", async (_, seekable, read) => {
    const out = await read(flipPayload(signed(seekable)), { checksum: false });
    expect(out.length).toBe(payload.length);
    expect(out.equals(payload)).toBe(false);
  });

  test.each(paths)(
    "%s reads an archive without checksums",
    async (_, seekable, read) => {
      expect((await read(unsigned(seekable))).equals(payload)).toBe(true);
    },
  );

  test.each([1, 0, "yes", "no", {}])("checksum: %j is refused", (bad) => {
    const opts = { checksum: bad };
    const plain = unsigned(false);
    for (const open of [
      () => zxc.compress(payload, opts),
      () => zxc.decompress(plain, opts),
      () => new zxc.Cctx(opts),
      () => new zxc.Dctx(opts),
      () => new zxc.CStream(opts),
      () => new zxc.DStream(opts),
      () => zxc.createCompressStream(opts),
      () => zxc.createDecompressStream(opts),
    ]) {
      expect(open).toThrow(TypeError);
    }
  });

  test("an absent checksum option keeps the default", () => {
    for (const checksum of [undefined, null]) {
      const a = zxc.compress(payload, { checksum });
      expect(zxc.getFrameInfo(a).hasChecksum).toBe(false);
      expect(() =>
        zxc.decompress(flipPayload(signed(false)), { checksum }),
      ).toThrow(expect.objectContaining({ code: zxc.ERROR_BAD_CHECKSUM }));
    }
  });
});

describe("Decompressed size limits", () => {
  const payload = Buffer.alloc(1000, 7);
  const archive = zxc.compress(payload);

  test("maxOutputSize refuses a larger size, on decompress and Dctx", () => {
    expect(zxc.decompress(archive, { maxOutputSize: 1000 }).length).toBe(1000);
    expect(() => zxc.decompress(archive, { maxOutputSize: 999 })).toThrow(
      /maxOutputSize/,
    );
    expect(() =>
      zxc.decompress(archive, { size: 5000, maxOutputSize: 2000 }),
    ).toThrow(/maxOutputSize/);

    const fits = new zxc.Dctx({ maxOutputSize: 1000 });
    const over = new zxc.Dctx({ maxOutputSize: 999 });
    try {
      expect(fits.decompress(archive).length).toBe(1000);
      expect(() => over.decompress(archive)).toThrow(/maxOutputSize/);
    } finally {
      fits.close();
      over.close();
    }
  });

  test.each([-1, 1.5, NaN, Infinity, "10"])(
    "maxOutputSize %s is refused",
    (bad) => {
      const opts = { maxOutputSize: bad };
      expect(() => zxc.decompress(archive, opts)).toThrow(RangeError);
      expect(() => new zxc.Dctx(opts)).toThrow(RangeError);
    },
  );

  // In a child: a failed allocation used to abort the whole process. 2^48 bytes
  // cannot be mapped on any target, and 1 GiB is what the footer check wants for
  // that claim at 2 MB blocks; the padding stays untouched, so it costs no RSS.
  test("a size that cannot be allocated throws, with or without the option", () => {
    const script = `
      const zxc = require(${JSON.stringify(require.resolve("../lib/index"))});
      const le = (n) => {
        const out = [];
        for (let v = BigInt(n); v > 0n; v >>= 8n) out.push(Number(v & 255n));
        return out;
      };
      const cs = new zxc.CStream({ blockSize: 2 ** 21 });
      const seed = Buffer.concat([cs.compress(Buffer.from("abc")), cs.end()]);
      const L = seed[seed.length - 1];
      const body = seed.subarray(0, seed.length - 3 - (L & 7) - (L >> 4));
      const claim = 2 ** 48;
      const sizes = le(claim);
      const padTo = 2 ** 30 + 4096;
      const frame = le(padTo + sizes.length + 4 + 1);
      const forged = Buffer.alloc(padTo + sizes.length + frame.length + 1);
      body.copy(forged);
      Buffer.from([...sizes, ...frame]).copy(forged, padTo);
      forged[forged.length - 1] = (sizes.length - 1) | ((frame.length - 1) << 4);
      if (zxc.getDecompressedSize(forged) !== claim) throw new Error("not forged");

      const sk = zxc.compress(Buffer.alloc(100), { seekable: true });
      const s = new zxc.Seekable(sk);
      const failing = {
        decompress: () => zxc.decompress(forged),
        size: () => zxc.decompress(sk, { size: claim }),
        dctx: () => new zxc.Dctx().decompress(forged),
        range: () => s.decompressRange(0, claim),
      };
      for (const [name, fn] of Object.entries(failing)) {
        let err = null;
        try {
          fn();
        } catch (e) {
          err = e;
        }
        if (!(err instanceof RangeError)) throw new Error(name + ": " + err);
      }
      let refused = "";
      try {
        zxc.decompress(forged, { maxOutputSize: 1000 });
      } catch (e) {
        refused = e.message;
      }
      if (!/maxOutputSize/.test(refused)) throw new Error(refused);
      console.log("thrown");
    `;
    const run = spawnSync(process.execPath, ["-e", script], {
      encoding: "utf8",
      timeout: 60000,
    });
    expect(run.signal, run.stderr).toBeNull();
    expect(run.status, run.stderr).toBe(0);
    expect(run.stdout.trim()).toBe("thrown");
  });
});
