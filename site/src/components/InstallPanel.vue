<script setup lang="ts">
import { onMounted, ref, watch } from 'vue'
import type { FirmwareImage } from '../lib/image'
import { loadFirmware, loadReleases, preferred, type Release } from '../lib/releases'
import { REPO_URL } from '../lib/links'
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
</script>

<template>
  <section class="panel">
    <div class="intro single">
      <div>
        <h2>Install OpenGX Pro</h2>
        <p class="lead">It replaces the display firmware only. Programs, names and calibration stay, Spooky2 on the PC works
          as before, and the original firmware can be put back at any time on the
          <a href="#/restore">Restore original</a> page.</p>
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
        <p v-else-if="releases" class="note warn">No version is published yet. You can build the firmware yourself
          (<a href="#/docs/build">Build from source</a>) and install your file on the
          <a href="#/restore">Restore original</a> page, which takes any display firmware file.</p>
        <p v-else-if="!error" class="muted">Loading the list of versions…</p>
        <p v-if="error" class="note err" role="alert">{{ error }}</p>
        <p class="muted">All versions and their notes: <a :href="`${REPO_URL}/releases`" target="_blank" rel="noopener">releases
          on GitHub</a>. Installing an older version works the same way.</p>
      </div>
    </div>
    <FlashFlow :image="image" what="this version"
               after="The start screen shows “OpenGX Pro” and the version; at the first start the key guide follows." />
  </section>
</template>
