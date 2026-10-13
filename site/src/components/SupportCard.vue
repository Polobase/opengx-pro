<script setup lang="ts">
import qr from '../assets/qr-support.png'
import { COFFEE_URL } from '../lib/links'
</script>

<template>
  <section class="panel support">
    <img :src="qr" alt="QR code of buymeacoffee.com/mahapo" width="148" height="148" />
    <div>
      <h2>Support the project</h2>
      <p>OpenGX Pro is free and made in spare time. If it is useful to you, a coffee keeps it going. The same code is on
        the unit under Settings → About → Support the project.</p>
      <p><a class="button primary" :href="COFFEE_URL" target="_blank" rel="noopener">Buy me a coffee</a></p>
    </div>
  </section>
</template>
