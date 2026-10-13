<script setup lang="ts">
import { rowCount } from '../lib/matrix'
import ComparisonMatrix from './ComparisonMatrix.vue'
</script>

<template>
  <section class="panel">
    <ComparisonMatrix full />
    <div class="after-matrix">
      <p><b>How sure is this?</b> “Same as before” means the same behaviour by design, checked by automated tests and
        against the original firmware on an emulated unit. On a real unit so far: installing, starting, and Spooky2
        running programs on both generators. What is still open is in <a href="#/docs/known-issues">Known issues</a>.</p>
      <p><b>More detail.</b> The same {{ rowCount }} points with their technical background are in the guide:
        <a href="#/docs/compare">side by side</a>. Every deliberate difference, with the place in the code, is in
        <a href="https://github.com/Polobase/opengx-pro/blob/main/DEVIATIONS.md" target="_blank" rel="noopener">DEVIATIONS.md</a>.</p>
    </div>
  </section>
</template>
