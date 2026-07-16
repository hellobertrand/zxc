/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

"use strict";

const zxc = require("../lib/index");

// =============================================================================
// Pre-trained dictionary support
// =============================================================================

// Build a set of similar samples that share a lot of structure, so a trained
// dictionary has something to capture.
function buildSamples(count = 8) {
  const samples = [];
  for (let i = 0; i < count; i++) {
    const obj = {
      id: 1000 + i,
      type: "user-record",
      createdAt: "2026-06-05T00:00:00Z",
      status: "active",
      roles: ["reader", "writer", "admin"],
      payload: `the quick brown fox jumps over the lazy dog #${i}`,
    };
    samples.push(Buffer.from(JSON.stringify(obj)));
  }
  return samples;
}

describe("trainDict", () => {
  test("produces non-empty dictionary content from similar samples", () => {
    const dict = zxc.trainDict(buildSamples(8));
    expect(Buffer.isBuffer(dict)).toBe(true);
    expect(dict.length).toBeGreaterThan(0);
    expect(dict.length).toBeLessThanOrEqual(65535);
  });

  test("honors a smaller maxSize cap", () => {
    const dict = zxc.trainDict(buildSamples(8), 1024);
    expect(dict.length).toBeLessThanOrEqual(1024);
  });

  test("rejects non-array / empty input", () => {
    expect(() => zxc.trainDict("nope")).toThrow(TypeError);
    expect(() => zxc.trainDict([])).toThrow(TypeError);
  });
});

describe("dictionary compress/decompress roundtrip", () => {
  const dict = zxc.trainDict(buildSamples(8));
  const original = Buffer.from(
    JSON.stringify({
      id: 2000,
      type: "user-record",
      createdAt: "2026-06-05T00:00:00Z",
      status: "active",
      roles: ["reader", "writer", "admin"],
      payload: "the quick brown fox jumps over the lazy dog #new",
    }),
  );

  test("compress with {dict} then decompress with {dict} equals original", () => {
    const compressed = zxc.compress(original, { dict });
    const restored = zxc.decompress(compressed, { dict });
    expect(restored).toEqual(original);
  });

  test("archive references the dictionary id", () => {
    const compressed = zxc.compress(original, { dict });
    expect(zxc.getDictId(compressed)).toBe(zxc.dictId(dict));
    expect(zxc.getDictId(compressed)).not.toBe(0);
  });

  test("decompressing a dict archive without the dictionary throws", () => {
    const compressed = zxc.compress(original, { dict });
    expect(() => zxc.decompress(compressed)).toThrow();
  });

  test("a non-dictionary archive reports dict id 0", () => {
    const compressed = zxc.compress(original);
    expect(zxc.getDictId(compressed)).toBe(0);
  });
});

describe("dictionary id consistency", () => {
  const dict = zxc.trainDict(buildSamples(8));
  const huf = zxc.trainDictHuf(buildSamples(8), dict);

  test("dictId(content) === getDictId(archive); .zxd id binds the table", () => {
    const original = Buffer.from(
      "the quick brown fox jumps over the lazy dog".repeat(4),
    );
    const compressed = zxc.compress(original, { dict });
    const zxd = zxc.dictSave(dict, huf);

    const idContent = zxc.dictId(dict);
    const idArchive = zxc.getDictId(compressed);
    const idZxd = zxc.dictGetId(zxd);

    expect(idContent).toBe(idArchive);
    // The .zxd id covers (content, table): non-zero, distinct from the
    // content-only id.
    expect(idZxd).not.toBe(0);
    expect(idZxd).not.toBe(idContent);
    expect(zxc.dictHuf(zxd)).toEqual(huf);
  });
});

describe("dictSave / dictLoad", () => {
  const dict = zxc.trainDict(buildSamples(8));
  const huf = zxc.trainDictHuf(buildSamples(8), dict);

  test("roundtrips content and id", () => {
    const zxd = zxc.dictSave(dict, huf);
    expect(Buffer.isBuffer(zxd)).toBe(true);
    expect(zxd.length).toBeGreaterThan(dict.length);

    const loaded = zxc.dictLoad(zxd);
    expect(loaded.content).toEqual(dict);
    expect(loaded.id).toBe(zxc.dictGetId(zxd));
  });

  test("dictLoad rejects garbage", () => {
    expect(() => zxc.dictLoad(Buffer.from("not a zxd file at all"))).toThrow();
  });
});

describe("Dictionary object", () => {
  test("train / save / load roundtrip and compress with {dict: Dictionary}", () => {
    const d = zxc.Dictionary.train(buildSamples(8));
    expect(Buffer.isBuffer(d.content)).toBe(true);
    expect(d.content.length).toBeGreaterThan(0);
    expect(d.huf.length).toBe(128);
    expect(d.id).not.toBe(0);

    // save() -> load() preserves the (content, table, id) triple.
    const reloaded = zxc.Dictionary.load(d.save());
    expect(reloaded.content).toEqual(d.content);
    expect(reloaded.huf).toEqual(d.huf);
    expect(reloaded.id).toBe(d.id);

    // One-call train matches the primitive 2-step training.
    expect(d.content).toEqual(zxc.trainDict(buildSamples(8)));
    expect(d.huf).toEqual(zxc.trainDictHuf(buildSamples(8), d.content));

    const original = Buffer.from(
      "the quick brown fox jumps over the lazy dog #obj".repeat(4),
    );
    const compressed = zxc.compress(original, { dict: d });
    expect(zxc.decompress(compressed, { dict: d })).toEqual(original);

    // The archive id binds (content, table): decoding with the raw
    // content but no table is rejected.
    expect(() => zxc.decompress(compressed, { dict: d.content })).toThrow();
  });
});

describe("seekable with dictionary", () => {
  const dict = zxc.trainDict(buildSamples(8));

  // A payload large enough to span multiple blocks, full of dictionary-ish
  // patterns so the dictionary is actually engaged.
  function buildPayload() {
    const parts = [];
    // > 512 KB so the seekable archive spans multiple blocks, exercising
    // cross-block random access with a dictionary attached.
    for (let i = 0; i < 24000; i++) {
      parts.push(
        `the quick brown fox jumps over the lazy dog #${i} active reader writer admin\n`,
      );
    }
    return Buffer.from(parts.join(""));
  }

  test("setDict + decompressRange roundtrip on a dict-compressed seekable archive", () => {
    const payload = buildPayload();
    const compressed = zxc.compress(payload, { seekable: true, dict });
    expect(zxc.getDictId(compressed)).toBe(zxc.dictId(dict));

    const s = new zxc.Seekable(compressed);
    try {
      expect(s.numBlocks()).toBeGreaterThan(1);
      s.setDict(dict);
      // Full-range round trip.
      const full = s.decompressRange(0, payload.length);
      expect(full).toEqual(payload);

      // A sub-range somewhere in the middle, crossing a block boundary.
      const off = 600000;
      const len = 4096;
      const slice = s.decompressRange(off, len);
      expect(slice).toEqual(payload.subarray(off, off + len));
    } finally {
      s.close();
    }
  });
});
