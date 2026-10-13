# The OpenGX Pro site

The page at <https://polobase.github.io/opengx-pro/>: it installs a released version of the
firmware from the browser, puts the vendor's original firmware back, and shows the user guide. It
is a static page (Vue, Vite) that talks to the unit over **Web Serial**, so installing needs
Chrome, Edge or Opera on a desktop computer.

| Section | What it does |
|---|---|
| Overview | what the firmware is, screens, how it works |
| Install | lists the released versions (`firmware/index.json`), downloads the chosen one from this site, checks its SHA-256 and flashes it through the unit's update mode |
| Restore original | takes the vendor's own updater `GX_Pro_Firmware_v7789.exe` (or `GxDisplayFW.bin`, or a file you built), finds the image in it in the browser and flashes it the same way. The vendor's firmware is never bundled or uploaded |
| Try it | a few things to try on the installed firmware: reply time, text on the screen, the stored programs. Display only |
| Docs | the user guide, `docs/guide/*.md` of this repository, rendered when the site is built |

## Safety

- `src/lib/flasher.ts` is a port of `tools/flash_gx_pro.py` (the vendor updater's protocol). Before
  anything is written it reads the hardware ID and refuses a port that answers another ID (the
  unit's other channel answers "X1") or nothing (not in update mode). At most 4 resends per update.
- `src/lib/image.ts` accepts only the display board's firmware ID (`X2Vx`) and at most the 112,640
  bytes the bootloader is proven to take.
- The update protocol has no address: only the application area can be written, not the
  bootloader and not the option bytes.
- "Try it" uses the serial transport of `@freqgen/core` and wire helpers of `@freqgen/spooky2`. It
  never opens a session with the generator boards; the build fails if that package's session
  modules end up in the bundle (`vite.config.ts`).

## Develop

```sh
npm ci
npm run firmware       # after `make release` in the repository: build/release -> public/firmware/
npm run dev            # http://localhost:5173/opengx-pro/ (Web Serial works on localhost)
npm test               # image rules, flashing against a simulated bootloader, the QR code, the release list
npm run build          # type check and dist/
```

`public/firmware/` is not in git. For the published site the Pages workflow fills it from the
project's GitHub releases (`npm run firmware:releases`): every release that carries
`opengx-pro-<version>.bin` and its `manifest.json` is offered, newest first. A browser cannot
download release files from github.com itself (no cross-origin access), so the site serves its own
copies; each is checked against its manifest when the site is built and again in the browser.
