<script setup lang="ts">
/** The mark of the firmware's start screen: a ring with a sine in it. */
defineProps<{ size?: number }>()
</script>

<template>
  <svg class="mark" :width="size ?? 28" :height="size ?? 28" viewBox="0 0 32 32" aria-hidden="true">
    <circle cx="16" cy="16" r="11.5" fill="none" stroke="currentColor" stroke-width="4.2" />
    <path d="M9.6 16c1.6-4.6 3.2-4.6 4.8 0s3.2 4.6 4.8 0 1.9-3.2 3.2-1.2" fill="none" stroke="#ff4d4d" stroke-width="1.9"
          stroke-linecap="round" />
  </svg>
</template>
