<script setup lang="ts">
/**
 * A drawing of the unit's front: the screen with one of the firmware's real screens, and the eight
 * keys (laid out as on the firmware's key test). With `interactive` the keys can be pressed.
 */
defineProps<{ src: string; alt: string; lit?: string[]; interactive?: boolean; label?: string }>()
defineEmits<{ key: [name: string] }>()
const rows = [['G1', 'G2', 'POWER'], ['UP', 'DOWN'], ['LOOP', 'START', 'STOP']]
</script>

<template>
  <div class="unit" :class="{ interactive }">
    <div class="unit-head"><span class="led"></span><span>GeneratorX Pro</span><em>{{ label ?? 'OpenGX Pro' }}</em></div>
    <div class="unit-screen">
      <Transition name="swap">
        <img :key="src" :src="src" :alt="alt" width="320" height="240" />
      </Transition>
      <i class="glare" aria-hidden="true"></i>
    </div>
    <div class="unit-keys" :aria-hidden="!interactive">
      <div v-for="(row, r) in rows" :key="r" class="unit-row">
        <button v-for="k in row" :key="k" type="button" :class="{ lit: lit?.includes(k), power: k === 'POWER' }"
                :tabindex="interactive ? 0 : -1" :disabled="!interactive && undefined" @click="interactive && $emit('key', k)">{{ k }}</button>
      </div>
    </div>
  </div>
</template>
