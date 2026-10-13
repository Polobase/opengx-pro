/* The flashing logic against a simulated bootloader. */
import assert from "node:assert/strict";
import { test } from "node:test";
import { flashImage } from "../src/lib/flasher";
import { parseImage } from "../src/lib/image";
import { FakeBootloader, updateFile } from "./helpers";

const quiet = { log() {}, progress() {} };

test("an update arrives byte for byte, also when an acknowledgement is lost", async () => {
  const file = updateFile(20 * 2048 + 320);
  const image = await parseImage(file);
  const dev = new FakeBootloader([0x58, 0x32], { 7: 1 });
  const log: string[] = [];
  let done = 0;
  await flashImage(dev as never, image, { log: (m) => log.push(m), progress: (d) => (done = d) });
  assert.equal(done, image.blocks);
  assert.equal(dev.writes, image.blocks + 1);
  const sent = new Uint8Array(dev.received.flatMap((b) => [...b]));
  assert.deepEqual(sent.subarray(0, file.length - 4), file.subarray(0, file.length - 4));
  assert.ok(sent.subarray(file.length - 4).every((b) => b === 0xff));
  assert.ok(log.some((l) => l.includes("sending it again (1/4)")));
});

test("four resends are allowed in a whole update, the fifth stops it", async () => {
  const image = await parseImage(updateFile(12 * 2048));
  const four = new FakeBootloader([0x58, 0x32], { 2: 2, 9: 2 });
  await flashImage(four as never, image, quiet);
  assert.equal(four.received.length, image.blocks);
  const five = new FakeBootloader([0x58, 0x32], { 2: 2, 9: 3 });
  await assert.rejects(flashImage(five as never, image, quiet), /Too many transfer errors/);
});

test("the unit's other channel is refused before anything is sent", async () => {
  const dev = new FakeBootloader([0x58, 0x31]); // answers "X1"
  await assert.rejects(flashImage(dev as never, await parseImage(updateFile(4096)), quiet), /answered "X1".*needs "X2"/);
  assert.equal(dev.writes, 0);
  assert.equal(dev.started, false);
});

test("no answer (not in update mode) is refused", async () => {
  const dev = new FakeBootloader([0, 0]);
  await assert.rejects(flashImage(dev as never, await parseImage(updateFile(4096)), quiet), /does not answer/);
  assert.equal(dev.writes, 0);
});

test("a start that is not acknowledged sends no block", async () => {
  const dev = new FakeBootloader([0x58, 0x32], {}, false);
  await assert.rejects(flashImage(dev as never, await parseImage(updateFile(4096)), quiet), /did not accept the start/);
  assert.equal(dev.writes, 0);
});
