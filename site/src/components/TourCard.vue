<script setup lang="ts">
import { computed, ref } from 'vue'
// Screens of the firmware, drawn by its own code (the repository's gallery baselines).
import dashRunning from '../../../tests/gallery/nx/dash_running.png'
import detail from '../../../tests/gallery/nx/detail_playing.png'
import menu from '../../../tests/gallery/nx/menu.png'
import manualFixed from '../../../tests/gallery/nx/manual_fixed.png'
import manualSweep from '../../../tests/gallery/nx/manual_sweep.png'
import chains from '../../../tests/gallery/nx/chains.png'
import timers from '../../../tests/gallery/nx/settings_timers.png'
import resume from '../../../tests/gallery/nx/resume_offer.png'
import diagnostics from '../../../tests/gallery/nx/diagnostics.png'
import quick from '../../../tests/gallery/nx/quick.png'
import lock from '../../../tests/gallery/nx/lock_card.png'
import settings from '../../../tests/gallery/nx/settings.png'
import support from '../../../tests/gallery/nx/support.png'

const steps = [
  { img: dashRunning, title: 'Run a program', press: 'G1, choose a program with UP/DOWN, START to load it, START again to run it.',
    see: 'The dashboard: the frequency in large digits, step n of N, time left, the next frequency and a waveform view.' },
  { img: detail, title: 'See every step', press: 'Hold LOOP on the dashboard.',
    see: 'All step frequencies of the program, the playing step marked.' },
  { img: menu, title: 'The menu', press: 'DOWN on the dashboard.',
    see: 'Programs, Chains, Manual, Timers, Diagnostics and Settings.' },
  { img: manualFixed, title: 'Manual generator', press: 'Menu → Manual. UP/DOWN pick a row, G1/G2 change the value, LOOP moves the digit, START plays.',
    see: 'A frequency, waveform and amplitude set on the unit and played at once, without a PC.' },
  { img: manualSweep, title: 'Sweeps', press: 'Menu → Manual → Mode: Sweep. Set Start, End, Time and Shape, then START.',
    see: 'A linear or logarithmic sweep, once, again or back and forth; the dashboard follows it.' },
  { img: chains, title: 'Chains and plans', press: 'Menu → Chains. UP/DOWN select a row, G1/G2 the generator, START plays; LOOP edits a chain.',
    see: 'Several programs in a row, and a chain per day as a plan.' },
  { img: timers, title: 'Timers and auto-start', press: 'Menu → Timers.',
    see: 'Stop a generator after a set time, power off when all programs have finished, start the last programs at power-on.' },
  { img: resume, title: 'Resume after a power cut', press: 'Let a program run for over a minute, pull the power plug, switch on again.',
    see: 'The unit offers to continue where it stopped: START resumes, STOP discards.' },
  { img: diagnostics, title: 'Diagnostics and key test', press: 'Menu → Diagnostics; LOOP starts the key test.',
    see: 'The link to each generator with errors and reply time, memory status, temperature and supply.' },
  { img: quick, title: 'Quick settings', press: 'UP on the dashboard.',
    see: 'Brightness, night mode, key lock, screen off, auto-dim, reduce motion.' },
  { img: lock, title: 'Key lock', press: 'Hold G1+G2 for 1.5 s; unlock with START+STOP held 1.5 s.',
    see: 'The keys are locked; POWER still switches the unit off.' },
  { img: settings, title: 'Settings and themes', press: 'Menu → Settings.',
    see: 'Dark, Light and High-contrast themes, dimming, key repeat, and “Original key behaviour” for the familiar keys.' },
  { img: support, title: 'About and support', press: 'Menu → Settings → About → Support the project.',
    see: 'The project, the firmware version, and a QR code if you would like to buy the developer a coffee.' },
]

const i = ref(0)
const step = computed(() => steps[i.value]!)
</script>

<template>
  <div class="tour">
    <img :src="step.img" :alt="step.title" />
    <div class="tour-text">
      <div class="muted">Step {{ i + 1 }} of {{ steps.length }}</div>
      <h4>{{ step.title }}</h4>
      <p><b>Press:</b> {{ step.press }}</p>
      <p><b>You see:</b> {{ step.see }}</p>
      <div class="row">
        <button :disabled="i === 0" @click="i--">Back</button>
        <button class="primary" :disabled="i === steps.length - 1" @click="i++">Next</button>
      </div>
      <div class="dots" aria-hidden="true">
        <span v-for="(_, n) in steps" :key="n" :class="{ on: n === i }" @click="i = n"></span>
      </div>
    </div>
  </div>
</template>
