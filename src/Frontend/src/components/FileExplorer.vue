<template>
  <div class="file-explorer">
    <div class="explorer-header">
      <button class="btn-back" @click="goBack" title="Volver">
        ← Atrás
      </button>
      <div class="breadcrumb">
        <button class="breadcrumb-item" @click="navigateTo('/')">
          <span class="breadcrumb-icon">/</span>
        </button>
        <template v-for="(part, index) in pathParts" :key="index">
          <span class="breadcrumb-separator">/</span>
          <button class="breadcrumb-item" @click="navigateTo(buildPath(index))">
            {{ part }}
          </button>
        </template>
      </div>
      <button class="btn-terminal" @click="openTerminal" title="Abrir terminal de comandos">
        ▶ Terminal
      </button>
    </div>

    <div class="explorer-content">
      <div class="file-list">
        <div class="file-list-header">
          <span class="col-name">Nombre</span>
          <span class="col-type">Tipo</span>
          <span class="col-size">Tamaño</span>
          <span class="col-perms">Permisos</span>
        </div>

        <div v-if="loading" class="loading">
          <span class="spinner"></span>
          Cargando...
        </div>

        <div v-else-if="items.length === 0" class="empty">
          <div class="empty-icon">📁</div>
          <p>Carpeta vacía</p>
        </div>

        <button
          v-for="(item, index) in items"
          :key="index"
          class="file-item"
          :class="{ 'is-directory': item.isDirectory }"
          @click="handleItemClick(item)"
        >
          <span class="col-name">
            <span class="item-icon">{{ item.isDirectory ? '📁' : '📄' }}</span>
            {{ item.name }}
          </span>
          <span class="col-type">{{ item.isDirectory ? 'Carpeta' : 'Archivo' }}</span>
          <span class="col-size">{{ item.size || '-' }}</span>
          <span class="col-perms">{{ item.perms || '-' }}</span>
        </button>
      </div>

      <div class="file-preview" v-if="selectedFile && !selectedFile.isDirectory">
        <div class="preview-header">
          <h3 class="preview-title">{{ selectedFile.name }}</h3>
          <button class="btn-close" @click="selectedFile = null" title="Cerrar">✕</button>
        </div>
        <div class="preview-content">
          <div v-if="fileContent" class="file-content">
            <pre>{{ fileContent }}</pre>
          </div>
          <div v-else class="preview-loading">
            <span class="spinner"></span>
            Cargando contenido...
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed } from 'vue'
import BACKEND from '../config.js'

const props = defineProps({
  sesion: Object,
  discoSeleccionado: Object
})

const emit = defineEmits(['navigate', 'viewFile', 'back'])

const currentPath = ref('/')
const loading = ref(false)
const items = ref([])
const selectedFile = ref(null)
const fileContent = ref('')

const pathParts = computed(() => {
  if (currentPath.value === '/') return []
  return currentPath.value.split('/').filter(p => p)
})

function buildPath(upToIndex) {
  const parts = pathParts.value.slice(0, upToIndex + 1)
  return '/' + parts.join('/')
}

async function loadDirectory(path) {
  loading.value = true
  items.value = [] // Limpiamos la lista actual
  
  try {
    // Mandamos el comando ls al backend de C++
    const script = `ls -path=${path}`
    
    const res = await fetch(`${BACKEND}/ejecutar`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ script })
    })

    if (res.ok) {
      const data = await res.json()
      const resultado = data.lineas[0]
      
      if (resultado && resultado.valida && resultado.mensaje) {
        const lines = resultado.mensaje.split('\n').filter(l => l.trim())
        
        items.value = lines.map(line => {
          // El frontend espera el formato: Nombre | Tipo | Tamaño | Permisos
          const partes = line.split('|').map(p => p.trim())
          
          let nombre = partes[0]
          let isDir = false
          let size = '-'
          let perms = '-'

          if (partes.length >= 2) {
            // Lectura de datos reales del backend
            isDir = (partes[1].toLowerCase() === 'd' || partes[1].toLowerCase() === 'carpeta' || partes[1] === '1')
            size = partes[2] || '-'
            perms = partes[3] || '-'
          } else {
            // Fallback: Si tu C++ solo manda el nombre, intenta adivinar por la barra '/'
            isDir = nombre.endsWith('/') || !nombre.includes('.')
            if (nombre.endsWith('/')) nombre = nombre.slice(0, -1)
          }

          return {
            name: nombre,
            fullPath: path === '/' ? `/${nombre}` : `${path}/${nombre}`,
            isDirectory: isDir,
            size: size,
            perms: perms
          }
        })
      }
    }
  } catch (err) {
    console.error('Error cargando el directorio:', err)
  }
  loading.value = false
}

async function handleItemClick(item) {
  if (item.isDirectory) {
    const newPath = currentPath.value === '/' 
      ? `/${item.name}`
      : `${currentPath.value}/${item.name}`
    currentPath.value = newPath
    await loadDirectory(newPath)
  } else {
    selectedFile.value = item
    await loadFileContent(item)
  }
}

async function loadFileContent(file) {
  fileContent.value = ''
  try {
    // Comando estandar en MIA para leer archivos
    const script = `cat -file1=${file.fullPath}`
    
    const res = await fetch(`${BACKEND}/ejecutar`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ script })
    })

    if (res.ok) {
      const data = await res.json()
      const resultado = data.lineas[0]
      if (resultado && resultado.valida) {
        fileContent.value = resultado.mensaje
      } else {
        fileContent.value = "// Error al leer el archivo desde el backend"
      }
    }
  } catch (err) {
    console.error('Error cargando archivo:', err)
    fileContent.value = "// Error de conexión"
  }
}

function navigateTo(path) {
  currentPath.value = path
  loadDirectory(path)
}

function goBack() {
  emit('back')
}

function openTerminal() {
  emit('navigate', { action: 'openTerminal' })
}

// Carga inicial
loadDirectory('/')
</script>

<style scoped>
.file-explorer {
  display: flex;
  flex-direction: column;
  height: 100%;
  background: var(--bg-base);
  overflow: hidden;
}

.explorer-header {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 16px;
  background: var(--bg-surface);
  border-bottom: 1px solid var(--border);
  flex-shrink: 0;
}

.btn-back {
  padding: 6px 12px;
  background: transparent;
  border: 1px solid var(--border);
  border-radius: 4px;
  color: var(--text-secondary);
  font-family: var(--font-mono);
  font-size: 11px;
  cursor: pointer;
  transition: all 0.15s;
  white-space: nowrap;
  flex-shrink: 0;
}

.btn-back:hover {
  border-color: var(--border-glow);
  color: var(--text-primary);
}

.breadcrumb {
  display: flex;
  align-items: center;
  gap: 4px;
  flex: 1;
  overflow-x: auto;
  padding: 0 8px;
  font-size: 12px;
}

.breadcrumb-item {
  padding: 4px 8px;
  background: transparent;
  border: none;
  color: var(--accent);
  font-family: var(--font-mono);
  cursor: pointer;
  transition: all 0.15s;
  white-space: nowrap;
}

.breadcrumb-item:hover {
  color: #33ddff;
}

.breadcrumb-icon {
  font-weight: 700;
}

.breadcrumb-separator {
  color: var(--text-muted);
}

.btn-terminal {
  padding: 6px 12px;
  background: var(--accent);
  border: 1px solid var(--accent);
  border-radius: 4px;
  color: var(--bg-base);
  font-family: var(--font-mono);
  font-size: 11px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.15s;
  white-space: nowrap;
  flex-shrink: 0;
}

.btn-terminal:hover {
  background: #33ddff;
  box-shadow: 0 0 12px var(--accent-glow);
}

.explorer-content {
  display: grid;
  grid-template-columns: 1fr 300px;
  gap: 1px;
  flex: 1;
  overflow: hidden;
  background: var(--border);
}

.file-list {
  display: flex;
  flex-direction: column;
  background: var(--bg-base);
  overflow-y: auto;
}

.file-list-header {
  display: grid;
  grid-template-columns: 1fr 80px 80px 80px;
  gap: 12px;
  padding: 8px 12px;
  background: var(--bg-surface);
  border-bottom: 1px solid var(--border);
  position: sticky;
  top: 0;
  font-size: 11px;
  font-weight: 600;
  color: var(--text-secondary);
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.file-item {
  display: grid;
  grid-template-columns: 1fr 80px 80px 80px;
  gap: 12px;
  padding: 8px 12px;
  background: transparent;
  border: none;
  border-bottom: 1px solid var(--border);
  text-align: left;
  cursor: pointer;
  transition: all 0.15s;
  font-family: var(--font-mono);
  font-size: 12px;
  color: var(--text-primary);
}

.file-item:hover {
  background: var(--bg-surface);
}

.file-item.is-directory {
  font-weight: 600;
  color: var(--accent);
}

.col-name {
  display: flex;
  align-items: center;
  gap: 8px;
  overflow: hidden;
}

.col-type,
.col-size,
.col-perms {
  text-align: right;
  font-size: 11px;
  color: var(--text-muted);
}

.item-icon {
  flex-shrink: 0;
}

.loading,
.empty {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 40px;
  color: var(--text-muted);
  text-align: center;
  gap: 12px;
}

.empty-icon {
  font-size: 48px;
  color: var(--border-glow);
}

.spinner {
  width: 16px;
  height: 16px;
  border: 2px solid var(--border);
  border-top-color: var(--accent);
  border-radius: 50%;
  animation: spin 0.6s linear infinite;
}

@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}

.file-preview {
  display: flex;
  flex-direction: column;
  background: var(--bg-surface);
  border-left: 1px solid var(--border);
  overflow: hidden;
}

.preview-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px;
  background: var(--bg-base);
  border-bottom: 1px solid var(--border);
}

.preview-title {
  font-size: 12px;
  font-weight: 700;
  color: var(--text-primary);
  margin: 0;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.btn-close {
  padding: 2px 6px;
  background: transparent;
  border: none;
  color: var(--text-muted);
  cursor: pointer;
  transition: all 0.15s;
}

.btn-close:hover {
  color: var(--text-primary);
}

.preview-content {
  flex: 1;
  overflow: auto;
  padding: 12px;
}

.file-content pre {
  margin: 0;
  font-family: var(--font-mono);
  font-size: 11px;
  color: var(--text-primary);
  white-space: pre-wrap;
  word-wrap: break-word;
  line-height: 1.5;
}

.preview-loading {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 8px;
  height: 100%;
  color: var(--text-muted);
  font-size: 12px;
}
</style>