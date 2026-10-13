/* Test images and a simulated bootloader. Nothing here comes from a real firmware file. */
import { crc16Modbus } from "../src/lib/image";

/** An update file with `payload` image bytes (a multiple of 16) and the given firmware ID. */
export function updateFile(payload: number, id = "X2Vx"): Uint8Array {
  const bytes = new Uint8Array(payload + 4);
  for (let i = 0; i < payload; i++) bytes[i] = (i * 31 + (i >> 8)) & 0xff;
  bytes.set([...id].map((c) => c.charCodeAt(0)), payload);
  return bytes;
}

/** A file shaped like the vendor's updater: "MZ", other bytes, then each update file behind its 4-byte length. */
export function updaterExe(...files: Uint8Array[]): Uint8Array {
  const parts: number[] = [0x4d, 0x5a, ...new Array(300).fill(0x90)];
  for (const f of files) {
    parts.push(f.length & 0xff, (f.length >> 8) & 0xff, (f.length >> 16) & 0xff, f.length >>> 24, ...f, ...new Array(64).fill(0));
  }
  return new Uint8Array(parts);
}

/** The bootloader's side of the update protocol, as far as the flasher uses it. */
export class FakeBootloader {
  queue: number[] = [];
  received: Uint8Array[] = [];
  started = false;
  writes = 0;
  /** hw: the two ID bytes it reports; lose: block -> how many acknowledgements of it get lost; start: answer "AD". */
  constructor(readonly hw: [number, number], readonly lose: Record<number, number> = {}, readonly start = true) {}
  drain() { this.queue.length = 0; }
  async write(b: Uint8Array) {
    const s = String.fromCharCode(b[0]!, b[1]!);
    if (b.length === 2) {
      if (s === "AR") this.queue.push(this.hw[0]);
      else if (s === "AN") this.queue.push(this.hw[1]);
      else if (s === "AD" && this.start) { this.started = true; this.queue.push(0x44); }
      return;
    }
    if (!this.started || s !== "AB" || b.length !== 2060) throw new Error("unexpected bytes");
    this.writes += 1;
    const crc = b[2]! | (b[3]! << 8);
    if (crc !== crc16Modbus(b.subarray(4, 4 + 2 + 2048))) return;
    const block = b[4]!;
    if (this.lose[block]) { this.lose[block]! -= 1; return; } // the acknowledgement is lost: a resend is expected
    this.received[block] = b.slice(6, 6 + 2048);
    this.queue.push(0x43);
  }
  async readByte(_timeoutMs: number) { return this.queue.length ? this.queue.shift()! : null; }
}
