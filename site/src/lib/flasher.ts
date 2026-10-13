/**
 * The GX Pro bootloader's update protocol (the same as the vendor updater and
 * `tools/flash_gx_pro.py`). Only the application area is written; the bootloader itself and the
 * option bytes cannot be reached through this protocol.
 *
 *   "AR" -> 1 byte   hardware ID, high byte
 *   "AN" -> 1 byte   hardware ID, low byte
 *   "AD" -> "D"      start (the display board's firmware type)
 *   per block: packet -> "C" within 2 s when it arrived intact, else it is sent again
 *   (at most 4 resends in the whole update, as the vendor updater allows)
 */

import { BytePort } from "./byteport";
import { buildPackets, describeHardwareId, type FirmwareImage } from "./image";

const REPLY_TIMEOUT_MS = 500;
const ACK_TIMEOUT_MS = 2000;
const MAX_RESENDS = 4;
const ACK = 0x43; // "C"
const STARTED = 0x44; // "D"

export interface FlashEvents {
  log(message: string): void;
  progress(done: number, total: number): void;
}

const ascii = (s: string) => new TextEncoder().encode(s);
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

async function request(port: BytePort, command: string): Promise<number> {
  port.drain();
  await port.write(ascii(command));
  return (await port.readByte(REPLY_TIMEOUT_MS)) ?? 0;
}

/** The hardware ID the bootloader reports (0 when nothing answers: not in update mode). */
export async function readHardwareId(port: BytePort): Promise<number> {
  await sleep(20);
  const high = await request(port, "AR");
  const low = await request(port, "AN");
  return (high << 8) | low;
}

async function waitForAck(port: BytePort): Promise<boolean> {
  const deadline = performance.now() + ACK_TIMEOUT_MS;
  for (;;) {
    const left = deadline - performance.now();
    if (left <= 0) return false;
    const b = await port.readByte(left);
    if (b === ACK) return true;
    if (b === null) return false;
  }
}

export async function flashImage(port: BytePort, image: FirmwareImage, events: FlashEvents): Promise<void> {
  const hw = await readHardwareId(port);
  if (hw === 0) {
    throw new Error("The unit does not answer. Is it in update mode (switch off, hold STOP, press POWER), " +
      "and is this the right USB port?");
  }
  if (hw !== image.hardwareId) {
    throw new Error(`This port answered "${describeHardwareId(hw)}", the firmware needs ` +
      `"${describeHardwareId(image.hardwareId)}". Choose the other USB port of the unit.`);
  }
  events.log(`Unit in update mode found (hardware ID ${describeHardwareId(hw)}).`);

  if ((await request(port, "AD")) !== STARTED) {
    throw new Error("The unit did not accept the start of the update. Switch it off, put it into update mode " +
      "again and retry.");
  }

  const packets = buildPackets(image);
  events.log(`Sending ${packets.length} blocks.`);
  events.progress(0, packets.length);
  let resends = 0;
  for (let i = 0; i < packets.length; ) {
    port.drain();
    await port.write(packets[i]!);
    if (await waitForAck(port)) {
      i += 1;
      events.progress(i, packets.length);
      continue;
    }
    if (resends >= MAX_RESENDS) {
      throw new Error("Too many transfer errors. The unit stays in update mode: switch it off, put it into " +
        "update mode again and retry.");
    }
    resends += 1;
    events.log(`Block ${i + 1} of ${packets.length}: checksum error, sending it again (${resends}/${MAX_RESENDS}).`);
  }
  events.log("All blocks confirmed by the unit.");
}
