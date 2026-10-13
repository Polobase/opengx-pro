<script setup lang="ts">
import { computed, onBeforeUnmount, ref } from 'vue'
import { DEMO_PROGRAM, DisplayLink, percentile, type StoredProgram } from '../lib/display'
import PortHint from './PortHint.vue'
import TourCard from './TourCard.vue'

/** The original firmware v7789 on one unit (1,000 requests per channel, 25 September 2026; docs/guide/measurements.md). */
const ORIGINAL_FW_MEDIAN = '19.3–20.6 ms'

const link = ref<DisplayLink | null>(null)
const revision = ref<string | null>(null)
const busy = ref('')
const error = ref('')

const times = ref<number[]>([])
const measured = ref(0)
const COUNT = 200

const title = ref('Hello from the browser')
const titleSent = ref(false)

const names = ref<Array<{ slot: number; name: string | null }>>([])
const program = ref<StoredProgram | null>(null)

// demo program
const demoSlot = ref(30)
const slotName = ref<string | null | undefined>(undefined) // undefined: not checked yet
const overwrite = ref(false)
const stored = ref(false)
const demo = DEMO_PROGRAM
const demoMinutes = computed(() => Math.round((demo.dwellS * demo.frequenciesHz.length) / 6) / 10)

// PC-screen sweep
const sweeping = ref(false)
const sweepHz = ref<number | null>(null)
let stopSweep = false

const stats = computed(() => {
  const s = [...times.value].sort((a, b) => a - b)
  return s.length ? { n: s.length, p50: percentile(s, 50), p95: percentile(s, 95), max: s[s.length - 1]! } : null
})
const histogram = computed(() => {
  const bins = new Array(30).fill(0) as number[]
  for (const t of times.value) bins[Math.min(29, Math.floor(t))]! += 1
  const top = Math.max(1, ...bins)
  return bins.map((n, i) => ({ i, n, h: (n / top) * 100 }))
})

async function run<T>(label: string, fn: () => Promise<T>): Promise<T | undefined> {
  busy.value = label
  error.value = ''
  try {
    return await fn()
  } catch (e) {
    const msg = e instanceof Error ? e.message : String(e)
    error.value = /lost|disconnected|device has been lost|NetworkError/i.test(msg)
      ? 'The connection to the unit was lost (USB unplugged?). Click Connect again.'
      : msg
  } finally {
    busy.value = ''
  }
}

async function connect() {
  await run('Connecting…', async () => {
    if (link.value) await link.value.close().catch(() => undefined)
    link.value = null
    const l = await DisplayLink.connect()
    revision.value = await l.revision()
    if (!revision.value) {
      await l.close()
      throw new Error('No answer from the display on this port. Is the unit switched on normally (not in update mode) ' +
        'and connected by USB? Otherwise choose the entry ending in …903.')
    }
    link.value = l
  })
}

async function disconnect() {
  stopSweep = true
  await link.value?.close().catch(() => undefined)
  link.value = null
  revision.value = null
}

async function measure() {
  if (!link.value) return
  times.value = []
  measured.value = 0
  await run('Measuring…', async () => {
    times.value = await link.value!.replyTimes(COUNT, (d) => (measured.value = d))
  })
}

async function sendTitle() {
  if (!link.value) return
  await run('Sending…', async () => {
    await link.value!.showTitle(title.value)
    titleSent.value = true
  })
}

async function backlight(on: boolean) {
  if (!link.value) return
  await run(on ? 'Backlight on…' : 'Backlight off…', () => link.value!.backlight(on))
}

async function readNames() {
  if (!link.value) return
  names.value = []
  program.value = null
  await run('Reading the program names…', async () => {
    for (let slot = 1; slot <= 30; slot++) names.value.push({ slot, name: await link.value!.programName(slot) })
    if (names.value.every((n) => n.name === null)) {
      throw new Error('The unit did not answer the program queries. This works with OpenGX Pro installed.')
    }
  })
}

async function readProgram(slot: number) {
  if (!link.value) return
  await run(`Reading program ${slot}…`, async () => {
    program.value = await link.value!.program(slot)
  })
}

async function checkSlot() {
  if (!link.value) return
  stored.value = false
  overwrite.value = false
  await run(`Checking slot ${demoSlot.value}…`, async () => {
    const [name, prog] = [await link.value!.programName(demoSlot.value), await link.value!.program(demoSlot.value)]
    if (name === null && prog === null) throw new Error('The unit did not answer. This works with OpenGX Pro installed.')
    slotName.value = prog && prog.frequenciesHz.length ? (name || '(a program without a name)') : null
  })
}

async function storeDemo() {
  if (!link.value) return
  await run(`Storing the demo program in slot ${demoSlot.value}…`, async () => {
    await link.value!.storeProgram(demoSlot.value, demo)
    stored.value = true
    slotName.value = demo.name
  })
}

async function eraseDemo() {
  if (!link.value) return
  await run(`Erasing slot ${demoSlot.value}…`, async () => {
    await link.value!.eraseProgram(demoSlot.value)
    stored.value = false
    slotName.value = null
  })
}

async function sweep() {
  if (!link.value) return
  if (sweeping.value) {
    stopSweep = true
    return
  }
  stopSweep = false
  sweeping.value = true
  await run('Sweeping…', () =>
    link.value!.pcSweep({ title: 'Demo sweep (browser)', fromHz: 100, toHz: 1000, seconds: 20 },
      (hz) => (sweepHz.value = hz), () => stopSweep))
  sweeping.value = false
}

async function clearPc() {
  if (!link.value) return
  await run('Clearing the PC screen…', () => link.value!.clearPcScreen())
  sweepHz.value = null
}

// live clock on the PC screen
const clockOn = ref(false)
let clockTimer: ReturnType<typeof setInterval> | undefined
function toggleClock() {
  if (clockOn.value) {
    clearInterval(clockTimer)
    clockOn.value = false
    return
  }
  if (!link.value) return
  clockOn.value = true
  const tick = () => {
    if (!link.value) return toggleClock()
    const now = new Date().toLocaleTimeString('en-GB')
    link.value.showTitle(`Live from the browser  ${now}`).catch(() => undefined)
  }
  tick()
  clockTimer = setInterval(tick, 1000)
}

// a long title that scrolls
const LONG_TITLE = "A long title scrolls across the unit's PC screen like this"
async function longTitle() {
  if (!link.value) return
  await run('Sending…', () => link.value!.showTitle(LONG_TITLE))
}

// overview of all 30 slots
interface Row { slot: number; name: string; steps: number; dwell: number; total: string; range: string }
const overview = ref<Row[] | null>(null)
const overviewDone = ref(0)
const hms = (s: number) => `${Math.floor(s / 3600)}:${String(Math.floor((s % 3600) / 60)).padStart(2, '0')}:${String(s % 60).padStart(2, '0')}`
async function readOverview() {
  if (!link.value) return
  overview.value = null
  overviewDone.value = 0
  await run('Reading all programs…', async () => {
    const rows: Row[] = []
    for (let slot = 1; slot <= 30; slot++) {
      const p = await link.value!.program(slot)
      overviewDone.value = slot
      if (!p || !p.frequenciesHz.length) continue
      const name = (await link.value!.programName(slot)) || `Slot ${String(slot).padStart(2, '0')}`
      const f = p.frequenciesHz
      rows.push({ slot, name, steps: f.length, dwell: p.dwellS, total: hms(p.dwellS * f.length),
        range: `${fmtHz(Math.min(...f))} – ${fmtHz(Math.max(...f))}` })
    }
    overview.value = rows
  })
}

const fmtHz = (hz: number) => hz.toLocaleString('en', { maximumFractionDigits: 2 }) + ' Hz'
const slotLabel = computed(() => String(demoSlot.value).padStart(2, '0'))

onBeforeUnmount(() => {
  clearInterval(clockTimer)
  void disconnect()
})
</script>

<template>
  <section class="panel">
    <div class="intro single">
      <div>
        <h2>Try it on your unit</h2>
        <p class="lead">A few things to try from the browser once OpenGX Pro is installed: how fast the display answers,
          text on its screen, the stored programs. Switch the unit on normally and connect it by USB: it shows its PC
          screen. Then connect here.</p>
        <p class="demo-only">This page talks to the display only. It never opens a session with the generator boards and
          never switches an output on. Storing the example program writes one program slot, after asking.</p>
        <PortHint purpose="demo" />
        <div class="row">
          <button v-if="!link" class="primary" :disabled="!!busy" @click="connect">Connect</button>
          <button v-else @click="disconnect">Disconnect</button>
          <span v-if="revision" class="note ok inline">Connected: the display answers “{{ revision }}”.</span>
          <span v-if="busy" class="muted">{{ busy }}</span>
        </div>
        <p v-if="error" class="note err" role="alert">{{ error }}</p>
      </div>
    </div>

    <div class="cards" :class="{ off: !link }">
      <article class="card wide">
        <h3>How fast the display answers</h3>
        <p>Sends the version query {{ COUNT }} times and times each reply. The original firmware answers in
          <b>{{ ORIGINAL_FW_MEDIAN }}</b> (median, measured on one unit; <a href="#/docs/measurements">how</a>).</p>
        <div class="row">
          <button :disabled="!link || !!busy" @click="measure">Measure</button>
          <span v-if="busy === 'Measuring…'" class="muted">{{ measured }} / {{ COUNT }}</span>
        </div>
        <div v-if="stats" class="result">
          <div class="big">{{ stats.p50.toFixed(1) }} ms <span>median</span></div>
          <div class="small-stats">p95 {{ stats.p95.toFixed(1) }} ms · slowest {{ stats.max.toFixed(1) }} ms ·
            {{ stats.n }} replies</div>
          <svg class="hist" viewBox="0 0 300 80" role="img" aria-label="Distribution of reply times, 0 to 30 ms">
            <rect v-for="b in histogram" :key="b.i" :x="b.i * 10 + 1" :y="70 - b.h * 0.66" width="8"
                  :height="Math.max(b.n ? 1 : 0, b.h * 0.66)" rx="1.5" />
            <line x1="0" x2="300" y1="70.5" y2="70.5" />
            <line class="ref" x1="193" x2="193" y1="4" y2="70" />
            <text x="197" y="12">original firmware</text>
            <text x="0" y="79">0 ms</text><text x="100" y="79">10</text><text x="200" y="79">20</text><text x="286" y="79">30</text>
          </svg>
        </div>
      </article>

      <article class="card">
        <h3>Text on the unit's screen</h3>
        <p>Sets the title the PC screen shows for G1 (up to 59 characters).</p>
        <input v-model="title" maxlength="59" :disabled="!link" />
        <div class="row">
          <button :disabled="!link || !!busy || !title.trim()" @click="sendTitle">Show on the unit</button>
          <span v-if="titleSent" class="muted">Look at the unit.</span>
        </div>
      </article>

      <article class="card">
        <h3>Backlight</h3>
        <p>Switches the unit's screen off and on again, as the PC software does.</p>
        <div class="row">
          <button :disabled="!link || !!busy" @click="backlight(false)">Off</button>
          <button :disabled="!link || !!busy" @click="backlight(true)">On</button>
        </div>
      </article>

      <article class="card wide">
        <h3>Stored programs</h3>
        <p>Reads the names of the 30 program slots; choose one to see its steps.</p>
        <button :disabled="!link || !!busy" @click="readNames">Read the program names</button>
        <ul v-if="names.length" class="slots">
          <li v-for="n in names" :key="n.slot">
            <button class="slot" :disabled="!n.name || !!busy" @click="readProgram(n.slot)">
              <span>{{ String(n.slot).padStart(2, '0') }}</span>{{ n.name || '–' }}
            </button>
          </li>
        </ul>
        <div v-if="program" class="program">
          <b>Slot {{ String(program.slot).padStart(2, '0') }}:</b> {{ program.frequenciesHz.length }} steps,
          {{ program.dwellS }} s per step, {{ program.amplitudeV.toFixed(1) }} V
          <div class="freqs">{{ program.frequenciesHz.slice(0, 12).map(fmtHz).join(' · ') }}<span
            v-if="program.frequenciesHz.length > 12"> · …</span></div>
        </div>
      </article>
      <article class="card wide">
        <span class="tag">Demo</span>
        <h3>A program running on the unit</h3>
        <p>Stores a short demo program in a slot, then you start it on the unit and watch the new dashboard: step n of N, time
          left, next frequency, waveform view. <b>{{ demo.frequenciesHz.length }} steps</b> from
          {{ fmtHz(demo.frequenciesHz[0]!) }} to {{ fmtHz(demo.frequenciesHz[demo.frequenciesHz.length - 1]!) }},
          {{ demo.dwellS }} s per step (about {{ demoMinutes }} min), sine, {{ demo.amplitudeV.toFixed(1) }} V.</p>
        <div class="row">
          <label class="inline-field">Slot
            <input v-model.number="demoSlot" type="number" min="1" max="30" :disabled="!link || !!busy"
                   @change="slotName = undefined; stored = false" />
          </label>
          <button :disabled="!link || !!busy" @click="checkSlot">Check slot {{ slotLabel }}</button>
        </div>
        <p v-if="slotName === null" class="note ok">Slot {{ slotLabel }} is empty.</p>
        <p v-else-if="slotName !== undefined && !stored" class="note warn">Slot {{ slotLabel }} holds “{{ slotName }}”.
          It would be replaced.
          <label class="check"><input v-model="overwrite" type="checkbox" /> replace it</label></p>
        <div class="row">
          <button class="primary" :disabled="!link || !!busy || slotName === undefined || (slotName !== null && !overwrite && !stored)"
                  @click="storeDemo">Store the demo program in slot {{ slotLabel }}</button>
          <button v-if="stored" :disabled="!link || !!busy" @click="eraseDemo">Erase it again</button>
        </div>
        <ol v-if="stored" class="howto">
          <li>Unplug the USB cable: the unit leaves its PC screen and shows the dashboard.</li>
          <li>Press <b>G1</b> (twice if G1 already has a program): the program list opens.</li>
          <li>Choose slot <b>{{ slotLabel }}</b> with UP/DOWN and press <b>START</b> to load it, then <b>START</b> again to run.</li>
          <li>Hold <b>LOOP</b> to see all steps; <b>STOP</b> ends it. Plug USB in again and click Connect to continue here.</li>
        </ol>
      </article>

      <article class="card wide">
        <span class="tag">Demo</span>
        <h3>A sweep on the PC screen</h3>
        <p>Plays a frequency sweep from 100 Hz to 1,000 Hz onto the unit's PC screen, the way the PC software drives it.
          <b>Display only:</b> this page never switches an output on.</p>
        <div class="row">
          <button :disabled="!link || (!!busy && !sweeping)" @click="sweep">{{ sweeping ? 'Stop' : 'Play the sweep (20 s)' }}</button>
          <button :disabled="!link || !!busy" @click="clearPc">Clear the PC screen</button>
          <span v-if="sweepHz !== null" class="freq-now">{{ fmtHz(sweepHz) }}</span>
        </div>
      </article>

      <article class="card wide always">
        <span class="tag">Demo</span>
        <h3>A tour of the unit</h3>
        <p>No computer needed: what to press on the unit to see each feature. Programs, sweeps and chains run on the unit
          itself.</p>
        <TourCard />
      </article>

      <article class="card">
        <span class="tag">Demo</span>
        <h3>A live clock on the PC screen</h3>
        <p>Updates the PC screen's title every second with this computer's time.</p>
        <button :disabled="!link || (!!busy && !clockOn)" @click="toggleClock">{{ clockOn ? 'Stop the clock' : 'Start the clock' }}</button>
      </article>

      <article class="card">
        <span class="tag">Demo</span>
        <h3>A title that scrolls</h3>
        <p>Sends a title too long for the screen: the unit scrolls it, as it does with long program names.</p>
        <button :disabled="!link || !!busy" @click="longTitle">Show a long title</button>
      </article>

      <article class="card wide">
        <span class="tag">Demo</span>
        <h3>All programs at a glance</h3>
        <p>Reads every program slot and lists steps, time per step, running time and frequency range.</p>
        <div class="row">
          <button :disabled="!link || !!busy" @click="readOverview">Read all programs</button>
          <span v-if="busy === 'Reading all programs…'" class="muted">{{ overviewDone }} / 30</span>
        </div>
        <table v-if="overview && overview.length" class="overview">
          <thead><tr><th>Slot</th><th>Name</th><th>Steps</th><th>Per step</th><th>Running time</th><th>Frequencies</th></tr></thead>
          <tbody>
            <tr v-for="r in overview" :key="r.slot">
              <td>{{ String(r.slot).padStart(2, '0') }}</td><td>{{ r.name }}</td><td>{{ r.steps }}</td><td>{{ r.dwell }} s</td>
              <td>{{ r.total }}</td><td>{{ r.range }}</td>
            </tr>
          </tbody>
        </table>
        <p v-else-if="overview" class="muted">No programs stored.</p>
      </article>
    </div>
  </section>
</template>
