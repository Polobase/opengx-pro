/** Light, dark, or as the system says (the default). The choice is kept in this browser. */
import { ref } from 'vue'

export type Theme = 'auto' | 'light' | 'dark'
const KEY = 'opengx-theme'
const read = (): Theme => {
  try {
    const v = localStorage.getItem(KEY)
    return v === 'light' || v === 'dark' ? v : 'auto'
  } catch {
    return 'auto'
  }
}

export const theme = ref<Theme>(read())

function apply(t: Theme) {
  if (t === 'auto') delete document.documentElement.dataset.theme
  else document.documentElement.dataset.theme = t
}
apply(theme.value)

/** auto -> the opposite of what shows now -> auto. */
export function cycleTheme() {
  const dark = window.matchMedia('(prefers-color-scheme: dark)').matches
  const next: Theme = theme.value === 'auto' ? (dark ? 'light' : 'dark') : 'auto'
  theme.value = next
  apply(next)
  try {
    if (next === 'auto') localStorage.removeItem(KEY)
    else localStorage.setItem(KEY, next)
  } catch {
    /* private mode: the choice lasts for this visit */
  }
}
