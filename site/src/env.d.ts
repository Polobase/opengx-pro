/// <reference types="vite/client" />

/** A page of the user guide (docs/guide/*.md), rendered at build time (vite.config.ts). */
declare module '*.md' {
  export const title: string
  export const html: string
}
