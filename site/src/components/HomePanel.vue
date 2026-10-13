<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { REPO_URL } from '../lib/links'
import { rowCount } from '../lib/matrix'
import { loadReleases, preferred, type Release } from '../lib/releases'
import { vReveal } from '../lib/reveal'
import { gallery, screen, shotCount } from '../lib/screens'
import ComparisonMatrix from './ComparisonMatrix.vue'
import FaqList from './FaqList.vue'
import HeroShowcase from './HeroShowcase.vue'
import KeysDemo from './KeysDemo.vue'
import SupportCard from './SupportCard.vue'

const latest = ref<Release | undefined>()
onMounted(async () => {
  latest.value = preferred(await loadReleases().catch(() => []))
})
const strip = gallery.flatMap((g) => g.shots).filter((_, i) => i % 3 === 0)
</script>

<template>
  <section class="hero">
    <div class="hero-bg" aria-hidden="true">
      <i class="glow a"></i><i class="glow b"></i><i class="glow c"></i><i class="grid"></i>
      <svg class="wave" viewBox="0 0 1440 220" preserveAspectRatio="none">
        <defs>
          <linearGradient id="wave-ink" x1="0" x2="1">
            <stop offset="0" stop-color="#3fd0e0" stop-opacity="0" /><stop offset=".25" stop-color="#3fd0e0" />
            <stop offset=".65" stop-color="#ff4d4d" /><stop offset="1" stop-color="#f5b544" stop-opacity="0" />
          </linearGradient>
        </defs>
        <path d="M0 110 C 90 10, 180 10, 270 110 S 450 210, 540 110 S 720 10, 810 110 S 990 210, 1080 110 S 1260 10, 1350 110 S 1440 160, 1440 160" />
      </svg>
    </div>
    <div class="wrap hero-top">
      <a class="pill" href="#/docs/known-issues"><b>Beta 0.9.0</b><span>free and open source</span><i aria-hidden="true">→</i></a>
      <h1>More from the screen of your <span class="grad">GeneratorX Pro</span></h1>
      <p class="hero-lead">OpenGX Pro is free software for the screen and keys of the Spooky2 GeneratorX Pro. It shows more,
        does more without a computer (the Terrain Protocol is built in), and answers Spooky2 ten times faster. Your programs
        stay, and you can go back to the original whenever you like.</p>
      <p class="hero-cta">
        <a class="btn primary big" href="#/install">Install{{ latest ? ` ${latest.version}` : '' }} <span class="sub">3 steps, about 5 minutes</span></a>
        <a class="btn ghost big" href="#/compare">What is different?</a>
      </p>
      <ul class="trust">
        <li>Works with Spooky2 as before</li>
        <li>Your programs and calibration stay</li>
        <li>Back to the original in a minute</li>
        <li>Free, source public</li>
      </ul>
    </div>
    <div class="wrap">
      <HeroShowcase />
    </div>
  </section>

  <section class="band">
    <div class="wrap">
      <div class="plain">
        <article v-reveal>
          <span class="ico"><svg viewBox="0 0 24 24"><rect x="3" y="4" width="18" height="13" rx="2.5" /><path d="M8 21h8M12 17v4" /></svg></span>
          <h3>What is it?</h3>
          <p>New software for the part of your unit that runs the <b>screen and the keys</b>. Nothing is opened, nothing is
            soldered: it goes in over the USB cable, from this page.</p>
        </article>
        <article v-reveal="80">
          <span class="ico"><svg viewBox="0 0 24 24"><path d="M12 3l7 3v5.5c0 4.4-2.9 8.2-7 9.5-4.1-1.3-7-5.1-7-9.5V6l7-3z" /><path d="M8.8 12l2.3 2.3 4.1-4.6" /></svg></span>
          <h3>What stays the same?</h3>
          <p>Spooky2 on your computer works as before. Your 30 programs, their names and the calibration stay in the unit.
            The two generators inside are not touched.</p>
        </article>
        <article v-reveal="160">
          <span class="ico"><svg viewBox="0 0 24 24"><path d="M4 12a8 8 0 1 0 2.6-5.9" /><path d="M4 4v5h5" /></svg></span>
          <h3>Can I go back?</h3>
          <p>Yes, in about a minute, with Spooky2's own updater file. The part of the unit that makes updating possible is
            never changed, so this way stays open.</p>
        </article>
      </div>
    </div>
  </section>

  <section class="wrap section">
    <header class="section-head" v-reveal>
      <span class="eyebrow">What you get</span>
      <h2>A unit that tells you more, and does more by itself</h2>
    </header>
    <div class="bento">
      <article class="tile wide" v-reveal>
        <div>
          <h3>Everything about the running program</h3>
          <p>Which step of how many, how long it still takes, which frequency comes next, the waveform. The second
            generator stays in view in a strip at the bottom.</p>
        </div>
        <img :src="screen('dash_running')" alt="Dashboard while a program runs" width="320" height="240" loading="lazy" />
      </article>
      <article class="tile" v-reveal="60">
        <img :src="screen('manual_sweep')" alt="Manual generator with a sweep" width="320" height="240" loading="lazy" />
        <h3>A generator by hand</h3>
        <p>Set a frequency or a sweep with the keys and play it at once. No computer.</p>
      </article>
      <article class="tile" v-reveal="120">
        <img :src="screen('chains')" alt="Chains and plans" width="320" height="240" loading="lazy" />
        <h3>Programs in a row</h3>
        <p>Chains play programs one after the other. A plan gives every day its chain.</p>
      </article>
      <article class="tile" v-reveal>
        <img :src="screen('resume_offer')" alt="Offer to resume after a power cut" width="320" height="240" loading="lazy" />
        <h3>Power cut? It goes on</h3>
        <p>The unit remembers where it was. Timers stop a program or switch the unit off when all is done.</p>
      </article>
      <article class="tile" v-reveal="60">
        <img :src="screen('night_view')" alt="Night view" width="320" height="240" loading="lazy" />
        <h3>Day and night</h3>
        <p>Dark, light and high-contrast themes. The screen dims by itself; the night view barely glows.</p>
      </article>
      <article class="tile speed" v-reveal="120">
        <h3>Ten times faster with Spooky2</h3>
        <p>How long the display takes to answer the computer:</p>
        <dl class="bars">
          <div><dt>Original</dt><dd><i style="--w: 100%"></i><b>about 20 ms</b></dd></div>
          <div class="ours"><dt>OpenGX Pro</dt><dd><i style="--w: 10%"></i><b>about 2 ms</b></dd></div>
        </dl>
        <p class="fine">Median of 1,000 requests on one unit. <a href="#/docs/measurements">How it was measured</a></p>
      </article>
      <article class="tile wide flip" v-reveal>
        <img :src="screen('pc_monitor_info')" alt="PC monitor" width="320" height="240" loading="lazy" />
        <div>
          <h3>With the computer: see what Spooky2 plays</h3>
          <p>Plug in USB and the screen becomes a monitor: both programs, all four outputs, waveform and amplitude. The
            screen only watches; Spooky2 stays in control.</p>
        </div>
      </article>
    </div>
  </section>

  <section class="band keys">
    <div class="wrap" v-reveal>
      <KeysDemo />
    </div>
  </section>

  <section class="wrap section" id="glance">
    <header class="section-head" v-reveal>
      <span class="eyebrow">At a glance</span>
      <h2>The original firmware and OpenGX Pro</h2>
      <p>The short version. The complete list has {{ rowCount }} points, including what is <em>not</em> in OpenGX Pro.</p>
    </header>
    <div v-reveal><ComparisonMatrix /></div>
    <p class="center" v-reveal><a class="btn primary" href="#/compare">See the complete list</a></p>
  </section>

  <section class="band terrain">
    <div class="wrap terrain-grid">
      <div v-reveal>
        <span class="eyebrow">Built in</span>
        <h2>The Terrain Protocol runs on the unit itself</h2>
        <p class="lead">No computer, no uploading set after set. Open Menu → Chains: the Terrain Protocol stands first in
          the list. Press START.</p>
        <ul>
          <li><b>All three flavours:</b> Terrain, Terrain-Mercury and Terrain-Nonviolent, each as Remote and as Contact sets.</li>
          <li><b>It keeps count:</b> the screen shows the day, for example “Day 6/11”, and tells you when a set is finished.</li>
          <li><b>It goes on where it stopped,</b> also after switching off or a power cut.</li>
          <li><b>A preset with sweeps too:</b> “Radionics (Boost) Manifestation” is built in. Programs uploaded from
            Spooky2 cannot sweep; this one does.</li>
        </ul>
        <p class="muted">The frequencies and times of both are taken from Spooky2's presets.</p>
      </div>
      <div class="terrain-shots" v-reveal="120">
        <figure><img :src="screen('chains_terrain')" alt="Terrain first in the list of chains" width="320" height="240" loading="lazy" /><figcaption>First in the list</figcaption></figure>
        <figure><img :src="screen('terrain_contact')" alt="Terrain, Contact, day 3 of 11" width="320" height="240" loading="lazy" /><figcaption>Day 3 of 11, session 2 of 2</figcaption></figure>
        <figure><img :src="screen('dash_terrain')" alt="Terrain running" width="320" height="240" loading="lazy" /><figcaption>Running: “Day 6/11”</figcaption></figure>
      </div>
    </div>
  </section>

  <section class="wrap section">
    <header class="section-head" v-reveal>
      <span class="eyebrow">Every screen</span>
      <h2>{{ shotCount }} screens, all real</h2>
      <p>Drawn by the firmware's own code from sample data. The program names in them are examples.</p>
    </header>
    <div class="marquee" v-reveal aria-hidden="true">
      <div class="marquee-track">
        <img v-for="(s, i) in [...strip, ...strip]" :key="i" :src="screen(s.name)" alt="" width="320" height="240" loading="lazy" />
      </div>
    </div>
    <p class="center" v-reveal><a class="btn" href="#/screens">Look at every screen</a></p>
  </section>

  <section class="band">
    <div class="wrap">
      <header class="section-head" v-reveal>
        <span class="eyebrow">Install</span>
        <h2>Three steps, from this page</h2>
      </header>
      <ol class="howto3">
        <li v-reveal>
          <span class="n">1</span>
          <h3>Put the unit into update mode</h3>
          <p>Switch it off. Hold <kbd>Stop</kbd> and press <kbd>Power</kbd>. The unit now waits for new software. The
            Install page shows this as a moving picture.</p>
        </li>
        <li v-reveal="80">
          <span class="n">2</span>
          <h3>Choose a version and click Install</h3>
          <p>On a computer with Chrome, Edge or Opera, connected by the USB cable. It takes about a minute.</p>
        </li>
        <li v-reveal="160">
          <span class="n">3</span>
          <h3>Switch the unit off and on</h3>
          <p>The start screen says “OpenGX Pro”. Your programs are where they were.</p>
        </li>
      </ol>
      <p class="center" v-reveal>
        <a class="btn primary big" href="#/install">Go to Install</a>
        <a class="btn big" href="#/restore">How to go back</a>
      </p>
    </div>
  </section>

  <section class="wrap section narrow">
    <header class="section-head" v-reveal>
      <span class="eyebrow">Questions</span>
      <h2>Good to know before you start</h2>
    </header>
    <div v-reveal><FaqList /></div>
    <p class="center muted" v-reveal>More in the <a href="#/docs">guide</a>, or look at the
      <a :href="REPO_URL" target="_blank" rel="noopener">source on GitHub</a>.</p>
  </section>

  <section class="wrap section">
    <div v-reveal><SupportCard /></div>
  </section>
</template>
