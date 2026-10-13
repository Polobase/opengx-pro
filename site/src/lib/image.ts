/**
 * GX Pro display firmware update files, as the vendor updater and `tools/flash_gx_pro.py` use them.
 *
 * An update file is the (obfuscated) application image, padded to 16 bytes, followed by a 4-byte
 * firmware ID. The first two ID bytes must equal the hardware ID the bootloader reports. The image
 * is sent in 2 KB blocks: "AB", CRC-16/MODBUS (little endian) over block number, last block number
 * and the 2048 data bytes, those bytes, and 6 zero bytes; the ID itself is never sent.
 */

export const BLOCK_SIZE = 2048;
/**
 * The largest image the bootloader is proven to take on a unit: 112,640 bytes (the 110 KB size
 * test; `FLASH_PROVEN` in the Makefile). The vendor updater counts the 4 ID bytes too, so such a
 * file is "56 blocks" of which the last carries no image data. Nothing larger is ever sent.
 */
export const MAX_PAYLOAD = 112_640;
/** The firmware ID of the display board's image. Files for other boards are refused. */
export const DISPLAY_ID = "X2Vx";

export interface FirmwareImage {
  bytes: Uint8Array;
  /** The 4-byte firmware ID as text, e.g. "X2Vx". */
  id: string;
  /** The hardware ID the image needs (the ID's first two bytes). */
  hardwareId: number;
  /** Packets to send: the vendor updater's count, which includes the ID bytes. */
  blocks: number;
  sha256: string;
}

function idText(bytes: Uint8Array): string {
  return String.fromCharCode(...bytes);
}

export async function sha256Hex(bytes: Uint8Array): Promise<string> {
  const digest = await crypto.subtle.digest("SHA-256", bytes as BufferSource);
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

/** Check an update file; throws with a plain-language reason. */
export async function parseImage(bytes: Uint8Array): Promise<FirmwareImage> {
  if (bytes.length < 1024) throw new Error("This file is too small to be a firmware update file.");
  const id = idText(bytes.subarray(bytes.length - 4));
  if (id !== DISPLAY_ID) {
    throw new Error(`This is not a GX Pro display firmware file (its firmware ID is not "${DISPLAY_ID}").`);
  }
  const payload = bytes.length - 4;
  if (payload % 16 !== 0) throw new Error("This file is damaged: its length does not fit a firmware update file.");
  if (payload > MAX_PAYLOAD) {
    throw new Error(`This firmware is ${payload.toLocaleString("en")} bytes; the largest size proven on a unit is ` +
      `${MAX_PAYLOAD.toLocaleString("en")}. It is not installed.`);
  }
  return {
    bytes,
    id,
    hardwareId: (bytes[bytes.length - 4]! << 8) | bytes[bytes.length - 3]!,
    blocks: Math.ceil(bytes.length / BLOCK_SIZE), // the vendor updater counts the ID bytes too
    sha256: await sha256Hex(bytes),
  };
}

/**
 * Find the display firmware inside the vendor's updater (`GX_Pro_Firmware_v7789.exe`): the
 * embedded resource is stored as a 4-byte little-endian length followed by the update file.
 * Exactly one such block must end in the display board's firmware ID.
 */
export function extractFromUpdater(exe: Uint8Array): Uint8Array {
  const view = new DataView(exe.buffer, exe.byteOffset, exe.byteLength);
  const hits: Uint8Array[] = [];
  for (let off = 0; off + 4 <= exe.length; off++) {
    const len = view.getUint32(off, true);
    if (len < 1024 || len > MAX_PAYLOAD + 4 || off + 4 + len > exe.length) continue;
    const end = off + 4 + len;
    if (idText(exe.subarray(end - 4, end)) === DISPLAY_ID) hits.push(exe.slice(off + 4, end));
  }
  if (hits.length !== 1) {
    throw new Error(hits.length
      ? "The updater contains more than one firmware; please use GxDisplayFW.bin instead."
      : "No display firmware was found in this file.");
  }
  return hits[0]!;
}

export function crc16Modbus(data: Uint8Array): number {
  let crc = 0xffff;
  for (const byte of data) {
    crc ^= byte;
    for (let i = 0; i < 8; i++) crc = crc & 1 ? (crc >>> 1) ^ 0xa001 : crc >>> 1;
  }
  return crc;
}

/** The update packets, one per 2 KB block (2060 bytes each). */
export function buildPackets(image: FirmwareImage): Uint8Array[] {
  const payload = image.bytes.subarray(0, image.bytes.length - 4);
  const packets: Uint8Array[] = [];
  for (let i = 0; i < image.blocks; i++) {
    const body = new Uint8Array(2 + BLOCK_SIZE).fill(0xff);
    body[0] = i;
    body[1] = image.blocks - 1;
    body.set(payload.subarray(i * BLOCK_SIZE, (i + 1) * BLOCK_SIZE), 2);
    const crc = crc16Modbus(body);
    const packet = new Uint8Array(4 + body.length + 6);
    packet.set([0x41, 0x42, crc & 0xff, crc >> 8], 0); // "AB", CRC little endian
    packet.set(body, 4);
    packets.push(packet);
  }
  return packets;
}

export function describeHardwareId(hw: number): string {
  const a = hw >> 8;
  const b = hw & 0xff;
  return a > 0x20 && a < 0x7f && b > 0x20 && b < 0x7f
    ? String.fromCharCode(a, b)
    : hw.toString(16).toUpperCase().padStart(4, "0");
}
