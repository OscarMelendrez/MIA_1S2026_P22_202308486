import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  plugins: [vue()],
  server: {
    port: 3000,
    proxy: {
      '/ejecutar': {
        target: 'http://3.22.167.218:8080',
        changeOrigin: true
      },
      '/mounted': {
        target: 'http://3.22.167.218:8080',
        changeOrigin: true
      },
      '/reportes': {
        target: 'http://3.22.167.218:8080',
        changeOrigin: true
      }
    }
  }
})