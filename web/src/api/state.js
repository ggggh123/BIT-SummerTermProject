import { ref } from 'vue'

// 全局数据源状态：mock（演示数据）| api（真实 Flask 接口）
export const dataSource = ref('mock')
export const generatedAt = ref('')
export const connectionState = ref('mock') // mock | live | stale | error
export const lastError = ref('')
