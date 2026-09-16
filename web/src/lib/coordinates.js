// Q9：缺失坐标保留为 null，不能 Number(null) -> 0 后误落到赤道。
export function coordinate(value, limit) {
  if (value === null || value === undefined || typeof value === 'boolean' || String(value).trim() === '') return null
  const n = Number(value)
  return Number.isFinite(n) && Math.abs(n) <= limit ? n : null
}
export function hasCoordinates(station) {
  return coordinate(station.longitude, 180) !== null && coordinate(station.latitude, 90) !== null
}
export function escapeHtml(value) {
  return String(value ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]))
}
