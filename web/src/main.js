import { createApp } from 'vue'
import App from './App.vue'
import router from './router'
import '@kjgl77/datav-vue3/dist/style.css'
import './styles/theme.css'

createApp(App).use(router).mount('#app')
