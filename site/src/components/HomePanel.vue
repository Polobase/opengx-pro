<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { loadReleases, preferred, type Release } from '../lib/releases'
import { REPO_URL } from '../lib/links'
import SupportCard from './SupportCard.vue'
// Screens of the firmware, drawn by its own code (the repository's gallery baselines).
import splash from '../../../tests/gallery/nx/splash_1100.png'
import dashboard from '../../../tests/gallery/nx/dash_running.png'
import menu from '../../../tests/gallery/nx/menu.png'
import chains from '../../../tests/gallery/nx/chains.png'
import manualSweep from '../../../tests/gallery/nx/manual_sweep.png'
import pcMonitor from '../../../tests/gallery/nx/pc_monitor.png'
import library from '../../../tests/gallery/nx/library.png'

const latest = ref<Release | undefined>()
onMounted(async () => {
  latest.value = preferred(await loadReleases().catch(() => []))
})

const shots = [
  { src: dashboard, title: 'Dashboard', text: 'The frequency, step n of N, time per step and time left, the next frequency and the waveform.' },
  { src: menu, title: 'Menu', text: 'Programs, chains, the manual generator, timers, diagnostics and settings, on the unit itself.' },
  { src: manualSweep, title: 'Manual generator and sweeps', text: 'A frequency or a sweep set with the keys and played at once, without a PC.' },
  { src: chains, title: 'Chains and plans', text: 'Several programs one after the other, and a chain per day.' },
  { src: pcMonitor, title: 'PC monitor', text: 'In PC mode: what Spooky2 plays on each output. The display only watches.' },
  { src: library, title: 'Program list', text: 'The 30 stored programs, chosen and loaded as on the original firmware.' },
]
</script>

<template>
  <section class="panel hero">
    <div>
      <h2>A new firmware for the unit's display, open and reversible</h2>
      <p class="lead">OpenGX Pro replaces the firmware of the GeneratorX Pro's display board. Spooky2 on the PC works as
        before, your stored programs and calibration stay, and the original firmware can be put back at any time.</p>
      <ul class="points">
        <li><b>More on the screen.</b> Step and time left, the next frequency, a waveform view, themes and a night view.</li>
        <li><b>More without a PC.</b> Manual generator with sweeps, chains and multi-day plans, timers, resume after a
          power cut, copy and move programs between slots.</li>
        <li><b>Faster replies to the PC.</b> About 2 ms instead of about 20 ms
          (<a href="#/docs/measurements">how it was measured</a>).</li>
        <li><b>Nothing hidden.</b> The source is public; every difference to the original is
          <a href="#/docs/compare">written down</a>.</li>
      </ul>
      <p class="row">
        <a class="button primary" href="#/install">Install{{ latest ? ` version ${latest.version}` : '' }}</a>
        <a class="button" href="#/docs/features">What it can do</a>
        <a class="button" :href="REPO_URL" target="_blank" rel="noopener">Source</a>
      </p>
      <p class="note warn"><b>Beta.</b> The firmware is tested by more than 42,000 automated checks and runs on a real unit with
        Spooky2, but most of the checklist on the device is still to be done. Read
        <a href="#/docs/known-issues">Known issues</a> first; going back to the original takes a minute.</p>
    </div>
    <img class="hero-shot" :src="splash" alt="The OpenGX Pro start screen" width="320" height="240" />
  </section>

  <section class="panel">
    <h2>On the unit</h2>
    <div class="shots grid">
      <figure v-for="s in shots" :key="s.title">
        <img :src="s.src" :alt="s.title" width="320" height="240" loading="lazy" />
        <figcaption><b>{{ s.title }}.</b> {{ s.text }}</figcaption>
      </figure>
    </div>
    <p class="muted">The screens are drawn by the firmware's own code; the program names in them are examples.</p>
  </section>

  <section class="panel">
    <h2>How it works</h2>
    <ol class="plain">
      <li><b>Install.</b> Put the unit into its update mode (off, hold STOP, press POWER), choose a version on the
        <a href="#/install">Install</a> page, wait about a minute. Needs Chrome, Edge or Opera on a desktop computer.</li>
      <li><b>Use it.</b> The keys do what they did; the <a href="#/docs/keys">key guide</a> shows what is new.</li>
      <li><b>Go back whenever you like.</b> The <a href="#/restore">Restore original</a> page installs the vendor's own
        firmware from the vendor's own updater file. The unit's bootloader is never touched, so this always works.</li>
    </ol>
  </section>

  <SupportCard />
</template>
