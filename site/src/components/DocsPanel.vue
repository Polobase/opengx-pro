<script setup lang="ts">
import { computed } from 'vue'
import { REPO_URL } from '../lib/links'

/** The user guide: docs/guide/*.md of the repository, rendered when the site is built. */
const props = defineProps<{ page: string }>()

const modules = import.meta.glob<{ title: string; html: string }>('../../../docs/guide/*.md', { eager: true })
const order = ['features', 'compare', 'keys', 'install', 'safety', 'measurements', 'build', 'known-issues']
const pages = Object.entries(modules)
  .map(([path, m]) => ({ id: /([\w-]+)\.md$/.exec(path)![1]!, ...m }))
  .sort((a, b) => (order.indexOf(a.id) + 1 || 99) - (order.indexOf(b.id) + 1 || 99) || a.id.localeCompare(b.id))
const current = computed(() => pages.find((p) => p.id === props.page) ?? pages[0])
</script>

<template>
  <section class="panel docs">
    <nav aria-label="Guide">
      <a v-for="p in pages" :key="p.id" :href="`#/docs/${p.id}`" :class="{ active: current?.id === p.id }">{{ p.title }}</a>
    </nav>
    <!-- eslint-disable-next-line vue/no-v-html: the guide is this repository's own Markdown, rendered at build time -->
    <article v-if="current" class="prose" v-html="current.html"></article>
    <p v-else class="muted">The guide is not part of this build.</p>
    <p v-if="current" class="muted edit"><a :href="`${REPO_URL}/blob/main/docs/guide/${current.id}.md`" target="_blank" rel="noopener">This
      page on GitHub</a></p>
  </section>
</template>
