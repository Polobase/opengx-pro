<script setup lang="ts">
import { ref } from 'vue'

/**
 * Shown where the browser has no Web Serial: what to do about it, as far as the page can tell which
 * browser this is. A current Brave has Web Serial (seen in 1.97); older ones had it behind a flag.
 */
type Kind = 'phone' | 'brave' | 'other'
const ua = navigator.userAgent
const touchMac = /Mac/i.test(ua) && navigator.maxTouchPoints > 1 // an iPad says it is a Mac
const kind: Kind = /Android|iPhone|iPad|iPod|Mobile/i.test(ua) || touchMac ? 'phone' : 'brave' in navigator ? 'brave' : 'other'

const BRAVE_FLAG = 'brave://flags/#brave-web-serial-api'
const copied = ref(false)
async function copy() {
  try {
    await navigator.clipboard.writeText(BRAVE_FLAG)
    copied.value = true
    setTimeout(() => (copied.value = false), 2000)
  } catch {
    /* the address is on the page and can be selected by hand */
  }
}
</script>

<template>
  <div class="note err browser-help" role="alert">
    <template v-if="kind === 'phone'">
      <p><b>Phones and tablets cannot do this.</b> Open this page on a desktop or laptop computer, in Chrome, Edge,
        Opera or Brave.</p>
    </template>
    <template v-else-if="kind === 'brave'">
      <p><b>This Brave has the USB connection switched off.</b> Current versions of Brave have it on, so updating Brave
        (menu → About Brave) is the easy way. To switch it on by hand:</p>
      <ol>
        <li>Copy this address into the address bar and press Enter: <code>{{ BRAVE_FLAG }}</code>
          <button type="button" class="copy" @click="copy">{{ copied ? 'Copied' : 'Copy' }}</button></li>
        <li>At <b>Web Serial API</b>, choose <b>Enabled</b>. If the entry is not shown, type “serial” into the search
          field of that page.</li>
        <li>Click <b>Relaunch</b>. Brave restarts and opens this page again.</li>
      </ol>
      <p>Or open this page in Chrome, Edge or Opera.</p>
    </template>
    <template v-else>
      <p><b>This browser cannot talk to USB devices.</b> Firefox and Safari cannot. Please open this page in
        <b>Chrome</b>, <b>Edge</b>, <b>Opera</b> or <b>Brave</b> on a desktop or laptop computer.</p>
    </template>
  </div>
</template>
