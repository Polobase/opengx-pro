/**
 * The released versions the page can install. `firmware/index.json` and the update files beside it
 * are put there when the site is built (scripts/collect-firmware.mjs: from the project's GitHub
 * releases, or from a local `make release`). They are served by this site itself, and every file
 * is checked against its SHA-256 again before it is offered for installing.
 */
import { parseImage, type FirmwareImage } from './image'

export interface Release {
  version: string
  /** "beta": before 1.0.0 or a pre-release; else "stable". */
  channel: 'stable' | 'beta'
  date: string
  file: string
  bytes: number
  image_bytes: number
  blocks: number
  sha256: string
  commit: string
  /** The release's page with its notes, when there is one. */
  notes?: string
}

const base = `${import.meta.env.BASE_URL}firmware/`

/** Newest first; an empty list when nothing is published yet. */
export async function loadReleases(): Promise<Release[]> {
  const r = await fetch(`${base}index.json`, { cache: 'no-store' })
  if (r.status === 404) return []
  if (!r.ok) throw new Error(`The list of versions could not be loaded (${r.status}).`)
  return ((await r.json()) as { releases: Release[] }).releases
}

/** The version to offer first: the newest stable one, else the newest. */
export function preferred(releases: Release[]): Release | undefined {
  return releases.find((r) => r.channel === 'stable') ?? releases[0]
}

export async function loadFirmware(release: Release): Promise<FirmwareImage> {
  const r = await fetch(base + release.file, { cache: 'no-store' })
  if (!r.ok) throw new Error(`Version ${release.version} could not be downloaded (${r.status}).`)
  const image = await parseImage(new Uint8Array(await r.arrayBuffer()))
  if (image.sha256 !== release.sha256) {
    throw new Error(`The downloaded file of version ${release.version} does not match its checksum. Reload the page.`)
  }
  return image
}
