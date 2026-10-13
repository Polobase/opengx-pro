<script setup lang="ts">
defineProps<{ purpose: 'update' | 'demo' }>()

// How the two ports are named differs from unit to unit and from computer to computer, so the hint
// names no entry: the page finds out which one it is (the hardware ID) before anything is written.
type Os = 'mac' | 'windows' | 'other'
const ua = `${navigator.platform ?? ''} ${navigator.userAgent}`
const os: Os = /Mac/i.test(ua) ? 'mac' : /Win/i.test(ua) ? 'windows' : 'other'
</script>

<template>
  <div class="porthint">
    <p><b>Which entry to choose:</b> the unit shows up as two USB ports<template v-if="os === 'windows'">, named
      “USB Dual_Serial”, each with its own COM number</template><template v-else-if="os === 'mac'">; a Mac may list
      each of them twice</template>. The names are different on every unit and computer.</p>
    <template v-if="purpose === 'update'">
      <p><b>Choose one of them.</b> Only one of the two is for updating. If you took the other one, the page tells you
        and nothing is written: click <b>Choose another port</b> and take a different entry.</p>
      <p class="muted">After you have chosen once, the page finds the unit by itself.</p>
    </template>
    <p v-else><b>Choose one of them.</b> If there is no answer, click <b>Connect</b> again and take a different
      entry.</p>
  </div>
</template>
