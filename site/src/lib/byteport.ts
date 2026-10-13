/**
 * A Web Serial port for byte-level protocols (the GX Pro bootloader answers single bytes).
 *
 * Received bytes are collected by a read loop into a queue; `readByte` waits for one with a
 * timeout. Chrome, Edge and Opera on desktop support Web Serial, over HTTPS or localhost.
 */

export const WCH_USB_VENDOR_ID = 0x1a86; // the GX Pro's USB-serial bridge

export function webSerialSupported(): boolean {
  return typeof navigator !== "undefined" && "serial" in navigator;
}

export class BytePort {
  private reader?: ReadableStreamDefaultReader<Uint8Array>;
  private writer?: WritableStreamDefaultWriter<Uint8Array>;
  private queue: number[] = [];
  private waiters: Array<() => void> = [];
  private loop?: Promise<void>;
  private opened = false;

  readonly port: SerialPort

  constructor(port: SerialPort) {
    this.port = port
  }

  /** Ports of the unit the user has already allowed on this site (no chooser needed). */
  static async granted(): Promise<BytePort[]> {
    if (!webSerialSupported()) return [];
    const ports = await navigator.serial.getPorts();
    return ports.filter((p) => p.getInfo().usbVendorId === WCH_USB_VENDOR_ID).map((p) => new BytePort(p));
  }

  static async request(): Promise<BytePort> {
    const port = await navigator.serial.requestPort({ filters: [{ usbVendorId: WCH_USB_VENDOR_ID }] });
    return new BytePort(port);
  }

  get label(): string {
    const info = this.port.getInfo();
    return info.usbVendorId !== undefined
      ? `USB ${info.usbVendorId.toString(16).padStart(4, "0")}:${(info.usbProductId ?? 0).toString(16).padStart(4, "0")}`
      : "serial port";
  }

  async open(baudRate = 115200): Promise<void> {
    await this.port.open({ baudRate, dataBits: 8, stopBits: 1, parity: "none", flowControl: "none" });
    // The vendor updater opens the port with DTR and RTS off.
    await this.port.setSignals({ dataTerminalReady: false, requestToSend: false }).catch(() => undefined);
    if (!this.port.readable || !this.port.writable) throw new Error("the port has no data streams");
    this.reader = this.port.readable.getReader();
    this.writer = this.port.writable.getWriter();
    this.opened = true;
    this.loop = this.readLoop();
  }

  private async readLoop(): Promise<void> {
    try {
      while (this.opened && this.reader) {
        const { value, done } = await this.reader.read();
        if (done) break;
        if (value) {
          for (const b of value) this.queue.push(b);
          this.waiters.splice(0).forEach((wake) => wake());
        }
      }
    } catch {
      /* the reader was cancelled on close */
    }
  }

  /** Forget everything received so far. */
  drain(): void {
    this.queue.length = 0;
  }

  async write(bytes: Uint8Array): Promise<void> {
    if (!this.writer) throw new Error("the port is not open");
    await this.writer.write(bytes);
  }

  /** The next received byte, or null after `timeoutMs`. */
  async readByte(timeoutMs: number): Promise<number | null> {
    const deadline = performance.now() + timeoutMs;
    while (this.queue.length === 0) {
      const left = deadline - performance.now();
      if (left <= 0) return null;
      await new Promise<void>((resolve) => {
        const timer = setTimeout(() => {
          const i = this.waiters.indexOf(wake);
          if (i >= 0) this.waiters.splice(i, 1);
          resolve();
        }, left);
        const wake = () => {
          clearTimeout(timer);
          resolve();
        };
        this.waiters.push(wake);
      });
    }
    return this.queue.shift() ?? null;
  }

  async close(): Promise<void> {
    this.opened = false;
    try {
      await this.reader?.cancel();
    } catch {
      /* ignore */
    }
    this.reader?.releaseLock();
    this.writer?.releaseLock();
    await this.loop;
    try {
      await this.port.close();
    } catch {
      /* already closed */
    }
  }
}
