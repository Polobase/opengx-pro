/**
 * Screens of the firmware for the site. They are the repository's gallery baselines
 * (tests/gallery/nx): drawn by the firmware's own screen code from sample data, 320 x 240 pixels.
 */
const files = import.meta.glob<string>('../../../tests/gallery/nx/*.png', { eager: true, query: '?url', import: 'default' })
const byName: Record<string, string> = {}
for (const [path, url] of Object.entries(files)) byName[/([\w-]+)\.png$/.exec(path)![1]!] = url

export function screen(name: string): string {
  const url = byName[name]
  if (!url) throw new Error(`no such screen: ${name}`)
  return url
}

export interface Shot {
  name: string
  caption: string
}
export interface ShotGroup {
  id: string
  title: string
  /** Only in a firmware you build yourself. */
  own?: boolean
  shots: Shot[]
}

/** Every screen, grouped as a user meets them. */
export const gallery: ShotGroup[] = [
  {
    id: 'start',
    title: 'Start and stop',
    shots: [
      { name: 'splash_1100', caption: 'Start screen' },
      { name: 'key_guide', caption: 'Key guide, first start' },
      { name: 'dash_starting', caption: 'Generators starting' },
      { name: 'resume_offer', caption: 'Resume after a power cut' },
      { name: 'auto_offer', caption: 'Start the last programs' },
      { name: 'done_card', caption: 'All finished: power off' },
      { name: 'power_ring', caption: 'POWER held: switch off' },
      { name: 'goodbye', caption: 'Goodbye' },
    ],
  },
  {
    id: 'dashboard',
    title: 'Dashboard',
    shots: [
      { name: 'dash_idle', caption: 'No program loaded' },
      { name: 'dash_ready', caption: 'Ready' },
      { name: 'dash_running', caption: 'Running' },
      { name: 'dash_paused', caption: 'Paused' },
      { name: 'dash_loop', caption: 'Repeat on' },
      { name: 'dash_loop_passes', caption: 'Repeat, with the pass count' },
      { name: 'dash_finished', caption: 'Program complete' },
      { name: 'dash_g2', caption: 'G2 in focus' },
      { name: 'dash_strip_none', caption: 'Other generator empty' },
      { name: 'dash_wave_unknown', caption: 'Stored waveform table' },
      { name: 'dash_error', caption: 'Generator not responding' },
      { name: 'dash_timer', caption: 'Stop-after timer' },
      { name: 'dash_chain', caption: 'Chain running' },
      { name: 'stop_both_ring', caption: 'STOP held: stop both' },
    ],
  },
  {
    id: 'programs',
    title: 'Programs',
    shots: [
      { name: 'library', caption: 'Program list' },
      { name: 'detail', caption: 'Program detail' },
      { name: 'detail_playing', caption: 'Playing step marked' },
      { name: 'slot_actions', caption: 'Slot actions' },
      { name: 'slot_pick', caption: 'Copy to another slot' },
    ],
  },
  {
    id: 'features',
    title: 'Menu and features',
    shots: [
      { name: 'menu', caption: 'Menu' },
      { name: 'quick', caption: 'Quick settings' },
      { name: 'manual_fixed', caption: 'Manual: fixed frequency' },
      { name: 'manual_sweep', caption: 'Manual: sweep' },
      { name: 'chains', caption: 'Chains: built-in rows first' },
      { name: 'chain_edit', caption: 'Chain editor' },
      { name: 'plan_edit', caption: 'Plan editor' },
      { name: 'settings_timers', caption: 'Timers' },
      { name: 'diagnostics', caption: 'Diagnostics' },
      { name: 'key_test', caption: 'Key test' },
    ],
  },
  {
    id: 'pc',
    title: 'With Spooky2',
    shots: [
      { name: 'pc_monitor', caption: 'PC monitor' },
      { name: 'pc_monitor_info', caption: 'Waveforms and amplitudes' },
      { name: 'pc_monitor_sync', caption: 'Out 2 follows Out 1' },
      { name: 'pc_monitor_wcm', caption: 'Output frequency ×11' },
    ],
  },
  {
    id: 'settings',
    title: 'Settings and looks',
    shots: [
      { name: 'settings', caption: 'Settings' },
      { name: 'settings_display', caption: 'Display' },
      { name: 'settings_keys', caption: 'Keys & lock' },
      { name: 'settings_motion', caption: 'Motion' },
      { name: 'settings_about', caption: 'About' },
      { name: 'support', caption: 'Support the project' },
      { name: 'confirm_reset', caption: 'Reset, with confirmation' },
      { name: 'lock_card', caption: 'Keys locked' },
      { name: 'night_view', caption: 'Night view' },
      { name: 'dash_running_light', caption: 'Light theme' },
      { name: 'dash_running_hc', caption: 'High contrast' },
      { name: 'calibration', caption: 'Calibration menu' },
    ],
  },
  {
    id: 'terrain',
    title: 'Terrain Protocol and the built-in preset',
    shots: [
      { name: 'chains_terrain', caption: 'Terrain in the list' },
      { name: 'terrain_remote', caption: 'Terrain: Remote sets' },
      { name: 'terrain_contact', caption: 'Terrain: Contact, day 3 of 11' },
      { name: 'dash_terrain', caption: 'Terrain running, day 6/11' },
      { name: 'terrain_alert', caption: 'Terrain: set finished' },
      { name: 'chains_preset', caption: 'A built-in preset' },
      { name: 'dash_preset', caption: 'Preset with a sweep' },
    ],
  },
]

export const shotCount = gallery.reduce((n, g) => n + g.shots.length, 0)
