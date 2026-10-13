/* Update files: what is accepted, what is refused, and the packets that are sent. */
import assert from "node:assert/strict";
import { test } from "node:test";
import { BLOCK_SIZE, MAX_PAYLOAD, buildPackets, crc16Modbus, extractFromUpdater, parseImage, sha256Hex } from "../src/lib/image";
import { updateFile, updaterExe } from "./helpers";

test("the largest proven image is accepted, the next size is not", async () => {
  assert.equal(MAX_PAYLOAD, 112_640);
  const max = await parseImage(updateFile(MAX_PAYLOAD));
  assert.equal(max.bytes.length, 112_644);
  assert.equal(max.blocks, 56); // the updater's count includes the ID bytes: 55 blocks of image
  await assert.rejects(parseImage(updateFile(MAX_PAYLOAD + 16)), /largest size proven/);
  await assert.rejects(parseImage(updateFile(114_672)), /largest size proven/);
});

test("only the display board's firmware ID is accepted", async () => {
  const image = await parseImage(updateFile(4096));
  assert.equal(image.id, "X2Vx");
  assert.equal(image.hardwareId, 0x5832);
  for (const id of ["X1Vx", "X3Vx", "M2Vx", "X2Vy", "\0\0\0\0"]) {
    await assert.rejects(parseImage(updateFile(4096, id)), /not a GX Pro display firmware file/, id);
  }
});

test("damaged and tiny files are refused", async () => {
  await assert.rejects(parseImage(updateFile(4096).subarray(1)), /not a GX Pro display|damaged/);
  const odd = new Uint8Array(4100 + 4);
  odd.set(updateFile(16).subarray(16), 4100);
  await assert.rejects(parseImage(odd), /damaged/);
  await assert.rejects(parseImage(updateFile(512)), /too small/);
});

test("checksums", async () => {
  assert.equal(crc16Modbus(new TextEncoder().encode("123456789")), 0x4b37);
  assert.equal(await sha256Hex(new TextEncoder().encode("abc")), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  const file = updateFile(2048);
  assert.equal((await parseImage(file)).sha256, await sha256Hex(file));
});

test("packets: 2060 bytes, CRC over number, last number and data, the ID is never sent", async () => {
  const file = updateFile(3 * BLOCK_SIZE + 160);
  const image = await parseImage(file);
  const packets = buildPackets(image);
  assert.equal(packets.length, 4);
  packets.forEach((p, i) => {
    assert.equal(p.length, 2060);
    assert.deepEqual([...p.subarray(0, 2)], [0x41, 0x42]);
    assert.equal(p[2]! | (p[3]! << 8), crc16Modbus(p.subarray(4, 2054)));
    assert.deepEqual([p[4], p[5]], [i, 3]);
    assert.deepEqual([...p.subarray(2054)], [0, 0, 0, 0, 0, 0]);
  });
  const sent = new Uint8Array(packets.flatMap((p) => [...p.subarray(6, 2054)]));
  assert.deepEqual(sent.subarray(0, file.length - 4), file.subarray(0, file.length - 4));
  assert.ok(sent.subarray(file.length - 4).every((b) => b === 0xff)); // padding, not the ID
});

test("the 56th packet of the largest image carries no image data", async () => {
  const packets = buildPackets(await parseImage(updateFile(MAX_PAYLOAD)));
  assert.equal(packets.length, 56);
  assert.ok(packets[55]!.subarray(6, 2054).every((b) => b === 0xff));
});

test("the firmware is found inside an updater file", () => {
  const file = updateFile(74_672);
  assert.deepEqual(extractFromUpdater(updaterExe(file)), file);
  assert.throws(() => extractFromUpdater(updaterExe()), /No display firmware/);
  assert.throws(() => extractFromUpdater(updaterExe(updateFile(4096, "X1Vx"))), /No display firmware/);
  assert.throws(() => extractFromUpdater(updaterExe(file, updateFile(4096))), /more than one/);
  assert.throws(() => extractFromUpdater(updaterExe(updateFile(MAX_PAYLOAD + 16))), /No display firmware/);
});
