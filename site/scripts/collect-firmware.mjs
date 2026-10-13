#!/usr/bin/env node
/**
 * Put the released update files into public/firmware/ with the list the page reads (index.json).
 *
 *   node scripts/collect-firmware.mjs --github Polobase/opengx-pro   every published release (needs `gh`)
 *   node scripts/collect-firmware.mjs --local ../build/release       the image of a local `make release`
 *   node scripts/collect-firmware.mjs --none                         an empty list
 *
 * A release is taken when it carries `opengx-pro-<version>.bin` and the `manifest.json` that
 * tools/release_check.py wrote. Every file is checked here again: the display board's firmware ID,
 * the size the bootloader is proven to take, and the SHA-256 of the manifest. A file that fails is
 * an error, not a silent skip. A browser cannot download release files from github.com itself
 * (no cross-origin access), so the site serves its own copies.
 */
import { createHash } from 'node:crypto'
import { execFileSync } from 'node:child_process'
import { cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))
const out = resolve(process.env.FIRMWARE_OUT ?? join(here, '../public/firmware')) // FIRMWARE_OUT: the tests
const MAX_PAYLOAD = 112640 // FLASH_PROVEN
const ID = 'X2Vx'

function check(dir, where) {
  const manifest = JSON.parse(readFileSync(join(dir, 'manifest.json'), 'utf8'))
  const fail = (why) => {
    throw new Error(`${where}: ${manifest.file}: ${why}`)
  }
  if (!/^opengx-pro-[\w.-]+\.bin$/.test(String(manifest.file))) fail('unexpected file name')
  const bytes = readFileSync(join(dir, manifest.file))
  if (bytes.subarray(bytes.length - 4).toString('latin1') !== ID) fail(`firmware ID is not ${ID}`)
  if ((bytes.length - 4) % 16) fail('length does not fit an update file')
  if (bytes.length - 4 > MAX_PAYLOAD) fail(`${bytes.length - 4} bytes, more than the proven ${MAX_PAYLOAD}`)
  if (bytes.length !== manifest.bytes) fail('size differs from the manifest')
  if (createHash('sha256').update(bytes).digest('hex') !== manifest.sha256) fail('SHA-256 differs from the manifest')
  if (!['stable', 'beta'].includes(manifest.channel)) fail('unknown channel')
  return { manifest, bytes }
}

/** 1.2.0 > 1.2.0-rc1 > 1.1.9: numbers by value, a version without a suffix after its pre-releases. */
function newerFirst(a, b) {
  const split = (v) => v.split('-')
  const [na, sa] = split(a.version)
  const [nb, sb] = split(b.version)
  const pa = na.split('.').map(Number)
  const pb = nb.split('.').map(Number)
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
    if ((pa[i] ?? 0) !== (pb[i] ?? 0)) return (pb[i] ?? 0) - (pa[i] ?? 0)
  }
  if (!sa !== !sb) return sa ? 1 : -1
  return (sb ?? '').localeCompare(sa ?? '', 'en', { numeric: true })
}

const [mode, arg] = process.argv.slice(2)
const releases = []
rmSync(out, { recursive: true, force: true })
mkdirSync(out, { recursive: true })

if (mode === '--local') {
  const dir = resolve(arg ?? join(here, '../../build/release'))
  if (!existsSync(join(dir, 'manifest.json'))) throw new Error(`${dir}: no manifest.json (run \`make release\` first)`)
  const { manifest } = check(dir, dir)
  cpSync(join(dir, manifest.file), join(out, manifest.file))
  releases.push(manifest)
} else if (mode === '--github') {
  if (!arg) throw new Error('--github needs owner/repository')
  const gh = (...a) => execFileSync('gh', a, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'inherit'] })
  const list = JSON.parse(gh('release', 'list', '--repo', arg, '--exclude-drafts', '--limit', '200', '--json', 'tagName'))
  const tmp = mkdtempSync(join(tmpdir(), 'opengx-releases-'))
  for (const { tagName } of list) {
    const dir = join(tmp, tagName)
    try {
      gh('release', 'download', tagName, '--repo', arg, '--dir', dir, '--pattern', 'opengx-pro-*.bin', '--pattern', 'manifest.json')
    } catch {
      console.warn(`${tagName}: no update file and manifest, left out`)
      continue
    }
    const { manifest } = check(dir, tagName)
    if (tagName !== `v${manifest.version}`) throw new Error(`${tagName}: its manifest says version ${manifest.version}`)
    cpSync(join(dir, manifest.file), join(out, manifest.file))
    releases.push({ ...manifest, notes: `https://github.com/${arg}/releases/tag/${tagName}` })
  }
  rmSync(tmp, { recursive: true, force: true })
} else if (mode !== '--none') {
  throw new Error('usage: collect-firmware.mjs --github owner/repo | --local [dir] | --none')
}

releases.sort(newerFirst)
const index = { source: mode === '--github' ? `github:${arg}` : mode.slice(2), releases }
writeFileSync(join(out, 'index.json'), JSON.stringify(index, null, 1) + '\n')
console.log(releases.length
  ? releases.map((r) => `${r.version} (${r.channel}): ${r.file}, ${r.bytes} bytes, sha256 ${r.sha256.slice(0, 16)}…`).join('\n')
  : 'no release: the page will say that nothing is published yet')
