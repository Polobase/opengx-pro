<script setup lang="ts">
import { computed, onBeforeUnmount, ref } from 'vue'
import { BytePort, webSerialSupported } from '../lib/byteport'
import { flashImage, readHardwareId } from '../lib/flasher'
import { describeHardwareId, type FirmwareImage } from '../lib/image'
import { FEEDBACK_URL } from '../lib/links'
import PortHint from './PortHint.vue'
import UpdateModeFigure from './UpdateModeFigure.vue'

/** `missing`: why there is no image yet, said where the greyed-out button is. */
const props = defineProps<{ image: FirmwareImage | null; what: string; after: string; missing: string }>()
const supported = webSerialSupported()

type Status = 'idle' | 'connecting' | 'ready' | 'wrong' | 'noanswer' | 'flashing' | 'done' | 'error'
const status = ref<Status>('idle')
const hw = ref(0)
const done = ref(0)
const total = ref(0)
const log = ref<string[]>([])
const error = ref('')
let port: BytePort | null = null

const pct = computed(() => (total.value ? Math.round((100 * done.value) / total.value) : 0))

function warnOnLeave(e: BeforeUnloadEvent) {
  e.preventDefault()
}

async function release() {
  const p = port
  port = null
  if (p) await p.close().catch(() => undefined)
}

/** A port the user allowed before that answers with the image's hardware ID, opened; else null. */
async function findGranted(): Promise<BytePort | null> {
  if (!props.image) return null
  for (const candidate of await BytePort.granted()) {
    try {
      await candidate.open()
      const id = await readHardwareId(candidate)
      if (id === props.image.hardwareId) {
        hw.value = id
        return candidate
      }
    } catch {
      /* busy or gone: try the next one */
    }
    await candidate.close().catch(() => undefined)
  }
  return null
}

async function connect(choose = false) {
  error.value = ''
  log.value = []
  await release()
  status.value = 'connecting'
  try {
    port = choose ? null : await findGranted()
    if (port) {
      status.value = 'ready'
      return
    }
    port = await BytePort.request()
    await port.open()
    hw.value = await readHardwareId(port)
    if (hw.value === 0) status.value = 'noanswer'
    else if (props.image && hw.value !== props.image.hardwareId) status.value = 'wrong'
    else status.value = 'ready'
    if (status.value !== 'ready') await release()
  } catch (e) {
    await release()
    const msg = e instanceof Error ? e.message : String(e)
    // Closing the port chooser is not an error.
    if (/No port selected/i.test(msg)) status.value = 'idle'
    else {
      error.value = msg
      status.value = 'error'
    }
  }
}

async function install() {
  if (!port || !props.image) return
  status.value = 'flashing'
  done.value = 0
  total.value = props.image.blocks
  window.addEventListener('beforeunload', warnOnLeave)
  try {
    await flashImage(port, props.image, {
      log: (m) => log.value.push(m),
      progress: (d, t) => {
        done.value = d
        total.value = t
      },
    })
    status.value = 'done'
  } catch (e) {
    error.value = e instanceof Error ? e.message : String(e)
    status.value = 'error'
  } finally {
    window.removeEventListener('beforeunload', warnOnLeave)
    await release()
  }
}

onBeforeUnmount(release)
</script>

<template>
  <ol class="steps">
    <li :class="{ current: status === 'idle' || status === 'noanswer' }">
      <div class="step-fig">
        <div>
          <h3>Put the unit into update mode</h3>
          <p>Connect the unit to this computer with its USB cable. Then:</p>
          <ol class="mini">
            <li><b>Switch the unit off:</b> hold <kbd>Power</kbd> for two seconds.</li>
            <li><b>Hold <kbd>Stop</kbd></b> and keep it held.</li>
            <li><b>Press <kbd>Power</kbd></b>, then let both go.</li>
          </ol>
          <p>The screen shows “Firmware Update” (on some units it stays dark). The unit now waits for the new software.
            Changed your mind? <kbd>Start</kbd> leaves update mode; nothing has been changed.</p>
        </div>
        <UpdateModeFigure />
      </div>
    </li>
    <li :class="{ current: ['idle', 'connecting', 'wrong', 'noanswer', 'ready'].includes(status) }">
      <h3>Choose the USB port</h3>
      <PortHint purpose="update" />
      <div class="row">
        <button class="primary" :disabled="!supported || !image || status === 'connecting' || status === 'flashing'" @click="connect()">
          {{ status === 'connecting' ? 'Looking for the unit…' : 'Find the unit' }}
        </button>
        <button v-if="status === 'wrong' || status === 'noanswer' || status === 'ready'" @click="connect(true)">Choose another port</button>
      </div>
      <p v-if="!supported" class="note warn">This browser cannot connect to the unit: see the note at the top of this
        page.</p>
      <p v-else-if="!image" class="note warn">{{ missing }}</p>
      <p v-else-if="status === 'ready'" class="note ok">Unit in update mode found (hardware ID {{ describeHardwareId(hw) }}).</p>
      <p v-else-if="status === 'wrong'" class="note warn">That is the unit's other channel (it answered
        “{{ describeHardwareId(hw) }}”; the display answers “{{ image ? describeHardwareId(image.hardwareId) : '' }}”).
        Click <b>Choose another port</b> and take the other entry (on Windows the higher COM number, on a Mac the one ending
        in …903).</p>
      <p v-else-if="status === 'noanswer'" class="note warn">No answer. Is the unit in update mode (switch off, hold STOP,
        press POWER)? If it is, click <b>Choose another port</b> and take the other entry.</p>
    </li>
    <li :class="{ current: status === 'ready' || status === 'flashing' }">
      <h3>Install {{ what }}</h3>
      <p>Takes about a minute. Keep the USB cable connected and this page open until it says “Done”.</p>
      <button class="primary" :disabled="status !== 'ready'" @click="install">Install {{ what }}</button>
      <div v-if="status === 'flashing' || status === 'done' || (status === 'error' && total)" class="progress"
           role="progressbar" :aria-valuenow="pct" aria-valuemin="0" aria-valuemax="100">
        <div class="bar" :style="{ width: pct + '%' }"></div>
        <span>{{ done }} / {{ total }} blocks</span>
      </div>
    </li>
    <li :class="{ current: status === 'done' }">
      <h3>Restart</h3>
      <p v-if="status === 'done'" class="note ok"><b>Done.</b> The unit's screen may show “OK!”. Switch the unit off and on
        again. {{ after }} <a :href="FEEDBACK_URL" target="_blank" rel="noopener">Tell us how it went</a>.</p>
      <p v-else>Switch the unit off and on again. {{ after }}</p>
    </li>
  </ol>
  <p v-if="status === 'error'" class="note err" role="alert">{{ error }}</p>
  <details v-if="log.length" class="log">
    <summary>Details</summary>
    <pre>{{ log.join('\n') }}</pre>
  </details>
</template>
