import { dirname, relative, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import vue from '@vitejs/plugin-vue'
import MarkdownIt from 'markdown-it'
import { defineConfig, type Plugin } from 'vite'

const REPO_URL = 'https://github.com/Polobase/opengx-pro'
const REPO_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..')

/**
 * The page must never contain the PC software's session code: fail the build if a module of
 * @freqgen/spooky2 that holds or uses it ends up in the bundle.
 */
function noSessionCode(): Plugin {
  const forbidden = [/genx-auth-transform/, /spooky2\/dist\/auth\.js$/, /spooky2\/dist\/genx-pro\.js$/, /spooky2\/dist\/genx-pair\.js$/]
  return {
    name: 'no-session-code',
    generateBundle(_options, bundle) {
      for (const chunk of Object.values(bundle)) {
        if (chunk.type !== 'chunk') continue
        const bad = Object.keys(chunk.modules).filter((id) => forbidden.some((re) => re.test(id)))
        if (bad.length) this.error(`session code in the bundle (${chunk.fileName}): ${bad.join(', ')}`)
      }
    },
  }
}

/**
 * The user guide (docs/guide/*.md, the same files GitHub shows) as modules: `html` and `title`.
 * Rendered at build time. Pictures become bundled assets; a link to another guide page becomes a
 * route of this page, a link to another file of the repository goes to that file on GitHub.
 */
function guideMarkdown(): Plugin {
  const md = new MarkdownIt({ html: false, linkify: true })
  return {
    name: 'guide-markdown',
    transform(source, id) {
      if (!id.endsWith('.md')) return null
      const tokens = md.parse(source, {})
      const images: string[] = []
      let title = ''
      for (let i = 0; i < tokens.length; i++) {
        const t = tokens[i]!
        if (t.type === 'heading_open' && t.tag === 'h1' && !title) title = tokens[i + 1]?.content ?? ''
        for (const c of t.children ?? []) {
          if (c.type === 'image') {
            const src = c.attrGet('src') ?? ''
            if (!/^[a-z]+:/i.test(src)) {
              c.attrSet('src', `\u0000${images.length}\u0000`)
              images.push(src)
            }
            c.attrSet('loading', 'lazy')
          } else if (c.type === 'link_open') {
            const href = c.attrGet('href') ?? ''
            const page = /^\.\/([\w-]+)\.md(#.*)?$/.exec(href)
            if (page) {
              c.attrSet('href', `#/docs/${page[1]}`)
            } else if (/^[a-z]+:/i.test(href)) {
              c.attrSet('target', '_blank')
              c.attrSet('rel', 'noopener')
            } else if (!href.startsWith('#')) {
              const [path, hash = ''] = href.split('#')
              const inRepo = relative(REPO_ROOT, resolve(dirname(id), path!)).split('\\').join('/')
              c.attrSet('href', `${REPO_URL}/blob/main/${inRepo}${hash && '#' + hash}`)
              c.attrSet('target', '_blank')
              c.attrSet('rel', 'noopener')
            }
          }
        }
      }
      const html = md.renderer.render(tokens, md.options, {})
      const parts = html.split(/\u0000(\d+)\u0000/).map((p, i) => (i % 2 ? `img${p}` : JSON.stringify(p)))
      const imports = images.map((src, i) => `import img${i} from ${JSON.stringify(src)}`)
      return {
        code: [...imports, `export const title = ${JSON.stringify(title)}`, `export const html = [${parts.join(', ')}].join('')`].join('\n'),
        map: null,
      }
    },
  }
}

// The site lives at https://polobase.github.io/opengx-pro/ (GitHub Pages serves a repository under its name).
export default defineConfig({
  base: '/opengx-pro/',
  plugins: [guideMarkdown(), vue(), noSessionCode()],
  server: { fs: { allow: ['..'] } },
})
