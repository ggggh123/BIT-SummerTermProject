import { createRouter, createWebHashHistory } from 'vue-router'

// hash 路由：dist 由 Flask / 任意静态服务器托管时不需要服务端 rewrite
const routes = [
  { path: '/', name: 'home', component: () => import('@/views/HomeView.vue'), meta: { title: '综合运营大屏' } },
  { path: '/user', name: 'user', component: () => import('@/views/UserView.vue'), meta: { title: '用户视角' } },
  { path: '/station', name: 'station', component: () => import('@/views/StationView.vue'), meta: { title: '充电站视角' } },
  { path: '/enterprise', name: 'enterprise', component: () => import('@/views/EnterpriseView.vue'), meta: { title: '企业视角' } },
  { path: '/gov', name: 'gov', component: () => import('@/views/GovView.vue'), meta: { title: '社会视角' } },
]

export default createRouter({ history: createWebHashHistory(), routes })
