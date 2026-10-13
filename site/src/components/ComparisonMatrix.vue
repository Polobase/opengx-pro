<script setup lang="ts">
import { computed, ref } from 'vue'
import { glance, groups, type Cell, type Row } from '../lib/matrix'

/** The original firmware and OpenGX Pro side by side: the short table, or the complete list with filters. */
const props = defineProps<{ full?: boolean }>()

type Kind = 'all' | 'new' | 'same' | 'different' | 'own'
const kindOf = (r: Row): Exclude<Kind, 'all'> =>
  r.ours === 'own' ? 'own' : r.original === true && r.ours === true ? 'same' : r.original === false && r.ours === true ? 'new' : 'different'

const filters: Array<{ id: Kind; label: string }> = [
  { id: 'all', label: 'Everything' },
  { id: 'new', label: 'New' },
  { id: 'same', label: 'Same as before' },
  { id: 'different', label: 'Different' },
  { id: 'own', label: 'Own build' },
]
const filter = ref<Kind>('all')
const source = props.full ? groups : glance
const count = (k: Kind) => source.reduce((n, g) => n + g.rows.filter((r) => k === 'all' || kindOf(r) === k).length, 0)
const shown = computed(() => source
  .map((g) => ({ ...g, rows: g.rows.filter((r) => filter.value === 'all' || kindOf(r) === filter.value) }))
  .filter((g) => g.rows.length))
const isText = (c: Cell) => typeof c === 'string' && c !== 'own'
const hasOwn = source.some((g) => g.rows.some((r) => r.ours === 'own'))
</script>

<template>
  <div class="matrix" :class="{ full }">
    <div v-if="full" class="chips" role="group" aria-label="Show">
      <button v-for="f in filters" :key="f.id" class="chip" :class="{ on: filter === f.id }" :aria-pressed="filter === f.id"
              @click="filter = f.id">{{ f.label }} <span>{{ count(f.id) }}</span></button>
    </div>
    <div class="matrix-scroll">
      <table>
        <thead>
          <tr><th scope="col">Feature</th><th scope="col">Original</th><th scope="col" class="ours">OpenGX Pro</th></tr>
        </thead>
        <tbody v-for="g in shown" :key="g.id">
          <tr class="group"><th colspan="3" scope="colgroup">{{ g.title }}<small v-if="g.intro">{{ g.intro }}</small></th></tr>
          <tr v-for="r in g.rows" :key="r.feature">
            <th scope="row">{{ r.feature }}<small v-if="full && r.note">{{ r.note }}</small></th>
            <td v-for="(c, i) in [r.original, r.ours]" :key="i" :class="{ ours: i === 1, text: isText(c) }">
              <span v-if="c === true" class="yes" role="img" aria-label="yes">
                <svg viewBox="0 0 20 20" aria-hidden="true"><path d="M4.5 10.5l3.6 3.6 7.4-8.2" /></svg>
              </span>
              <span v-else-if="c === false" class="no" role="img" aria-label="no">–</span>
              <a v-else-if="c === 'own'" class="own" href="#/docs/build" title="Only in a firmware you build yourself">own build</a>
              <template v-else>{{ c }}</template>
            </td>
          </tr>
        </tbody>
      </table>
    </div>
    <p class="legend">
      <span class="yes"><svg viewBox="0 0 20 20" aria-hidden="true"><path d="M4.5 10.5l3.6 3.6 7.4-8.2" /></svg></span> yes
      <span class="no">–</span> not there, or not visible from outside
      <template v-if="hasOwn"><span class="own">own build</span> only in a firmware you build yourself</template>
    </p>
  </div>
</template>
