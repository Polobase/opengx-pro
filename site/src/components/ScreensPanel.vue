<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref } from 'vue'
import { gallery, screen, type Shot } from '../lib/screens'

/** Every screen of the firmware, grouped. A click shows one large; the arrow keys move on. */
const all = gallery.flatMap((g) => g.shots)
const open = ref<Shot | null>(null)
function step(d: number) {
  if (!open.value) return
  open.value = all[(all.indexOf(open.value) + d + all.length) % all.length]!
}
function onKey(e: KeyboardEvent) {
  if (!open.value) return
  if (e.key === 'Escape') open.value = null
  else if (e.key === 'ArrowRight') step(1)
  else if (e.key === 'ArrowLeft') step(-1)
}
onMounted(() => window.addEventListener('keydown', onKey))
onBeforeUnmount(() => window.removeEventListener('keydown', onKey))
</script>

<template>
  <section class="panel screens">
    <nav class="chips" aria-label="Groups">
      <a v-for="g in gallery" :key="g.id" class="chip" :href="`#/screens/${g.id}`"
         @click.prevent="$el.querySelector(`#shots-${g.id}`)?.scrollIntoView({ behavior: 'smooth', block: 'start' })">{{ g.title }}
        <span>{{ g.shots.length }}</span></a>
    </nav>
    <div v-for="g in gallery" :id="`shots-${g.id}`" :key="g.id" class="shot-group">
      <h2>{{ g.title }}</h2>
      <div class="shot-grid">
        <button v-for="s in g.shots" :key="s.name" type="button" class="shot" @click="open = s">
          <img :src="screen(s.name)" :alt="s.caption" width="320" height="240" loading="lazy" />
          <span>{{ s.caption }}</span>
        </button>
      </div>
    </div>
    <p class="muted">Each picture is drawn by the firmware's own screen code from sample data, 320 × 240 pixels, as on the
      unit. The program names and numbers are examples.</p>
    <Teleport to="body">
      <div v-if="open" class="lightbox" role="dialog" aria-modal="true" :aria-label="open.caption" @click.self="open = null">
        <button type="button" class="lb-nav prev" aria-label="Previous" @click="step(-1)">‹</button>
        <figure>
          <img :src="screen(open.name)" :alt="open.caption" width="320" height="240" />
          <figcaption>{{ open.caption }}</figcaption>
        </figure>
        <button type="button" class="lb-nav next" aria-label="Next" @click="step(1)">›</button>
        <button type="button" class="lb-close" aria-label="Close" @click="open = null">×</button>
      </div>
    </Teleport>
  </section>
</template>
