<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref } from 'vue'
import UnitMock from './UnitMock.vue'

/**
 * How to put the unit into update mode, as a moving drawing: the unit is off, Stop is held, Power
 * is pressed, the unit waits for an update. Without motion (system setting) the last picture stands.
 */
const steps = ['The unit is off', 'Hold Stop …', '… and press Power', 'The unit waits for the update']
const reduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches
const phase = ref(reduced ? 3 : 0)
let timer = 0
onMounted(() => {
  if (!reduced) timer = window.setInterval(() => (phase.value = (phase.value + 1) % 5), 1500)
})
onBeforeUnmount(() => window.clearInterval(timer))
</script>

<template>
  <figure class="updfig" aria-label="Hold Stop, then press Power">
    <UnitMock small :held="phase === 1 || phase === 2 ? ['STOP'] : []" :lit="phase === 2 ? ['POWER'] : phase === 1 ? ['STOP'] : []">
      <template #screen>
        <div class="updscreen" :class="{ on: phase >= 3 }">
          <span>*** Firmware Update ***</span>
          <span>Press &lt;Start&gt; to Exit!</span>
        </div>
      </template>
    </UnitMock>
    <ol>
      <li v-for="(s, i) in steps" :key="s" :class="{ on: Math.min(phase, 3) === i }">{{ s }}</li>
    </ol>
  </figure>
</template>
