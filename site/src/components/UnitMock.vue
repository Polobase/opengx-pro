<script setup lang="ts">
/**
 * A drawing of the unit's front: the screen on the left, the eight keys on the right, where they
 * are on the real unit (Stop and Power on top, the up/down rocker beside the G1/G2 rocker, Start and
 * Loop below). The screen shows one of the firmware's real screens, or whatever the `screen` slot
 * holds. With `interactive` the keys can be pressed.
 */
defineProps<{ src?: string; alt?: string; lit?: string[]; held?: string[]; interactive?: boolean; small?: boolean }>()
defineEmits<{ key: [name: string] }>()
const keys = [
  { id: 'STOP', label: 'Stop', cls: 'k-stop' },
  { id: 'POWER', label: '', cls: 'k-power' },
  { id: 'UP', label: '▲', cls: 'k-up' },
  { id: 'G1', label: 'G1', cls: 'k-g1' },
  { id: 'DOWN', label: '▼', cls: 'k-down' },
  { id: 'G2', label: 'G2', cls: 'k-g2' },
  { id: 'START', label: 'Start', cls: 'k-start' },
  { id: 'LOOP', label: 'Loop', cls: 'k-loop' },
]
</script>

<template>
  <div class="unit" :class="{ interactive, small }">
    <div class="unit-name">GeneratorX <b>Pro</b></div>
    <div class="unit-face">
      <div class="unit-screen">
        <slot name="screen">
          <Transition name="swap">
            <img :key="src" :src="src" :alt="alt" width="320" height="240" />
          </Transition>
        </slot>
        <i class="glare" aria-hidden="true"></i>
      </div>
      <div class="unit-pad" :aria-hidden="!interactive">
        <button v-for="k in keys" :key="k.id" type="button" :class="[k.cls, { lit: lit?.includes(k.id), held: held?.includes(k.id) }]"
                :tabindex="interactive ? 0 : -1" :aria-label="k.id" @click="interactive && $emit('key', k.id)">
          <svg v-if="k.id === 'POWER'" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3.5v8M7.2 6.6a7.2 7.2 0 1 0 9.6 0" /></svg>
          <template v-else>{{ k.label }}</template>
        </button>
      </div>
    </div>
  </div>
</template>
