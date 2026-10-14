# Backlog: ideas for later

Not implemented. First priority is the same features as the original firmware (see
[`DEVIATIONS.md`](DEVIATIONS.md)); these items wait until then.

## Faster display replies to the PC

- **Measured (2026-09-23, `tools/gx_latency.py`, `:n00=$` → `:Rev201`, median over 50):** our
  firmware 22.1 / 23.1 ms (channels …901 / …903), original 26.9 / 29.9 ms.
- **Why ~20 ms:** both firmwares answer these commands from the main loop, whose pass includes the
  original's 13.3 ms key-debounce delay plus screen updates.
- **Done in the nx firmware (`APP=nx`):** its cooperative main loop answers in about 2 ms
  (`build/latency-nx.json`); the classic firmware keeps the original's timing.

## Biofeedback scan speed

Baseline from a serial capture of Spooky2 with the original firmware (two scans, without and with
a load; evaluated with `tools/bfb_bench.py`):

| | Scan 1 (no load) | Scan 2 (with load) |
|---|---|---|
| Baseline at 41 kHz | 26.5 s, 202 readings every 74 ms | 15.9 s, 202 readings every 72 ms |
| Sweep | 129 steps in 16.2 s | 218 steps in 17.9 s |
| Time per step (median) | 116 ms | 74 ms |
| of which generator replies (`:w24`, `:r11`, `:r12`, ~4 ms each) | 12.9 ms | 11.0 ms |
| of which PC / Spooky2 (mostly its "Delay for GX Inputs" of 70 ms) | 99.4 ms | 61.2 ms |
| Readings r11 / r12 | 46210 ± 4.5 / 5781 ± 1.3 | 48877 ± 11 / 7465 ± 2.4 |

- Step +0.025 % per frequency; a full 41 kHz → 1.8 MHz sweep (end point assumed) is ~15,130 steps,
  i.e. 19–29 min per pass at this pace.
- In PC mode the PC talks to the generator boards directly; the display firmware is not in this
  loop, so scan speed should be the same with both firmwares.
- The generator boards do not answer `:w24` / `:r11` / `:r12` outside a Spooky2 session (tested
  with both firmwares). Measurements of the generator side therefore go through Spooky2 captures.

Open items:

1. **Control measurement:** run the same scan in Spooky2 with our firmware, capture it with Device
   Monitoring Studio, compare with `tools/bfb_bench.py` (expected: identical time per step).
2. **Settling time:** repeat captures with smaller "Delay for GX Inputs" values in Spooky2 and
   compare how stable r11/r12 are per step; this shows how fast a scan can go without losing
   measurement quality.
3. **Built-in scan in the firmware:** done on 2026-10-14 as a build switch that is off by default
   (`NX_FEAT_BFB`, [`docs/nx/features.md`](docs/nx/features.md) §9.18; cancelled before, on
   2026-09-24, with the nx Drops 3 and 4). Open: whether the generator answers `:r11` in
   standalone mode, with or without the vendor handshake, and a scan on a unit against Spooky2's.
