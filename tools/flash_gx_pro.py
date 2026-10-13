#!/usr/bin/env python3
"""Flash GX Pro display firmware from macOS (or Linux).

Re-implementation of the Windows-only "FWUpd2.exe" (GX Pro Display Firmware
Upgrade Software v2.01, rev. 2024.01), shipped as GX_Pro_Firmware_v7789.exe.
The protocol was recovered from that tool's .NET IL:

  Image     Raw bytes. The last 4 bytes are a big-endian firmware ID (b"X2Vx");
            its upper 16 bits must match the hardware ID the device reports.
            Those 4 ID bytes are never sent.
  Link      115200 baud, 8N1, no flow control, DTR/RTS off.
  1.        "AR" -> 1 byte   hardware ID, high byte
  2.        "AN" -> 1 byte   hardware ID, low byte
  3.        "AD" -> "D"      start (firmware type 2; type 1 uses "AC")
  4.        For every 2048-byte block i (last block padded with 0xFF):
              "AB" crc_lo crc_hi i last_i <2048 bytes> 00*6      (2060 bytes)
            crc = CRC-16/MODBUS over bytes [4:2054]. The device answers "C"
            within 2 s if the block arrived intact, otherwise it is resent.
            At most 4 resends are tolerated over the whole transfer.

Setup (once, in this folder):
  python3 -m venv .venv && .venv/bin/python -m pip install -r tools/requirements.txt

Usage:
  .venv/bin/python tools/flash_gx_pro.py            # choose the serial port interactively
  .venv/bin/python tools/flash_gx_pro.py -p /dev/cu.usbserial-1410
  .venv/bin/python tools/flash_gx_pro.py --dry-run  # check the image, touch no hardware
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is missing. Set up a local environment in this folder once:\n"
             "  python3 -m venv .venv && .venv/bin/python -m pip install -r tools/requirements.txt\n"
             "then run:  .venv/bin/python tools/flash_gx_pro.py")

DEFAULT_FIRMWARE = Path(__file__).resolve().parent.parent / "original" / "GxDisplayFW.bin"

BAUDRATE = 115200
BLOCK_SIZE = 2048
REPLY_TIMEOUT = 0.5  # handshake replies; FWUpd2 uses 20 ms, USB adapters on macOS can be slower
ACK_TIMEOUT = 2.0  # per-packet "C" acknowledgement, same as FWUpd2
MAX_RESENDS = 4
MAX_BLOCKS = 56   # 0x08004000..0x0801FFFF: the 112 KB application region in 2 KB blocks
# The largest image the bootloader is proven to take on a unit (Makefile FLASH_PROVEN: the 110 KB
# size test, 55 full blocks). FWUpd2 counts the 4 ID bytes too, so such a file is "56 blocks"; a
# file that fills the 56th block with image data has never been tried and is refused.
MAX_PAYLOAD = 112640

# Firmware ID (last 4 bytes of the image) -> firmware type, from FWUpd2's Form1_Load.
# FWUpd2 refuses to flash any image whose ID is not in this table.
KNOWN_FIRMWARE_IDS = {
    b"M0Vx": 2, b"M1Vx": 2, b"M2Vx": 2, b"M3Vx": 2,
    b"X0Vx": 2, b"X1Vx": 2, b"X2Vx": 2, b"X3Vx": 2,
    b"X1\x12\x34": 1, b"X3\x12\x34": 1,
}
START_COMMAND = {1: b"AC", 2: b"AD"}


class FlashError(Exception):
    pass


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def build_packets(image: bytes) -> list[bytes]:
    payload = image[:-4]
    count = -(-len(image) // BLOCK_SIZE)  # FWUpd2 counts the ID bytes too
    if count > MAX_BLOCKS:
        raise FlashError(f"image needs {count} blocks of 2 KB, the application region holds "
                         f"{MAX_BLOCKS} (112 KB behind the bootloader)")
    if len(payload) > MAX_PAYLOAD:
        raise FlashError(f"image is {len(payload)} bytes, more than the {MAX_PAYLOAD} the bootloader "
                         f"is proven to take")
    packets = []
    for i in range(count):
        block = payload[i * BLOCK_SIZE:(i + 1) * BLOCK_SIZE].ljust(BLOCK_SIZE, b"\xff")
        body = bytes([i, count - 1]) + block
        crc = crc16_modbus(body)
        packets.append(b"AB" + crc.to_bytes(2, "little") + body + bytes(6))
    return packets


def load_firmware(path: Path) -> tuple[bytes, bytes, int]:
    image = path.read_bytes()
    if len(image) < 8:
        raise FlashError(f"{path}: file is too small to be a firmware image")
    fw_id = image[-4:]
    fw_type = KNOWN_FIRMWARE_IDS.get(fw_id)
    if fw_type is None:
        raise FlashError(f"{path.name}: unknown firmware ID {fw_id!r}; FWUpd2 would refuse this file")
    return image, fw_id, fw_type


def open_port(port: str) -> serial.Serial:
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = BAUDRATE
    ser.dtr = False  # match .NET SerialPort defaults
    ser.rts = False
    ser.open()
    return ser


def request(ser: serial.Serial, command: bytes) -> int:
    """Send a 2-byte command and return the 1-byte reply (0 if none)."""
    ser.reset_input_buffer()
    ser.write(command)
    ser.timeout = REPLY_TIMEOUT
    reply = ser.read(1)
    return reply[0] if reply else 0


def read_hardware_id(ser: serial.Serial) -> int:
    time.sleep(0.02)
    return (request(ser, b"AR") << 8) | request(ser, b"AN")


def describe_id(hw: int) -> str:
    raw = hw.to_bytes(2, "big")
    return raw.decode() if all(0x20 < b < 0x7F for b in raw) else f"{hw:04X}"


def probe(port: str) -> int | None:
    """Ask a port for the hardware ID (sends only "AR"/"AN"); None if it can't be opened."""
    try:
        with open_port(port) as ser:
            return read_hardware_id(ser)
    except (serial.SerialException, OSError):
        return None


def pick_port(requested: str | None, expected_hw: int) -> str:
    if requested:
        return requested
    ports = [
        p for p in list_ports.comports()
        if "Bluetooth" not in p.device and "debug-console" not in p.device
        and (sys.platform != "darwin" or p.device.startswith("/dev/cu."))
    ]
    if not ports:
        raise FlashError("no serial port found. Is the GX Pro connected by USB? "
                         "Some adapters (e.g. CH340) need a driver on macOS.")

    # Only USB ports are probed: opening a Bluetooth port can hang while macOS tries to connect.
    usb_ports = [p for p in ports if p.vid is not None]
    if usb_ports:
        print("Looking for the GX Pro:")
    for p in usb_ports:
        hw = probe(p.device)
        if hw is None:
            status = "cannot open"
        elif hw == 0:
            status = "no reply"
        elif hw == expected_hw:
            print(f"  {p.device}: GX Pro, hardware ID {describe_id(hw)}")
            return p.device
        else:
            status = f"replied {describe_id(hw)}, firmware needs {describe_id(expected_hw)}"
        print(f"  {p.device}: {status}")

    print(f"\nNo port answered with hardware ID {describe_id(expected_hw)}. "
          "Is the GX Pro in programming mode (Stop, then Power)?\n"
          "Pick a port only if you are sure it is the GX Pro, otherwise press Ctrl+C.")
    for n, p in enumerate(ports, 1):
        print(f"  {n}) {p.device}  {p.description}")
    choice = input(f"Which port is the GX Pro [1-{len(ports)}]? ").strip()
    if not choice.isdigit() or not 1 <= int(choice) <= len(ports):
        raise FlashError("no valid port selected")
    return ports[int(choice) - 1].device


def wait_for_ack(ser: serial.Serial) -> bool:
    deadline = time.monotonic() + ACK_TIMEOUT
    while (left := deadline - time.monotonic()) > 0:
        ser.timeout = left
        if ser.read(1) == b"C":
            return True
    return False


def show_progress(done: int, total: int) -> None:
    width = 30
    filled = width * done // total
    print(f"\r  [{'#' * filled}{'-' * (width - filled)}] {done}/{total} packets", end="", flush=True)


def flash(ser: serial.Serial, packets: list[bytes], fw_id: bytes, fw_type: int) -> None:
    expected_hw = int.from_bytes(fw_id[:2], "big")
    hw = read_hardware_id(ser)
    if hw == expected_hw:
        print(f"Firmware matches your hardware (ID {describe_id(hw)}).")
    elif hw == 0:
        print("Warning: no hardware ID reply. Continuing, as FWUpd2 does.")
    else:
        raise FlashError(f"firmware does not match your hardware (device ID {hw:04X}, "
                         f"firmware expects {expected_hw:04X}). Wrong port?")

    if request(ser, START_COMMAND[fw_type]) != ord("D"):
        raise FlashError("device did not accept the start command. "
                         "Is the GX Pro in programming mode, and is this the right port?")

    print(f"Sending {len(packets)} packets:")
    resends = 0
    i = 0
    show_progress(0, len(packets))
    while i < len(packets):
        ser.reset_input_buffer()
        ser.write(packets[i])
        ser.flush()
        if wait_for_ack(ser):
            i += 1
            show_progress(i, len(packets))
            continue
        print(f"\n  Packet {i} of {len(packets) - 1}: CRC check ERROR", end="")
        if resends >= MAX_RESENDS:
            print()
            raise FlashError("too many CRC errors, giving up. Put the GX Pro back into "
                             "programming mode and run this script again.")
        resends += 1
        print(f", resending ({resends}/{MAX_RESENDS})")
    print()


def main() -> int:
    parser = argparse.ArgumentParser(description="Flash GX Pro display firmware over USB serial.")
    parser.add_argument("-p", "--port", help="serial port, e.g. /dev/cu.usbserial-1410 (asks if omitted)")
    parser.add_argument("-f", "--firmware", type=Path, default=DEFAULT_FIRMWARE,
                        help=f"firmware image (default: {DEFAULT_FIRMWARE.name})")
    parser.add_argument("-y", "--yes", action="store_true", help="start without asking for confirmation")
    parser.add_argument("--dry-run", action="store_true",
                        help="validate the image and build the packets without opening a port")
    args = parser.parse_args()

    try:
        image, fw_id, fw_type = load_firmware(args.firmware)
        packets = build_packets(image)
        print(f"Firmware:  {args.firmware.name}, {len(image)} bytes, ID {fw_id.decode(errors='replace')!r}, "
              f"type {fw_type}, {len(packets)} packets")
        if args.dry_run:
            print("Dry run: image is valid, no port opened.")
            return 0

        if not args.yes:
            print("\n1. Power off your GX Pro.\n"
                  "2. Put it into programming mode by pressing Stop, then Power.\n"
                  "3. Make sure it is connected by USB.")
            input("Press Enter to start flashing (Ctrl+C to cancel)... ")

        port = pick_port(args.port, int.from_bytes(fw_id[:2], "big"))
        with open_port(port) as ser:
            flash(ser, packets, fw_id, fw_type)
        print("Success! Please restart your GX Pro to enable the firmware.")
        return 0
    except FlashError as e:
        print(f"\nERROR: {e}", file=sys.stderr)
        return 1
    except serial.SerialException as e:
        print(f"\nERROR: serial port: {e}", file=sys.stderr)
        return 1
    except (KeyboardInterrupt, EOFError):
        print("\nCancelled. If packets were already sent, the display firmware is incomplete: "
              "put the GX Pro back into programming mode and run this script again.", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
