// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
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

  test("accept a Dictionary instance like the one-shot API", () => {
    const set = samples();
    const d = zxc.Dictionary.train(set);
    const payload = set[7];
    const cctx = new zxc.Cctx({ dict: d });
    const archive = cctx.compress(payload);
    expect(archive.equals(zxc.compress(payload, { dict: d }))).toBe(true);
    expect(zxc.getDictId(archive)).toBe(d.id);
    const dctx = new zxc.Dctx({ dict: d });
    expect(dctx.decompress(archive).equals(payload)).toBe(true);
    cctx.close();
    dctx.close();
  });

  test("compress an empty payload like the one-shot API", () => {
    const cctx = new zxc.Cctx();
    const dctx = new zxc.Dctx();
    const archive = cctx.compress(Buffer.alloc(0));
    expect(archive.equals(zxc.compress(Buffer.alloc(0)))).toBe(true);
    expect(dctx.decompress(archive).length).toBe(0);
    cctx.close();
    dctx.close();
  });

  test("check the table length even without a dictionary", () => {
    expect(() => new zxc.Cctx({ dictHuf: Buffer.alloc(3) })).toThrow();
    expect(() => new zxc.Dctx({ dictHuf: Buffer.alloc(3) })).toThrow();
  });

  test("reject a table that is not 128 bytes", () => {
    const dict = zxc.trainDict(samples());
    expect(() => new zxc.Cctx({ dict, dictHuf: Buffer.alloc(3) })).toThrow();
    expect(() => new zxc.Dctx({ dict, dictHuf: Buffer.alloc(3) })).toThrow();
  });

  test("refuse an out-of-range level at construction", () => {
    // Contexts set the level on the native context: no clamping, the setter
    // refuses and the constructor throws with the zxc error code.
    let err;
    try {
      new zxc.Cctx({ level: zxc.LEVEL_ULTRA + 1 });
    } catch (e) {
      err = e;
    }
    expect(err).toBeDefined();
    expect(err.code).toBe(zxc.ERROR_BAD_LEVEL);
    expect(err.message).toBe("ZXC_ERROR_BAD_LEVEL");
    // The one-shot API refuses too: no entry point clamps the level.
    const payload = Buffer.concat(samples());
    let oneShot;
    try {
      zxc.compress(payload, { level: zxc.LEVEL_ULTRA + 1 });
    } catch (e) {
      oneShot = e;
    }
    expect(oneShot).toBeDefined();
    expect(oneShot.code).toBe(zxc.ERROR_BAD_LEVEL);
    expect(() => zxc.compress(payload, { level: -1 })).toThrow();
    // 0 still selects the default level.
    expect(
      zxc
        .compress(payload, { level: 0 })
        .equals(zxc.compress(payload, { level: zxc.LEVEL_DEFAULT })),
    ).toBe(true);
  });

  test("refuse a non-integer level instead of coercing it", () => {
    const payload = Buffer.from("level coercion");
    const code = (fn) => {
      try {
        fn();
      } catch (e) {
        return e.code;
      }
      return undefined;
    };
    for (const level of [2 ** 32 + 5, 7.9, 0.5, NaN, Infinity, "3"]) {
      expect(code(() => zxc.compress(payload, { level }))).toBe(
        zxc.ERROR_BAD_LEVEL,
      );
      expect(code(() => new zxc.Cctx({ level }))).toBe(zxc.ERROR_BAD_LEVEL);
      expect(code(() => new zxc.CStream({ level }))).toBe(zxc.ERROR_BAD_LEVEL);
    }
    // null, like undefined, selects the default.
    expect(
      new zxc.Cctx({ level: null }).compress(payload).length,
    ).toBeGreaterThan(0);
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
