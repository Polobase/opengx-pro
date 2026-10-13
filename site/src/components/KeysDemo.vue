<script setup lang="ts">
import { ref } from 'vue'
import { screen } from '../lib/screens'
import UnitMock from './UnitMock.vue'

/** Press a key on the drawing and see the screen the firmware shows for it. A picture show, not an emulator. */
const steps: Record<string, { shot: string; says: string }> = {
  START: { shot: 'dash_running', says: 'START plays the program. Press it again to pause.' },
  STOP: { shot: 'dash_ready', says: 'STOP stops the program. Held for a second it stops both generators.' },
  LOOP: { shot: 'dash_loop', says: 'LOOP repeats the program until you stop it. Held, it shows every step.' },
  G1: { shot: 'library', says: 'G1 opens the program list for the first generator: UP and DOWN choose, START loads.' },
  G2: { shot: 'dash_g2', says: 'G2 brings the second generator to the front. The other one stays in the strip below.' },
  DOWN: { shot: 'menu', says: 'DOWN opens the menu: programs, chains, manual generator, timers, diagnostics, settings.' },
  UP: { shot: 'quick', says: 'UP opens the quick settings: brightness, night mode, key lock, screen off.' },
  POWER: { shot: 'goodbye', says: 'A short press switches the screen off and on. Held, POWER switches the unit off.' },
}
const key = ref('START')
const paused = ref(false)
function press(k: string) {
  if (k === 'START' && key.value === 'START') paused.value = !paused.value
  else paused.value = false
  key.value = k
}
</script>

<template>
  <div class="keysdemo">
    <UnitMock :src="screen(key === 'START' && paused ? 'dash_paused' : steps[key]!.shot)" :alt="steps[key]!.says" :lit="[key]" interactive
              @key="press" />
    <div class="keysdemo-text">
      <span class="eyebrow">Try it here</span>
      <h2>Eight keys. They still do what you know.</h2>
      <p class="lead">Press a key on the drawing. The pictures are real screens of the firmware.</p>
      <p class="says" aria-live="polite"><kbd>{{ key }}</kbd> {{ key === 'START' && paused ? 'Paused. START plays on, with the time left where it was.' : steps[key]!.says }}</p>
      <p class="muted">Prefer the keys exactly as they were? One switch in the settings, “Original key behaviour”, gives them back.
        <a href="#/docs/keys">All keys and screens</a></p>
    </div>
  </div>
</template>
