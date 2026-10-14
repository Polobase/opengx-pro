# Reply time of the display to the PC

In PC mode the PC sends the display short queries and waits for the replies. This page gives the
time the display takes to answer, with the original firmware and with the new one, how it was
measured and how you can repeat it.

**The numbers are from 2026-09-25, measured with a development build of the new firmware, not
with a release file. The measurement will be repeated on the current release and this page updated.**

## Method

- **The query**: the version query `:n00=$`. The display answers `:Rev201`. Both firmwares give
  the same answer.
- **How many**: 1,000 requests on each of the two USB channels of the unit.
- **Spacing**: a random gap of 0 to 20 ms between two requests, so that the requests do not fall
  into step with the main loop of the firmware.
- **Speed**: 115,200 baud, the speed the unit uses.
- **What is timed**: from sending the line on the computer to receiving the reply line on the
  computer. The time includes the USB adapter of the unit and the computer. Both are the same for
  the two firmwares, so the difference is the firmware.
- **Setup**: one unit and one computer. The original firmware v7789 and the new firmware were
  flashed onto the same unit, one after the other.

## Result

Where two values are given, they are the two USB channels.

| | New firmware (development build) | Original firmware v7789 |
|---|---|---|
| Median (p50) | 2.05 ms | 19.3–20.6 ms |
| 95 of 100 replies within (p95) | 2.27–2.28 ms | 31.2 ms |
| 99 of 100 replies within (p99) | 2.60–2.96 ms | 32.5–32.6 ms |
| Slowest of 1,000 | 7.9–8.2 ms | 33.0–60.9 ms |

The median reply of the new firmware comes about ten times sooner.

## Why

The original firmware answers a query at the end of a pass of its main loop. A pass contains a
fixed wait of 13.3 ms for the key scan, and screen updates. The new firmware reads the keys in a
timer interrupt, draws the screen in short slices and answers a line as soon as its main loop
sees it.

## What does not change

- **Commands for the generator boards** take the same time with both firmwares: about 2.4 ms for
  a frequency command (`:w24`) and 3.1 to 3.2 ms for the two read commands (`:r11`, `:r12`). In
  PC mode the PC talks to the generator boards directly; the display only listens.
- **Two queries sent in one write**, without waiting for the first reply: 50 % of the queries
  were answered, with both firmwares.
- **What Spooky2 does with the time** was not measured. Its own waits and the replies of the
  generator boards are the same as before.

## Repeat it

The tool is [`tools/latency_bench.py`](../../tools/latency_bench.py). It needs Python with
pyserial; the project's `.venv` has it ([Build from source](./build.md)). Connect the unit by
USB, switched on. Run the tool once per firmware, on the same unit, the same USB port and the
same computer.

| Step | Command |
|---|---|
| With the original firmware installed | `.venv/bin/python tools/latency_bench.py run --firmware original-v7789` |
| With OpenGX Pro installed | `.venv/bin/python tools/latency_bench.py run --firmware opengx-pro-0.9.1 --image build/release/opengx-pro-0.9.1.bin` |
| Both runs side by side | `.venv/bin/python tools/latency_bench.py summarize build/latency/*.csv` |
| Try the tool without a unit | `.venv/bin/python tools/latency_bench.py selftest` |

- `--firmware` is a label you give. It is required, because the unit cannot say which firmware
  it runs: both answer `:Rev201`. `--image` adds the SHA-256 of the file you flashed to every
  row.
- Each run writes every single measurement to a CSV file in `build/latency/`, with a file of the
  run's settings next to it. `summarize` prints p50, p95 and p99 per firmware, port and command
  and writes `build/latency_summary.csv`.
- `-n` sets the number of requests per command (default 1,000), `--gap-ms` the largest gap
  (default 20), `--seed` the seed of the random gaps. `--burst 2` adds the test with two queries
  in one write.
- The tool also times the three generator commands. The generator boards answer them only inside
  a session of the PC software, which the tool does not open. Without one it skips these rows
  after three timeouts in a row. The outputs stay off and nothing is stored on the unit.
