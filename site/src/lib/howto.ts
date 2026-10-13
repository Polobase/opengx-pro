/**
 * How to use the unit, step by step: which key, and what the screen then shows. The keys and
 * screens are those of docs/guide/keys.md; the pictures are the gallery's (lib/screens.ts).
 */
export interface Step {
  /** Keys to press (lit on the drawing). Several: any of them, or together when `together`. */
  keys: string[]
  together?: boolean
  /** Hold the key instead of a short press. */
  hold?: boolean
  shot: string
  does: string
  see: string
}
export interface Topic {
  id: string
  title: string
  steps: Step[]
}

export const topics: Topic[] = [
  {
    id: 'play',
    title: 'Play a program',
    steps: [
      { keys: ['G1'], shot: 'library', does: 'Press G1', see: 'The program list of generator 1 opens. If G1 already shows a program, press G1 once more.' },
      { keys: ['UP', 'DOWN'], shot: 'library', does: 'Choose with ▲ and ▼', see: 'Your 30 stored programs. The list remembers where you were.' },
      { keys: ['START'], shot: 'dash_ready', does: 'Press Start', see: 'The program is loaded. The screen says READY and shows its first frequency.' },
      { keys: ['START'], shot: 'dash_running', does: 'Press Start again', see: 'It plays. You see the frequency, the step, the time left and the next frequency.' },
      { keys: ['START'], shot: 'dash_paused', does: 'Start pauses', see: 'Paused. Start plays on from where it was.' },
      { keys: ['LOOP'], shot: 'dash_loop', does: 'Press Loop', see: 'Repeat is on: when the program ends, it starts again. Loop once more switches repeat off.' },
      { keys: ['STOP'], shot: 'dash_ready', does: 'Press Stop', see: 'The program stops and goes back to its first step. The outputs are off.' },
      { keys: ['STOP'], hold: true, shot: 'stop_both_ring', does: 'Hold Stop for a second', see: 'Both generators stop. A ring shows while you hold.' },
    ],
  },
  {
    id: 'two',
    title: 'The second generator',
    steps: [
      { keys: ['G2'], shot: 'dash_g2', does: 'Press G2', see: 'Generator 2 comes to the front. Generator 1 stays in view in the strip at the bottom.' },
      { keys: ['G2'], shot: 'library', does: 'Press G2 again', see: 'Its program list opens. Choose and load a program as for G1.' },
      { keys: ['G1'], shot: 'dash_running', does: 'Press G1', see: 'Back to generator 1. Each generator plays its own program, both at the same time.' },
    ],
  },
  {
    id: 'detail',
    title: 'Look into a program',
    steps: [
      { keys: ['LOOP'], hold: true, shot: 'detail_playing', does: 'Hold Loop', see: 'Every step of the program with its frequency. The step that plays is marked.' },
      { keys: ['UP', 'DOWN'], shot: 'detail_scrolled', does: 'Scroll with ▲ and ▼', see: 'Up to 200 steps. Stop goes back.' },
      { keys: ['LOOP'], shot: 'slot_actions', does: 'Press Loop here', see: 'More for this program: play it on both generators, copy it, move it, erase it.' },
      { keys: ['START'], shot: 'slot_pick', does: 'Choose “Copy” and press Start', see: 'Pick the slot the copy goes to. The first empty slot is suggested.' },
    ],
  },
  {
    id: 'menu',
    title: 'Menu and settings',
    steps: [
      { keys: ['DOWN'], shot: 'menu', does: 'Press ▼ on the main screen', see: 'The menu: Programs, Chains, Manual, Timers, Diagnostics, Settings.' },
      { keys: ['G1', 'G2', 'UP', 'DOWN'], shot: 'menu', does: 'Move with G1, G2, ▲ and ▼', see: 'G1 and G2 move along the row, ▲ and ▼ change the row. Stop closes the menu.' },
      { keys: ['START'], shot: 'settings', does: 'Press Start on Settings', see: 'Display, Keys & lock, Motion, About, and “Reset all settings”.' },
      { keys: ['START'], shot: 'settings_display', does: 'Open Display', see: 'Theme, brightness, when the screen dims and switches off. G1 and G2 change a value.' },
      { keys: ['UP'], shot: 'quick', does: 'Or press ▲ on the main screen', see: 'Quick settings: brightness, night mode, key lock, screen off.' },
    ],
  },
  {
    id: 'manual',
    title: 'A frequency by hand',
    steps: [
      { keys: ['DOWN'], shot: 'menu', does: 'Menu, then Manual', see: 'Open the menu with ▼, move to Manual, press Start.' },
      { keys: ['UP', 'DOWN'], shot: 'manual_fixed', does: 'Choose a row with ▲ and ▼', see: 'Frequency, waveform, amplitude, and the generator it plays on.' },
      { keys: ['G1', 'G2'], shot: 'manual_fixed', does: 'Change the value with G1 and G2', see: 'G1 is down, G2 is up. Hold one and it keeps counting.' },
      { keys: ['LOOP'], shot: 'manual_fixed', does: 'Loop moves the digit', see: 'The underlined digit of the frequency is the one G1 and G2 change.' },
      { keys: ['START'], shot: 'manual_fixed', does: 'Press Start', see: 'It plays at once. Changes you make now apply while it plays. Start again stops it.' },
      { keys: ['G1', 'G2'], shot: 'manual_sweep', does: 'Mode: Sweep', see: 'Set start, end, time and shape. The frequency then moves from one to the other.' },
    ],
  },
  {
    id: 'chains',
    title: 'Chains and plans',
    steps: [
      { keys: ['DOWN'], shot: 'chains', does: 'Menu, then Chains', see: 'One list: the built-in programmes first, then Chain 1 to 4 and Plan 1 and 2.' },
      { keys: ['LOOP'], shot: 'chain_edit', does: 'On a chain, press Loop', see: 'The editor. G1 and G2 choose the program, Start adds one, Loop sets how often it repeats.' },
      { keys: ['STOP'], shot: 'chains', does: 'Stop saves and goes back', see: 'The row now says how many programs the chain has.' },
      { keys: ['START'], shot: 'dash_chain', does: 'Press Start on the chain', see: 'The programs play one after the other. The top line shows “Chain 2/3”.' },
      { keys: ['LOOP'], shot: 'plan_edit', does: 'A plan: Loop edits it', see: 'A chain for every day, up to 14 days. Start on the plan plays the next day.' },
    ],
  },
  {
    id: 'terrain',
    title: 'Terrain Protocol',
    steps: [
      { keys: ['DOWN'], shot: 'chains_terrain', does: 'Menu, then Chains', see: 'The Terrain Protocol stands first: Terrain, Terrain-Mercury and Terrain-Nonviolent, each as Remote and as Contact.' },
      { keys: ['G1', 'G2'], shot: 'chains_terrain', does: 'G1 or G2 choose the generator', see: 'The badge at the top right shows where it will play.' },
      { keys: ['START'], shot: 'dash_terrain', does: 'Press Start', see: 'It plays from where it stopped last time. The top line shows the day, here “Day 6/11”.' },
      { keys: ['LOOP'], shot: 'terrain_remote', does: 'Loop on its row', see: 'The Terrain screen: the set, the day, how long it has run. ▲ and ▼ move one set back or on.' },
      { keys: ['START'], shot: 'terrain_contact', does: 'A Contact protocol', see: 'Two sessions a day. The screen says which pads to connect.' },
      { keys: ['START'], shot: 'terrain_alert', does: 'When a set is finished', see: 'The screen wakes and says so. Any key closes the notice.' },
    ],
  },
  {
    id: 'timers',
    title: 'Timers and power cuts',
    steps: [
      { keys: ['DOWN'], shot: 'settings_timers', does: 'Menu, then Timers', see: 'Stop a generator after a set time, switch off when all is finished, start the last programs at power-on.' },
      { keys: ['G1', 'G2'], shot: 'dash_timer', does: 'Set “G1 stops after”', see: 'The top line of the dashboard counts the minutes that are left.' },
      { keys: ['START', 'STOP'], shot: 'resume_offer', does: 'After a power cut', see: 'The unit offers to go on where it was. Start resumes, Stop discards. Without a key it resumes by itself.' },
      { keys: ['STOP'], shot: 'done_card', does: 'All programs finished', see: 'With “Power off when all finished” the unit counts down and switches off. Any key cancels.' },
    ],
  },
  {
    id: 'lock',
    title: 'Lock, screen, power',
    steps: [
      { keys: ['G1', 'G2'], together: true, hold: true, shot: 'lock_card', does: 'Hold G1 and G2 together', see: 'The keys are locked. Any key now only shows this card.' },
      { keys: ['START', 'STOP'], together: true, hold: true, shot: 'lock_unlocking', does: 'Hold Start and Stop together', see: 'A ring fills, then the keys are free again.' },
      { keys: ['POWER'], shot: 'night_view', does: 'Press Power briefly', see: 'The screen goes off, the programs play on. Any key brings it back. If you chose the night view, it shows this instead of a dark screen.' },
      { keys: ['POWER'], hold: true, shot: 'power_ring', does: 'Hold Power', see: 'Both generators stop, then the unit switches off.' },
    ],
  },
  {
    id: 'pc',
    title: 'With Spooky2',
    steps: [
      { keys: [], shot: 'pc_monitor', does: 'Plug in the USB cable', see: 'PC mode. Spooky2 on your computer is in control, as before; the screen shows what it plays.' },
      { keys: ['UP', 'DOWN'], shot: 'pc_monitor_info', does: '▲ and ▼ set the brightness', see: 'Start, Stop and Loop do nothing in PC mode, as on the original. The screen also shows waveform and amplitude.' },
      { keys: [], shot: 'dash_ready', does: 'Unplug the USB cable', see: 'Back to the main screen. Programs that Spooky2 uploaded are in the list.' },
    ],
  },
]
