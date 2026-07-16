/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Tests for the stream.Transform adapters and detectZxc helper.
 */

"use strict";

const { Readable, PassThrough } = require("node:stream");
const { pipeline } = require("node:stream/promises");
const zxc = require("../lib/index");

async function gather(stream) {
  const chunks = [];
  for await (const c of stream) chunks.push(c);
  return Buffer.concat(chunks);
}

async function roundtripPipeline(data, opts = {}) {
  const compressed = await gather(
    Readable.from([data]).pipe(zxc.createCompressStream(opts)),
  );
  const decompressed = await gather(
    Readable.from([compressed]).pipe(zxc.createDecompressStream(opts)),
  );
  return decompressed;
}

describe("CompressStream / DecompressStream roundtrip", () => {
  test("small buffer", async () => {
    const data = Buffer.from("Hello stream.Transform bridge over ZXC!");
    const got = await roundtripPipeline(data);
    expect(Buffer.compare(got, data)).toBe(0);
  });

  test("large 2 MB buffer", async () => {
    const data = Buffer.alloc(2 * 1024 * 1024);
    for (let i = 0; i < data.length; i++) data[i] = (i * 13) % 251;
    const got = await roundtripPipeline(data);
    expect(got.length).toBe(data.length);
    expect(Buffer.compare(got, data)).toBe(0);
  });

  test("many small chunks fed sequentially", async () => {
    const want = Buffer.alloc(64 * 1024);
    for (let i = 0; i < want.length; i++) want[i] = (i ^ 0x5a) & 0xff;

    // Feed 257-byte chunks to exercise buffering boundaries.
    const chunks = [];
    for (let i = 0; i < want.length; i += 257) {
      chunks.push(want.subarray(i, Math.min(i + 257, want.length)));
    }
    const compressed = await gather(
      Readable.from(chunks).pipe(zxc.createCompressStream()),
    );
    const got = await gather(
      Readable.from([compressed]).pipe(zxc.createDecompressStream()),
    );
    expect(Buffer.compare(got, want)).toBe(0);
  });

  test("with checksum option", async () => {
    const data = Buffer.alloc(32 * 1024);
    for (let i = 0; i < data.length; i++) data[i] = i & 0xff;
    const got = await roundtripPipeline(data, { checksum: true });
    expect(Buffer.compare(got, data)).toBe(0);
  });

  test("via pipeline()", async () => {
    const data = Buffer.from("pipeline integration smoke test".repeat(100));
    const intermediate = new PassThrough();
    const finalSink = new PassThrough();
    const collected = gather(finalSink);

    await pipeline(
      Readable.from([data]),
      zxc.createCompressStream(),
      intermediate,
      zxc.createDecompressStream(),
      finalSink,
    );

    expect(Buffer.compare(await collected, data)).toBe(0);
  });
});

describe("DecompressStream error paths", () => {
  test("truncated frame emits ZXC_TRUNCATED", async () => {
    const data = Buffer.alloc(32 * 1024, 0x41);
    const compressed = await gather(
      Readable.from([data]).pipe(zxc.createCompressStream()),
    );
    const truncated = compressed.subarray(0, Math.floor(compressed.length / 2));

    await expect(
      gather(Readable.from([truncated]).pipe(zxc.createDecompressStream())),
    ).rejects.toMatchObject({ code: "ZXC_TRUNCATED" });
  });
});

describe("detectZxc", () => {
  test("detects a frame produced by compress()", () => {
    const frame = zxc.compress(Buffer.from("sniff me"));
    expect(zxc.detectZxc(frame)).toBe(true);
  });

  test("detects a frame produced by CompressStream", async () => {
    const frame = await gather(
      Readable.from([Buffer.from("hi")]).pipe(zxc.createCompressStream()),
    );
    expect(zxc.detectZxc(frame)).toBe(true);
  });

  test.each([
    ["empty", Buffer.alloc(0)],
    ["too short", Buffer.from([0xf5, 0x2e, 0xb0])],
    ["zeros", Buffer.alloc(4)],
    ["random text", Buffer.from("not a zxc frame at all")],
  ])("rejects %s", (_name, buf) => {
    expect(zxc.detectZxc(buf)).toBe(false);
  });

  test("accepts Uint8Array", () => {
    const frame = zxc.compress(Buffer.from("x"));
    const u8 = new Uint8Array(frame);
    expect(zxc.detectZxc(u8)).toBe(true);
  });

  test("returns false for null/undefined", () => {
    expect(zxc.detectZxc(null)).toBe(false);
    expect(zxc.detectZxc(undefined)).toBe(false);
  });
});
