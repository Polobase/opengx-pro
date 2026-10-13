<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { webSerialSupported } from './lib/byteport'
import { COFFEE_URL, REPO_URL } from './lib/links'
import { cycleTheme, theme } from './lib/theme'
import BrandMark from './components/BrandMark.vue'
import ComparePanel from './components/ComparePanel.vue'
import DemoPanel from './components/DemoPanel.vue'
import DocsPanel from './components/DocsPanel.vue'
import HomePanel from './components/HomePanel.vue'
import InstallPanel from './components/InstallPanel.vue'
import RestorePanel from './components/RestorePanel.vue'
import ScreensPanel from './components/ScreensPanel.vue'

/** The page's sections are hash routes (#/install, #/docs/keys, ...), so every one has a link. */
type Section = 'home' | 'compare' | 'screens' | 'install' | 'restore' | 'try' | 'docs'
const sections: Array<{ id: Section; label: string; title: string; lead: string }> = [
  { id: 'home', label: 'Overview', title: '', lead: '' },
  { id: 'compare', label: 'What is different', title: 'The original firmware and OpenGX Pro, point by point',
    lead: 'Everything that stays the same, everything that is new, what behaves differently, and what OpenGX Pro does not have.' },
  { id: 'screens', label: 'Screens', title: 'Every screen', lead: 'What you see on the unit, from switching on to switching off.' },
  { id: 'install', label: 'Install', title: 'Install OpenGX Pro',
    lead: 'From this page, over the USB cable. It takes about five minutes, and you can go back whenever you like.' },
  { id: 'restore', label: 'Go back', title: 'Go back to the original firmware',
    lead: 'With Spooky2’s own updater file, in about a minute. This always works.' },
  { id: 'try', label: 'Try it', title: 'Try it from the browser',
    lead: 'A few things to do with the installed firmware: how fast it answers, text on its screen, your stored programs.' },
  { id: 'docs', label: 'Guide', title: '', lead: '' },
]
const parse = (): { section: Section; page: string } => {
  const [, name = '', page = ''] = /^#\/?([\w-]*)\/?([\w-]*)/.exec(location.hash) ?? []
  return { section: sections.some((s) => s.id === name) ? (name as Section) : 'home', page }
}
const route = ref(parse())
const current = computed(() => sections.find((s) => s.id === route.value.section)!)
const scrolled = ref(false)
function onHash() {
  const before = route.value.section
  route.value = parse()
  if (route.value.section !== before || route.value.section === 'docs') window.scrollTo(0, 0)
}
const onScroll = () => (scrolled.value = window.scrollY > 8)
onMounted(() => {
  window.addEventListener('hashchange', onHash)
  window.addEventListener('scroll', onScroll, { passive: true })
})
onBeforeUnmount(() => {
  window.removeEventListener('hashchange', onHash)
  window.removeEventListener('scroll', onScroll)
})
const needsSerial = computed(() => ['install', 'restore', 'try'].includes(route.value.section))
const supported = webSerialSupported()
</script>

<template>
  <header class="nav" :class="{ solid: scrolled || route.section !== 'home', over: route.section === 'home' }">
    <div class="wrap nav-in">
      <a class="logo" href="#/" aria-label="OpenGX Pro, overview"><BrandMark :size="26" /><span>OpenGX Pro</span><em>beta</em></a>
      <nav aria-label="Sections">
        <a v-for="s in sections.slice(1)" :key="s.id" :href="`#/${s.id}`" :class="{ active: route.section === s.id }"
           :aria-current="route.section === s.id ? 'page' : undefined">{{ s.label }}</a>
      </nav>
      <div class="nav-actions">
        <button type="button" class="icon-btn" :title="`Colours: ${theme === 'auto' ? 'as the system' : theme}`" aria-label="Switch between light and dark"
                @click="cycleTheme">
          <svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="4.2" /><path d="M12 2.5v2.2M12 19.3v2.2M2.5 12h2.2M19.3 12h2.2M5.3 5.3l1.6 1.6M17.1 17.1l1.6 1.6M5.3 18.7l1.6-1.6M17.1 6.9l1.6-1.6" /></svg>
        </button>
        <a class="icon-btn" :href="REPO_URL" target="_blank" rel="noopener" aria-label="Source on GitHub" title="Source on GitHub">
          <svg viewBox="0 0 24 24" aria-hidden="true" class="fill"><path d="M12 .5C5.65.5.5 5.65.5 12c0 5.1 3.3 9.4 7.9 10.9.6.1.8-.25.8-.56v-2c-3.2.7-3.9-1.4-3.9-1.4-.5-1.3-1.3-1.7-1.3-1.7-1.05-.7.08-.7.08-.7 1.15.08 1.76 1.2 1.76 1.2 1.03 1.76 2.7 1.25 3.36.96.1-.75.4-1.25.73-1.54-2.55-.3-5.24-1.28-5.24-5.7 0-1.26.45-2.3 1.2-3.1-.13-.3-.52-1.47.1-3.06 0 0 .98-.3 3.2 1.2a11 11 0 0 1 5.8 0c2.2-1.5 3.2-1.2 3.2-1.2.62 1.6.23 2.77.1 3.06.75.8 1.2 1.84 1.2 3.1 0 4.43-2.7 5.4-5.26 5.7.42.36.78 1.06.78 2.14v3.17c0 .3.2.67.8.56A11.5 11.5 0 0 0 23.5 12C23.5 5.65 18.35.5 12 .5z" /></svg>
        </a>
        <a class="btn coffee" :href="COFFEE_URL" target="_blank" rel="noopener">Buy me a coffee</a>
      </div>
    </div>
  </header>

  <main>
    <HomePanel v-if="route.section === 'home'" />
    <template v-else>
      <header v-if="current.title" class="page-head">
        <div class="wrap">
          <h1>{{ current.title }}</h1>
          <p>{{ current.lead }}</p>
        </div>
      </header>
      <div class="wrap page">
        <p v-if="needsSerial && !supported" class="note err" role="alert">This browser cannot talk to USB devices. Please open
          this page in <b>Chrome</b>, <b>Edge</b> or <b>Opera</b> on a desktop or laptop computer.</p>
        <KeepAlive>
          <ComparePanel v-if="route.section === 'compare'" />
          <ScreensPanel v-else-if="route.section === 'screens'" />
          <InstallPanel v-else-if="route.section === 'install'" />
          <RestorePanel v-else-if="route.section === 'restore'" />
          <DemoPanel v-else-if="route.section === 'try'" />
          <DocsPanel v-else :page="route.page" />
        </KeepAlive>
        <section v-if="needsSerial" class="panel safety">
          <h2>What this page can and cannot do</h2>
          <ul>
            <li>Only the software of the screen is replaced, through the unit's own update mode. The generators and the part
              that makes updating possible cannot be written this way.</li>
            <li>If an update stops half way, switch the unit off, put it into update mode again and repeat it.</li>
            <li>Stored programs, names and calibration are kept.</li>
            <li>Everything runs in your browser. No file you choose is uploaded anywhere.</li>
          </ul>
          <p class="muted">More: <a href="#/docs/safety">Safety and recovery</a>.</p>
        </section>
      </div>
    </template>
  </main>

  <footer class="foot">
    <div class="wrap foot-in">
      <div>
        <a class="logo" href="#/"><BrandMark :size="22" /><span>OpenGX Pro</span></a>
        <p>An independent open-source project under the MIT licence, without any warranty. Not affiliated with or endorsed by
          Spooky2; “Spooky2” and “GeneratorX” are names of their owner.</p>
      </div>
      <nav aria-label="More">
        <a href="#/docs">Guide</a>
        <a href="#/docs/safety">Safety and recovery</a>
        <a href="#/docs/known-issues">Known issues</a>
        <a :href="REPO_URL" target="_blank" rel="noopener">Source on GitHub</a>
        <a :href="`${REPO_URL}/issues`" target="_blank" rel="noopener">Report a problem</a>
        <a :href="COFFEE_URL" target="_blank" rel="noopener">Buy me a coffee</a>
      </nav>
    </div>
  </footer>
</template>
