<script setup lang="ts">
import { VENDOR_DOWNLOADS_URL } from '../lib/links'
</script>

<template>
  <div class="faq">
    <details>
      <summary>What is “firmware”, and what does OpenGX Pro replace?</summary>
      <p>Firmware is the software inside a device. Your GeneratorX Pro has several parts; OpenGX Pro replaces the
        software of one of them: the part that runs the <b>screen and the keys</b>. The two generators inside the unit
        keep their own software, untouched.</p>
    </details>
    <details>
      <summary>Do I lose my programs or my calibration?</summary>
      <p>No. Your 30 programs, their names and the calibration are stored in a separate memory chip. Installing OpenGX
        Pro, updating it or going back to the original does not touch them.</p>
    </details>
    <details>
      <summary>Does Spooky2 on my computer still work?</summary>
      <p>Yes, as before. OpenGX Pro answers Spooky2 with the same replies in the same order, only faster. When USB is
        connected the unit is in PC mode and the screen shows what Spooky2 plays.</p>
    </details>
    <details>
      <summary>Can I go back to the original?</summary>
      <p>Yes, at any time, in about a minute. You need Spooky2's own updater file
        (<a :href="VENDOR_DOWNLOADS_URL" target="_blank" rel="noopener">from Spooky2's download page</a>); the
        <a href="#/restore">Go back</a> page installs the original from it. The part of the unit that makes updating
        possible is never changed, so this way always stays open.</p>
    </details>
    <details>
      <summary>Is it safe? Can it break my unit?</summary>
      <p>Installing uses the unit's own update mode, the same one Spooky2's updater uses. Through it only the screen's
        software can be written. If an update is interrupted, you simply start it again. The software comes without any
        warranty, as open-source software does; the <a href="#/docs/safety">safety page</a> says exactly what can and
        cannot change.</p>
    </details>
    <details>
      <summary>What does “beta” mean here?</summary>
      <p>The firmware passes more than 42,000 automated checks and runs on a real unit with Spooky2. But not every
        function has been tried on a real unit yet. <a href="#/docs/known-issues">Known issues</a> lists what was checked
        and what was not.</p>
    </details>
    <details>
      <summary>Is the Terrain Protocol in it?</summary>
      <p>Yes. The Terrain Protocol (Terrain, Terrain-Mercury and Terrain-Nonviolent, as Remote and as Contact sets) and
        the preset “Radionics (Boost) Manifestation” are built in. You find them first in the list under Menu → Chains
        and start them with START, without a computer. Their frequencies and times are taken from Spooky2's presets.</p>
    </details>
    <details>
      <summary>What do I need to install it?</summary>
      <p>A desktop or laptop computer with Chrome, Edge, Opera or Brave, and the unit's USB cable. No extra program: this page
        does it from the browser. Phones and tablets cannot do it.</p>
    </details>
    <details>
      <summary>What does it cost? Who makes it?</summary>
      <p>Nothing. OpenGX Pro is free, open-source software under the MIT licence, made by one developer in spare time.
        It is an independent project, not made or endorsed by Spooky2. If it is useful to you, you can
        <a href="https://buymeacoffee.com/mahapo" target="_blank" rel="noopener">buy me a coffee</a>.</p>
    </details>
  </div>
</template>
