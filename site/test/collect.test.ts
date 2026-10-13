/* scripts/collect-firmware.mjs: what goes into the list of versions, and what stops the build. */
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { updateFile } from "./helpers";

const script = fileURLToPath(new URL("../scripts/collect-firmware.mjs", import.meta.url));

function release(payload: number, change: (m: Record<string, unknown>) => void = () => {}, id = "X2Vx") {
  const dir = mkdtempSync(join(tmpdir(), "opengx-test-"));
  const bytes = updateFile(payload, id);
  const manifest: Record<string, unknown> = {
    name: "OpenGX Pro", version: "0.9.0", channel: "beta", file: "opengx-pro-0.9.0.bin", bytes: bytes.length,
    image_bytes: payload, blocks: Math.ceil(payload / 2048), sha256: createHash("sha256").update(bytes).digest("hex"),
    commit: "test", date: "2026-10-13",
  };
  change(manifest);
  writeFileSync(join(dir, String(manifest.file)), bytes);
  writeFileSync(join(dir, "manifest.json"), JSON.stringify(manifest));
  return dir;
}

function collect(...args: string[]) {
  const out = mkdtempSync(join(tmpdir(), "opengx-out-"));
  execFileSync(process.execPath, [script, ...args], { env: { ...process.env, FIRMWARE_OUT: out }, stdio: "pipe" });
  return { out, index: JSON.parse(readFileSync(join(out, "index.json"), "utf8")) };
}

test("a local release build is listed with its file", () => {
  const { out, index } = collect("--local", release(4096));
  assert.equal(index.releases.length, 1);
  assert.equal(index.releases[0].version, "0.9.0");
  assert.equal(readFileSync(join(out, "opengx-pro-0.9.0.bin")).length, 4100);
});

test("no release gives an empty list", () => {
  assert.deepEqual(collect("--none").index.releases, []);
});

test("a file that is not what its manifest says stops the build", () => {
  assert.throws(() => collect("--local", release(4096, (m) => (m.sha256 = "00".repeat(32)))), /SHA-256 differs/);
  assert.throws(() => collect("--local", release(4096, (m) => (m.bytes = 1))), /size differs/);
  assert.throws(() => collect("--local", release(4096, () => {}, "X1Vx")), /firmware ID is not X2Vx/);
  assert.throws(() => collect("--local", release(112_656)), /more than the proven 112640/);
  assert.throws(() => collect("--local", release(4096, (m) => (m.file = "../evil.bin"))), /unexpected file name/);
});
