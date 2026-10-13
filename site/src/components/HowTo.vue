<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { topics } from '../lib/howto'
import { screen } from '../lib/screens'
import UnitMock from './UnitMock.vue'

/** How to use the unit: choose a topic, go through its steps. The lit keys are the ones to press; pressing one goes on. */
const topicId = ref(topics[0]!.id)
const index = ref(0)
const topic = computed(() => topics.find((t) => t.id === topicId.value)!)
const step = computed(() => topic.value.steps[index.value]!)
watch(topicId, () => (index.value = 0))
const last = computed(() => index.value === topic.value.steps.length - 1)

function next() {
  if (!last.value) index.value += 1
  else {
    const i = topics.findIndex((t) => t.id === topicId.value)
    topicId.value = topics[(i + 1) % topics.length]!.id
  }
}
function back() {
  if (index.value > 0) index.value -= 1
}
function press(key: string) {
  if (step.value.keys.includes(key)) next()
}
function onKey(e: KeyboardEvent) {
  if ((e.target as HTMLElement | null)?.closest('input, select, textarea')) return
  if (e.key === 'ArrowRight') next()
  else if (e.key === 'ArrowLeft') back()
}
onMounted(() => window.addEventListener('keydown', onKey))
onBeforeUnmount(() => window.removeEventListener('keydown', onKey))
</script>

<template>
  <div class="howto">
    <h1>How to use it</h1>
    <p class="lead">Choose what you want to do and go through it step by step. The lit key is the one to press; the screen
      shows what the unit then shows. The pictures are real screens of the firmware.</p>
    <div class="chips" role="tablist" aria-label="What do you want to do?">
      <button v-for="t in topics" :key="t.id" type="button" role="tab" class="chip" :class="{ on: t.id === topicId }"
              :aria-selected="t.id === topicId" @click="topicId = t.id">{{ t.title }}</button>
    </div>
    <div class="howto-stage">
      <ol class="howto-steps">
        <li v-for="(s, i) in topic.steps" :key="i" :class="{ on: i === index, done: i < index }">
          <button type="button" :aria-current="i === index ? 'step' : undefined" @click="index = i">
            <span class="num">{{ i + 1 }}</span>
            <span class="body"><b>{{ s.does }}</b><span class="text">{{ s.see }}</span></span>
          </button>
        </li>
      </ol>
      <div class="howto-unit">
        <UnitMock :src="screen(step.shot)" :alt="step.see" :lit="step.keys" :held="step.hold ? step.keys : []" interactive @key="press" />
        <p class="howto-now" aria-live="polite">
          <template v-if="step.keys.length">
            <span>{{ step.hold ? 'Hold' : 'Press' }}</span>
            <template v-for="(k, i) in step.keys" :key="k">
              <i v-if="i">{{ step.together ? '+' : 'or' }}</i><kbd>{{ { UP: '▲', DOWN: '▼', POWER: 'Power', START: 'Start', STOP: 'Stop', LOOP: 'Loop' }[k] ?? k }}</kbd>
            </template>
          </template>
          <span v-else>{{ step.does }}</span>
        </p>
        <div class="howto-nav">
          <button type="button" :disabled="index === 0" @click="back">Back</button>
          <span>{{ index + 1 }} / {{ topic.steps.length }}</span>
          <button type="button" class="primary" @click="next">{{ last ? 'Next topic' : 'Next' }}</button>
        </div>
      </div>
    </div>
    <p class="muted">Every key on every screen: <a href="#/docs/keys">Keys and screens</a>. The program names in the pictures
      are examples.</p>
  </div>
</template>
