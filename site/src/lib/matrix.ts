/**
 * The original firmware (v7789) and OpenGX Pro 0.9.0, feature by feature, in plain words. The
 * technical record with the place in the code for each difference is docs/guide/compare.md,
 * DEVIATIONS.md and docs/parity.md; this list must not say more than they do.
 *
 * A cell is `true` (yes), `false` (no, or not visible from outside), 'own' (only in a firmware you
 * build yourself, see docs/guide/build.md) or a short text.
 */
export type Cell = boolean | 'own' | string

export interface Row {
  feature: string
  /** A second line under the feature, in smaller type. */
  note?: string
  original: Cell
  ours: Cell
  /** Part of the short "at a glance" table on the overview. */
  glance?: boolean
}

export interface Group {
  id: string
  title: string
  intro?: string
  rows: Row[]
}

export const groups: Group[] = [
  {
    id: 'same',
    title: 'Works as before',
    rows: [
      { feature: 'Spooky2 on the PC controls the unit as before', note: 'Same commands, same replies, in the same order.', original: true, ours: true, glance: true },
      { feature: 'Your 30 stored programs and their names', note: 'They stay in the unit when you change the firmware.', original: true, ours: true, glance: true },
      { feature: 'The calibration of your unit', note: 'It stays as it is.', original: true, ours: true },
      { feature: 'Choose a program with G1 / G2, UP / DOWN and START', original: true, ours: true },
      { feature: 'Run, pause, stop and repeat a program', original: true, ours: true },
      { feature: 'Two generators, each with its own program', original: true, ours: true },
      { feature: 'Calibration menu (G1 + G2 while switching on)', original: true, ours: true },
      { feature: "Update through the unit's own update mode", note: 'This is also the way back to the original.', original: true, ours: true, glance: true },
      { feature: 'Switch off with POWER held', original: true, ours: true },
    ],
  },
  {
    id: 'screen',
    title: 'On the screen',
    rows: [
      { feature: 'The frequency that is playing', original: true, ours: true },
      { feature: 'The frequency also when a program is ready or paused', original: false, ours: true },
      { feature: 'Step n of N, time per step and time left', original: false, ours: true, glance: true },
      { feature: 'The next frequency', original: false, ours: true },
      { feature: 'Amplitude and waveform of the program', original: false, ours: true },
      { feature: 'Waveform view', original: false, ours: true, glance: true },
      { feature: 'Both generators at a glance', note: 'One large, the other in a strip at the bottom.', original: false, ours: true },
      { feature: 'Program detail with every step', original: false, ours: true, glance: true },
      { feature: '“Finished” when a program has ended', original: false, ours: true },
      { feature: 'Themes: dark, light, high contrast', original: false, ours: true, glance: true },
      { feature: 'Brightness setting', original: false, ours: true },
      { feature: 'Screen dims and switches off when idle', original: false, ours: true },
      { feature: 'Night view', note: 'Both generators at a glance on a nearly dark screen.', original: false, ours: true },
      { feature: 'Long program names scroll', original: true, ours: true },
    ],
  },
  {
    id: 'standalone',
    title: 'On its own, without a PC',
    rows: [
      { feature: 'Play the stored programs', original: true, ours: true },
      { feature: 'A menu on the unit', original: false, ours: true },
      { feature: 'Manual generator: a frequency set by hand', note: '0.01 Hz to 40 MHz, nine waveforms, 0 to 20 V.', original: false, ours: true, glance: true },
      { feature: 'Sweeps', note: 'Linear or logarithmic; once, again, or back and forth.', original: false, ours: true, glance: true },
      { feature: 'Chains: several programs one after the other', original: false, ours: true, glance: true },
      { feature: 'Plans: a chain for every day', note: 'Up to 14 days. The unit has no clock: you start the next day with START.', original: false, ours: true, glance: true },
      { feature: 'Timer: stop a generator after a set time', original: false, ours: true, glance: true },
      { feature: 'Switch off when all programs have finished', original: false, ours: true },
      { feature: 'Start the last programs at power-on', original: false, ours: true },
      { feature: 'Terrain Protocol built in', note: 'Terrain, Terrain-Mercury and Terrain-Nonviolent, as Remote and as Contact sets. The unit counts the days and goes on where it stopped.', original: false, ours: true, glance: true },
      { feature: '“Radionics (Boost) Manifestation” built in', note: 'Spooky2’s preset, with its sweeps, without a PC. Programs uploaded from Spooky2 cannot sweep; this one can.', original: false, ours: true, glance: true },
      { feature: 'Resume after a power cut or PC mode', note: 'The unit offers to continue where it stopped, also after Spooky2 had taken over.', original: false, ours: true, glance: true },
      { feature: 'Copy, move or erase a program on the unit', original: false, ours: true },
      { feature: 'Play one program on both generators', original: false, ours: true },
      { feature: 'Key lock', original: false, ours: true, glance: true },
      { feature: 'Diagnostics and key test', original: false, ours: true, glance: true },
      { feature: 'Settings that are kept after switching off', original: false, ours: true },
    ],
  },
  {
    id: 'options',
    title: 'Switched off to save space',
    intro: 'The unit’s memory is small. These two exist and work, but are not in the download: they can be switched on in a firmware you build yourself.',
    rows: [
      { feature: 'History: every run, with totals', original: false, ours: 'own' },
      { feature: 'Extra-large frequency digits', original: false, ours: 'own' },
    ],
  },
  {
    id: 'pc',
    title: 'With Spooky2 (PC mode)',
    rows: [
      { feature: 'Reply time of the display to the PC (median)', note: 'Measured on one unit; see Measurements in the guide.', original: 'about 20 ms', ours: 'about 2 ms', glance: true },
      { feature: 'PC screen: program names and the four frequencies', original: true, ours: true },
      { feature: 'PC screen: waveform, amplitude and gating', original: false, ours: true },
      { feature: 'PC screen: the frequency at the output', note: 'For presets whose wave table holds several cycles (“396.00 Hz ×11”).', original: false, ours: true },
      { feature: 'Brightness with UP / DOWN in PC mode', original: false, ours: true },
      { feature: 'USB symbol shows that Spooky2 has found the generator', note: 'The original changes its colour; OpenGX Pro has one symbol.', original: true, ours: false },
    ],
  },
  {
    id: 'reliable',
    title: 'Timing and reliability',
    rows: [
      { feature: 'Length of a step', original: '1 % longer than set', ours: 'exactly as set' },
      { feature: 'Both generators served at the same time', original: false, ours: true, glance: true },
      { feature: 'Warning when a generator stops answering', original: false, ours: true, glance: true },
      { feature: 'Restarts by itself if the firmware hangs', original: false, ours: true, glance: true },
      { feature: 'Recovers after a fault', note: 'After three faults in a row it switches off instead of restarting again.', original: false, ours: true },
      { feature: 'Settings survive a power cut while they are saved', original: false, ours: true, glance: true },
      { feature: 'Start screen', note: 'The generators get their first command after about 4.4 s with both.', original: 'about 4 s', ours: '1.5 s, any key skips it' },
    ],
  },
  {
    id: 'keys',
    title: 'Keys that behave differently',
    intro: 'Settings → Keys & lock → “Original key behaviour” gives POWER, START and LOOP back as they were.',
    rows: [
      { feature: 'POWER pressed briefly', original: 'stops both generators', ours: 'screen off and on' },
      { feature: 'START', original: 'play and pause; repeat is cleared', ours: 'play and pause; repeat is kept' },
      { feature: 'LOOP while a program runs', original: 'pauses, with repeat on', ours: 'switches repeat on or off' },
      { feature: 'STOP held for a second', original: false, ours: 'stops both generators' },
      { feature: 'DOWN and UP on the main screen', original: false, ours: 'menu and quick settings' },
      { feature: 'Keys in PC mode', original: 'POWER only', ours: 'POWER, and UP / DOWN for brightness' },
    ],
  },
  {
    id: 'not',
    title: 'Not in OpenGX Pro',
    rows: [
      { feature: 'Spooky2’s artwork on the start and goodbye screens', note: 'OpenGX Pro draws its own.', original: true, ours: false },
      { feature: 'Special characters in program names', note: 'OpenGX Pro shows letters, digits and punctuation; other characters appear as “?”.', original: true, ours: false },
      { feature: 'Check tied to the device ID in the display firmware', note: 'The generator boards keep their own check; nothing there is changed.', original: true, ours: false },
    ],
  },
  {
    id: 'open',
    title: 'Open',
    rows: [
      { feature: 'Source code public', original: false, ours: true },
      { feature: 'Every difference to the original written down', original: false, ours: true },
    ],
  },
]

/** The short table of the overview: the rows marked `glance`, in their groups. */
export const glance: Group[] = groups
  .map((g) => ({ ...g, intro: undefined, rows: g.rows.filter((r) => r.glance) }))
  .filter((g) => g.rows.length)

export const rowCount = groups.reduce((n, g) => n + g.rows.length, 0)
