<script setup lang="ts">
defineProps<{ purpose: 'update' | 'demo' }>()

type Os = 'mac' | 'windows' | 'other'
const ua = `${navigator.platform ?? ''} ${navigator.userAgent}`
const os: Os = /Mac/i.test(ua) ? 'mac' : /Win/i.test(ua) ? 'windows' : 'other'
</script>

<template>
  <div class="porthint">
    <template v-if="os === 'windows'">
      <p><b>Which entry to choose:</b> the unit shows up as two ports named “USB Dual_Serial”. Choose the
        <b>second one, with the higher COM number</b>, for example <code>COM4</code> when <code>COM3</code> and
        <code>COM4</code> are listed.</p>
    </template>
    <template v-else-if="os === 'mac'">
      <p><b>Which entry to choose:</b> the unit has two USB channels, and a Mac lists each of them twice (two drivers).
        Choose the entry that ends in <b class="nowrap">…903</b>: <code>cu.wchusbserial…903</code>
        <span class="muted">(or</span> <code>cu.usbmodem…903</code><span class="muted">)</span>.</p>
    </template>
    <template v-else>
      <p><b>Which entry to choose:</b> the unit shows up as two serial ports. Choose the <b>second</b> of the two.</p>
    </template>
    <p v-if="purpose === 'update'" class="muted">If the page says it is the unit's other channel (it answers “X1”), click
      <b>Choose another port</b> and take the other entry. After you have chosen once, the page finds the unit by itself.</p>
    <p v-else class="muted">In normal operation both ports reach the display; the one above is the port for G1.</p>
  </div>
</template>
