<script setup lang="ts">
import { computed, ref } from 'vue'
import { webSerialSupported } from './lib/byteport'
import { COFFEE_URL, REPO_URL } from './lib/links'
import DemoPanel from './components/DemoPanel.vue'
import DocsPanel from './components/DocsPanel.vue'
import HomePanel from './components/HomePanel.vue'
import InstallPanel from './components/InstallPanel.vue'
import RestorePanel from './components/RestorePanel.vue'

/** The page's sections are hash routes (#/install, #/docs/keys, ...), so every one has a link. */
type Section = 'home' | 'install' | 'restore' | 'try' | 'docs'
const sections: Array<{ id: Section; label: string }> = [
  { id: 'home', label: 'Overview' },
  { id: 'install', label: 'Install' },
  { id: 'restore', label: 'Restore original' },
  { id: 'try', label: 'Try it' },
  { id: 'docs', label: 'Docs' },
]
const parse = (): { section: Section; page: string } => {
  const [, name = '', page = ''] = /^#\/?([\w-]*)\/?([\w-]*)/.exec(location.hash) ?? []
  return { section: sections.some((s) => s.id === name) ? (name as Section) : 'home', page }
}
const route = ref(parse())
window.addEventListener('hashchange', () => {
  route.value = parse()
  window.scrollTo(0, 0)
})
const needsSerial = computed(() => ['install', 'restore', 'try'].includes(route.value.section))
const supported = webSerialSupported()
</script>

<template>
  <header class="top">
    <div class="wrap">
      <div class="brand">
        <span class="kicker">Open firmware · beta</span>
        <h1><a href="#/">OpenGX Pro</a></h1>
        <p>Open firmware for the display of the Spooky2 GeneratorX Pro. Install it from your browser, and put the original
          firmware back whenever you like.</p>
      </div>
      <nav class="tabs" aria-label="Sections">
        <a v-for="s in sections" :key="s.id" :href="`#/${s.id === 'home' ? '' : s.id}`" :class="{ active: route.section === s.id }"
           :aria-current="route.section === s.id ? 'page' : undefined">{{ s.label }}</a>
        <span class="spacer"></span>
        <a class="ext" :href="REPO_URL" target="_blank" rel="noopener">GitHub</a>
        <a class="ext coffee" :href="COFFEE_URL" target="_blank" rel="noopener">Buy me a coffee</a>
      </nav>
    </div>
  </header>

  <main class="wrap">
    <p v-if="needsSerial && !supported" class="note err" role="alert">This browser cannot talk to USB serial devices. Please
      open this page in <b>Chrome</b>, <b>Edge</b> or <b>Opera</b> on a desktop computer.</p>
    <KeepAlive>
      <HomePanel v-if="route.section === 'home'" />
      <InstallPanel v-else-if="route.section === 'install'" />
      <RestorePanel v-else-if="route.section === 'restore'" />
      <DemoPanel v-else-if="route.section === 'try'" />
      <DocsPanel v-else :page="route.page" />
    </KeepAlive>
    <section v-if="needsSerial" class="panel safety">
      <h2>What this page can and cannot do</h2>
      <ul>
        <li>Only the display firmware is replaced, through the unit's own update mode. The bootloader and the generator
          boards cannot be written this way.</li>
        <li>If an update stops half way, switch the unit off, put it into update mode again and repeat it, or restore the
          original.</li>
        <li>Stored programs, names and calibration are kept.</li>
        <li>Everything runs in your browser. No file you choose is uploaded anywhere.</li>
      </ul>
      <p class="muted">More: <a href="#/docs/safety">Safety and recovery</a>.</p>
    </section>
  </main>

  <footer class="wrap foot">
    <p>OpenGX Pro is an independent open-source project under the MIT licence, without any warranty. It is not affiliated
      with or endorsed by Spooky2; “Spooky2” and “GeneratorX” are names of their owner.</p>
    <p><a :href="REPO_URL" target="_blank" rel="noopener">Source on GitHub</a> ·
      <a :href="`${REPO_URL}/issues`" target="_blank" rel="noopener">Report a problem</a> ·
      <a :href="COFFEE_URL" target="_blank" rel="noopener">Buy me a coffee</a></p>
  </footer>
</template>
