/**
 * v-reveal: an element fades and rises into place the first time it scrolls into view. With
 * "reduce motion" switched on in the system, or without IntersectionObserver, it is simply there.
 */
import type { Directive } from 'vue'

const still = typeof window === 'undefined' || !('IntersectionObserver' in window)
  || window.matchMedia('(prefers-reduced-motion: reduce)').matches

const observer = still ? null : new IntersectionObserver((entries) => {
  for (const e of entries) {
    if (!e.isIntersecting) continue
    e.target.classList.add('in')
    observer!.unobserve(e.target)
  }
}, { rootMargin: '0px 0px -8% 0px', threshold: 0.08 })

export const vReveal: Directive<HTMLElement, number | undefined> = {
  mounted(el, binding) {
    if (!observer) return
    el.classList.add('reveal')
    if (binding.value) el.style.transitionDelay = `${binding.value}ms`
    observer.observe(el)
  },
  unmounted(el) {
    observer?.unobserve(el)
  },
}
