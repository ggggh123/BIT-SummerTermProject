<script setup>
defineProps({ name: { type: String, default: 'energy' } })
const paths = {
  energy: 'm14 2-9 12h6l-1 8 9-12h-6l1-8Z',
  home: 'M3 10V3h7v7H3Zm11 0V3h7v7h-7ZM3 21v-7h7v7H3Zm11 0v-7h7v7h-7Z',
  user: 'M16 7a4 4 0 1 1-8 0 4 4 0 0 1 8 0ZM4 21v-3a6 6 0 0 1 6-6h4a6 6 0 0 1 6 6v3',
  station: 'M4 21V3h11v18M2 21h15M7 6h5v5H7M15 7h3l3 4v7a2 2 0 0 1-4 0v-4h-2',
  enterprise: 'M3 21h18M5 21V8h5v13M10 21V3h9v18M13 7h3M13 11h3M13 15h3',
  gov: 'm2 8 10-6 10 6H2Zm2 13h16M6 11v7m6-7v7m6-7v7',
  arrow: 'M5 12h14m-6-6 6 6-6 6',
  expand: 'M8 3H3v5m13-5h5v5M3 16v5h5m13-5v5h-5',
  close: 'm6 6 12 12M6 18 18 6',
  check: 'm5 12 4 4L19 6',
}
</script>
<template>
  <svg class="ui-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path :d="paths[name] || paths.energy" /></svg>
</template>
