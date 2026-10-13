<script setup lang="ts">
import { ref } from 'vue'
import { extractFromUpdater, parseImage, type FirmwareImage } from '../lib/image'
import { VENDOR_DOWNLOADS_URL } from '../lib/links'
import FlashFlow from './FlashFlow.vue'

/** SHA-256 of the vendor's display firmware v7789 (GxDisplayFW.bin inside GX_Pro_Firmware_v7789.exe). */
const V7789_SHA256 = '69455ac4327fc1c51a99054eaa56c6fc48346a60132c8dfaea74a2899c59d894'

const image = ref<FirmwareImage | null>(null)
const fileName = ref('')
const error = ref('')

async function pick(e: Event) {
  error.value = ''
  image.value = null
  const file = (e.target as HTMLInputElement).files?.[0]
  if (!file) return
  fileName.value = file.name
  try {
    let bytes = new Uint8Array(await file.arrayBuffer())
    if (bytes[0] === 0x4d && bytes[1] === 0x5a) bytes = new Uint8Array(extractFromUpdater(bytes)) // "MZ": the updater .exe
    image.value = await parseImage(bytes)
  } catch (err) {
    error.value = err instanceof Error ? err.message : String(err)
  }
}
</script>

<template>
  <section class="panel">
    <div class="intro single">
      <div>
        <h2>Choose Spooky2's updater file</h2>
        <p class="lead">The original firmware belongs to Spooky2 and is not part of this site. Take the vendor's own updater,
          <b>GX_Pro_Firmware_v7789.exe</b>, from <a :href="VENDOR_DOWNLOADS_URL" target="_blank" rel="noopener">Spooky2's download
          page</a> (unpack the .zip), and choose it here. The page takes the firmware out of the updater in your browser and
          installs it the same way; nothing is uploaded. On Windows the vendor's updater itself works just as well.</p>
        <label class="file">
          <input type="file" accept=".exe,.bin" @change="pick" />
          <span>Choose file…</span>
          <em v-if="fileName">{{ fileName }}</em>
        </label>
        <p v-if="error" class="note err" role="alert">{{ error }}</p>
        <dl v-if="image" class="facts">
          <div><dt>Firmware</dt><dd>{{ image.sha256 === V7789_SHA256 ? 'Original display firmware v7789' : 'A display firmware file' }}
            <span class="muted">(ID {{ image.id }})</span></dd></div>
          <div><dt>Update file</dt><dd>{{ image.bytes.length.toLocaleString('en') }} bytes, {{ image.blocks }} blocks</dd></div>
          <div><dt>SHA-256</dt><dd class="mono">{{ image.sha256.slice(0, 16) }}…
            <span v-if="image.sha256 === V7789_SHA256" class="ok-text">matches the original v7789</span></dd></div>
        </dl>
        <p v-if="image && image.sha256 !== V7789_SHA256" class="note warn">This is not the original v7789 this page knows.
          It has the display board's firmware ID and a size the unit takes, so it can be installed: do so only if you know
          what the file is, for example your own build.</p>
        <p class="muted">You can also choose the image inside the updater, <b>GxDisplayFW.bin</b>, or an update file you
          built yourself (<a href="#/docs/build">Build from source</a>).</p>
      </div>
    </div>
    <FlashFlow :image="image" what="this firmware"
               :missing="error ? 'That file cannot be installed: see above.' : 'Choose the file above first.'"
               after="The unit starts with the firmware you chose." />
  </section>
</template>
