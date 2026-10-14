<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import type { FirmwareImage } from '../lib/image'
import { loadFirmware, loadReleases, preferred, type Release } from '../lib/releases'
import { REPO_URL, VENDOR_DOWNLOADS_URL } from '../lib/links'
import FlashFlow from './FlashFlow.vue'

const releases = ref<Release[] | null>(null)
const chosen = ref<Release | undefined>()
const image = ref<FirmwareImage | null>(null)
const error = ref('')

onMounted(async () => {
  try {
    releases.value = await loadReleases()
    chosen.value = preferred(releases.value)
  } catch (e) {
    error.value = e instanceof Error ? e.message : String(e)
  }
})

// The chosen version is downloaded from this site and checked against its SHA-256 before it can be installed.
watch(chosen, async (release) => {
  image.value = null
  error.value = ''
  if (!release) return
  try {
    const loaded = await loadFirmware(release)
    if (chosen.value === release) image.value = loaded
  } catch (e) {
    if (chosen.value === release) error.value = e instanceof Error ? e.message : String(e)
  }
})

/** Why "Find the unit" is greyed out while there is no checked file. */
const missing = computed(() => {
  if (error.value) return 'The version could not be loaded: see “Choose a version” above.'
  if (!releases.value) return 'Loading the list of versions…'
  if (!releases.value.length) return 'There is nothing to install yet: the first version is not published (see “Choose a version” above).'
  return 'The file of this version is being checked…'
})
</script>

<template>
  <section class="panel">
    <div class="intro single">
      <div>
        <h2>Before you start</h2>
        <ul class="before">
          <li><span><b>A desktop or laptop computer</b> with Chrome, Edge, Opera or Brave. Phones and tablets cannot do this.</span></li>
          <li><span><b>The unit's USB cable,</b> plugged into the computer.</span></li>
          <li><span><b>Spooky2 closed</b> while you install, so that it does not use the cable at the same time.</span></li>
          <li><span><b>For the way back:</b> get <a :href="VENDOR_DOWNLOADS_URL" target="_blank" rel="noopener">Spooky2's updater
            file</a> now, then you have it when you want it.</span></li>
        </ul>
        <p class="note warn"><b>Only for the GeneratorX Pro.</b> The original GeneratorX looks the same from the front, but it
          differs inside (Spooky2 treats it as another generator), and OpenGX Pro is neither made nor tested for it. Please do
          not install it there.</p>
        <p class="lead">Only the software of the screen and keys is replaced. Your programs, their names and the calibration
          stay, and Spooky2 works as before.</p>
        <h2>Choose a version</h2>
        <template v-if="releases && releases.length">
          <label class="pick">
            <span>Version</span>
            <select v-model="chosen">
              <option v-for="r in releases" :key="r.version" :value="r">
                {{ r.version }}{{ r.channel === 'beta' ? ' (beta)' : '' }} · {{ r.date }}
              </option>
            </select>
          </label>
          <dl v-if="chosen" class="facts">
            <div><dt>Version</dt><dd>{{ chosen.version }}
              <span v-if="chosen.channel === 'beta'" class="badge">beta</span>
              <span class="muted"> (build {{ chosen.commit }}, {{ chosen.date }})</span>
              <template v-if="chosen.notes"> · <a :href="chosen.notes" target="_blank" rel="noopener">release notes</a></template></dd></div>
            <div><dt>Update file</dt><dd>{{ chosen.bytes.toLocaleString('en') }} bytes, {{ chosen.blocks }} blocks</dd></div>
            <div><dt>SHA-256</dt><dd class="mono">{{ chosen.sha256.slice(0, 16) }}…
              <span v-if="image" class="ok-text">checked</span><span v-else-if="!error" class="muted">checking…</span></dd></div>
          </dl>
          <p v-if="chosen?.channel === 'beta'" class="note warn"><b>Beta.</b> Not everything has been checked on a real unit
            yet: see <a href="#/docs/known-issues">Known issues</a>.</p>
        </template>
        <p v-else-if="releases" class="note warn"><b>The first version is not published yet.</b> It is being checked on a
          real unit and will appear here when that is done. If you build the firmware yourself
          (<a href="#/docs/build">Build from source</a>), the <a href="#/restore">Go back</a> page installs your file.</p>
        <p v-else-if="!error" class="muted">Loading the list of versions…</p>
        <p v-if="error" class="note err" role="alert">{{ error }}</p>
        <p class="muted">All versions and their notes: <a :href="`${REPO_URL}/releases`" target="_blank" rel="noopener">releases
          on GitHub</a>. Installing an older version works the same way.</p>
      </div>
    </div>
    <FlashFlow :image="image" what="this version" :missing="missing"
               after="The start screen shows “OpenGX Pro” and the version; at the first start the key guide follows." />
  </section>
</template>
