<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { screen } from '../lib/screens'
import UnitMock from './UnitMock.vue'

/** What the firmware can do, one thing at a time, each with its real screen. Rotates by itself; a click chooses. */
const features = [
  { id: 'now', title: 'See what is playing', text: 'The frequency, step 12 of 180, the time left, the next frequency and the waveform: all on one screen.', shot: 'dash_running', keys: ['START'] },
  { id: 'menu', title: 'A menu on the unit', text: 'Programs, chains, the manual generator, timers and settings. Press DOWN.', shot: 'menu', keys: ['DOWN'] },
  { id: 'manual', title: 'Set a frequency by hand', text: 'The manual generator plays a frequency or a sweep at once. No computer needed.', shot: 'manual_sweep', keys: ['G1', 'G2'] },
  { id: 'chains', title: 'Programs in a row', text: 'Chains play several programs one after the other. Plans give every day its chain.', shot: 'chains', keys: ['START', 'LOOP'] },
  { id: 'resume', title: 'Picks up after a power cut', text: 'The unit remembers where it was and offers to go on.', shot: 'resume_offer', keys: ['START', 'STOP'] },
  { id: 'pc', title: 'Spooky2, in view', text: 'In PC mode the screen shows what Spooky2 plays on every output, and answers it ten times faster.', shot: 'pc_monitor_info', keys: [] },
  { id: 'night', title: 'Easy on the eyes', text: 'Themes and brightness. The screen dims by itself; the night view keeps both generators in sight.', shot: 'night_view', keys: ['UP'] },
  { id: 'terrain', title: 'Terrain Protocol built in', text: 'Start it on the unit: it runs the sets by itself, counts the days and goes on where it stopped.', shot: 'dash_terrain', keys: ['START'] },
  { id: 'preset', title: 'A preset with sweeps, without a PC', text: 'Spooky2’s “Radionics (Boost) Manifestation” is built in and plays with its sweeps on the unit alone.', shot: 'dash_preset', keys: ['START'] },
]
const STEP_MS = 5200

const index = ref(0)
const paused = ref(false)
const current = computed(() => features[index.value]!)
let timer = 0

function go(i: number, user = false) {
  index.value = (i + features.length) % features.length
  if (user) restart()
}
function restart() {
  window.clearInterval(timer)
  timer = window.setInterval(() => !paused.value && go(index.value + 1), STEP_MS)
}
const reduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches
onMounted(() => !reduced && restart())
onBeforeUnmount(() => window.clearInterval(timer))
</script>

<template>
  <div class="showcase" @mouseenter="paused = true" @mouseleave="paused = false" @focusin="paused = true" @focusout="paused = false">
    <ol class="showcase-list">
      <li v-for="(f, i) in features" :key="f.id" :class="{ on: i === index }">
        <button type="button" :aria-current="i === index ? 'true' : undefined" @click="go(i, true)">
          <span class="num">{{ String(i + 1).padStart(2, '0') }}</span>
          <span class="body">
            <b>{{ f.title }}</b>
            <span class="text">{{ f.text }}</span>
          </span>
          <i v-if="i === index" :key="index" class="bar" :class="{ paused, still: reduced }" :style="{ animationDuration: STEP_MS + 'ms' }"></i>
        </button>
      </li>
    </ol>
    <div class="showcase-stage">
      <UnitMock :src="screen(current.shot)" :alt="current.title" :lit="current.keys" />
      <p class="showcase-caption"><b>{{ current.title }}.</b> {{ current.text }}</p>
      <div class="showcase-dots" role="tablist" aria-label="Features">
        <button v-for="(f, i) in features" :key="f.id" type="button" role="tab" :aria-selected="i === index" :aria-label="f.title"
                :class="{ on: i === index }" @click="go(i, true)"></button>
      </div>
    </div>
  </div>
</template>
