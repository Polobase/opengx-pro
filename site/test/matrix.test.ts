/* The comparison list: every row is complete, nothing is listed twice, and the short table of the
 * overview is a part of the complete one. */
import assert from "node:assert/strict";
import { test } from "node:test";
import { glance, groups, rowCount } from "../src/lib/matrix";

test("every row has a feature and both cells", () => {
  for (const g of groups) {
    assert.ok(g.title && g.rows.length, g.id);
    for (const r of g.rows) {
      assert.ok(r.feature.length > 3, g.id);
      for (const c of [r.original, r.ours]) assert.ok(typeof c === "boolean" || (typeof c === "string" && c.length > 0), r.feature);
      assert.notEqual(r.original, "own", r.feature);
    }
  }
});

test("no feature is listed twice", () => {
  const names = groups.flatMap((g) => g.rows.map((r) => r.feature));
  assert.equal(new Set(names).size, names.length);
  assert.equal(names.length, rowCount);
});

test("the short table is part of the complete one, and says what is built in", () => {
  const all = new Set(groups.flatMap((g) => g.rows.map((r) => r.feature)));
  const short = glance.flatMap((g) => g.rows);
  assert.ok(short.length >= 15 && short.length < rowCount);
  for (const r of short) assert.ok(all.has(r.feature));
  const terrain = short.find((r) => r.feature.startsWith("Terrain Protocol"));
  assert.deepEqual([terrain?.original, terrain?.ours], [false, true]);
});
