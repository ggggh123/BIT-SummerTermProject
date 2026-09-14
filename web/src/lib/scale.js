// 大屏等比缩放（《01》§5.1：主页按 1920×1080 设计，用 transform: scale 适配）
export function computeScale(viewportW, viewportH, baseW = 1920, baseH = 1080) {
  if (!Number.isFinite(viewportW) || !Number.isFinite(viewportH) || viewportW <= 0 || viewportH <= 0) return 1
  if (!Number.isFinite(baseW) || !Number.isFinite(baseH) || baseW <= 0 || baseH <= 0) return 1
  return Number(Math.min(viewportW / baseW, viewportH / baseH).toFixed(4))
}
