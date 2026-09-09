/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Tests for the reusable Cctx / Dctx contexts.
 */

"use strict";

const zxc = require("../lib/index");

function samples(count = 64) {
  return Array.from({ length: count }, (_, i) =>
    Buffer.from(
      JSON.stringify({
        id: 1000 + i,
        user: `alice_${i}`,
        mail: `alice${i}@example.com`,
        role: "member",
      }),
    ),
  );
}

describe("Cctx / Dctx", () => {
  test("produce the same archive as the one-shot API", () => {
    const payload = Buffer.concat(samples());
    const cctx = new zxc.Cctx({ level: zxc.LEVEL_DEFAULT });
    const dctx = new zxc.Dctx();
    const archive = cctx.compress(payload);
    expect(archive.equals(zxc.compress(payload))).toBe(true);
    expect(dctx.decompress(archive).equals(payload)).toBe(true);
    cctx.close();
    dctx.close();
  });

  test("are reusable across payloads", () => {
    const payloads = samples(16);
    const cctx = new zxc.Cctx();
    const dctx = new zxc.Dctx();
    const archives = payloads.map((p) => cctx.compress(p));
    payloads.forEach((p, i) => {
      expect(dctx.decompress(archives[i]).equals(p)).toBe(true);
    });
    cctx.close();
    dctx.close();
  });

  test("honour the checksum option", () => {
    const payload = Buffer.concat(samples());
    const cctx = new zxc.Cctx({ checksum: true });
    const archive = cctx.compress(payload);
    expect(archive.equals(zxc.compress(payload, { checksum: true }))).toBe(
      true,
    );
    const dctx = new zxc.Dctx({ checksum: true });
    expect(dctx.decompress(archive).equals(payload)).toBe(true);
    cctx.close();
    dctx.close();
  });

  test("apply the dictionary to every call", () => {
    const set = samples();
    const dict = zxc.trainDict(set);
    const dictHuf = zxc.trainDictHuf(set, dict);
    const payload = set[7];

    const cctx = new zxc.Cctx({ dict, dictHuf });
    const first = cctx.compress(payload);
    const second = cctx.compress(payload);
    expect(first.equals(second)).toBe(true);
    expect(zxc.getDictId(first)).not.toBe(0);

    const dctx = new zxc.Dctx({ dict, dictHuf });
    expect(dctx.decompress(first).equals(payload)).toBe(true);

    // The archive binds the dictionary: decoding without it must fail.
    const plain = new zxc.Dctx();
    expect(() => plain.decompress(first)).toThrow();
    cctx.close();
    dctx.close();
    plain.close();
  });

  test("reject a table that is not 128 bytes", () => {
    const dict = zxc.trainDict(samples());
    expect(() => new zxc.Cctx({ dict, dictHuf: Buffer.alloc(3) })).toThrow();
    expect(() => new zxc.Dctx({ dict, dictHuf: Buffer.alloc(3) })).toThrow();
  });

  test("refuse calls after close", () => {
    const payload = Buffer.concat(samples());
    const cctx = new zxc.Cctx();
    const archive = cctx.compress(payload);
    cctx.close();
    cctx.close(); // idempotent
    expect(() => cctx.compress(payload)).toThrow();

    const dctx = new zxc.Dctx();
    expect(dctx.decompress(archive).equals(payload)).toBe(true);
    dctx.close();
    expect(() => dctx.decompress(archive)).toThrow();
  });
});
