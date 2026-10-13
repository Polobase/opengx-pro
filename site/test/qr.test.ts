/* The support QR code, read back with an independent decoder: from the firmware's rendered page
 * (the repository's gallery baseline) and from the picture this site shows. Both must say the
 * address of assets/nx/qr.json, and that must be the address the page links to. */
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import jsQR from "jsqr";
import { PNG } from "pngjs";
import { COFFEE_URL } from "../src/lib/links";

const root = new URL("../..", import.meta.url);
const wanted: string = JSON.parse(readFileSync(new URL("assets/nx/qr.json", root), "utf8")).codes
  .find((c: { id: string }) => c.id === "support").text;

function decode(path: string): string | undefined {
  const png = PNG.sync.read(readFileSync(new URL(path, root)));
  return jsQR(new Uint8ClampedArray(png.data), png.width, png.height)?.data;
}

test("the address is the one the site links to", () => {
  assert.equal(wanted, COFFEE_URL);
});

test("the firmware's support page shows a code that reads as the address", () => {
  assert.equal(decode("tests/gallery/nx/support.png"), wanted);
});

test("the site's picture of the code reads as the address", () => {
  assert.equal(decode("site/src/assets/qr-support.png"), wanted);
});
