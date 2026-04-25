<template>
  <div class="command-terminal">
    <div class="terminal-header">
      <div class="terminal-info">
        <h2 class="terminal-title">Terminal de Comandos</h2>
        <p class="terminal-subtitle" v-if="sesion.activa">
          Sesión: {{ sesion.usuario }} · Partición: {{ sesion.particion }}
        </p>
        <p class="terminal-subtitle" v-else>
          Usuario: root · Sin sesión iniciada
        </p>
      </div>
      <div class="header-actions">
        <button v-if="sesion.activa" class="btn btn-visualizer" @click="emit('openVisualizer')">
          📁 Visualizador FS
        </button>

        <button v-if="!sesion.activa" class="btn btn-login" @click="goToLogin">
          🔐 Iniciar Sesión
        </button>
        <button v-else class="btn btn-logout" @click="handleLogout">
          ✕ Cerrar Sesión
        </button>
      </div>
    </div>

    <div class="terminal-content">
      <section class="input-panel">
        <div class="panel-header">
          <span class="panel-icon">❯</span>
          <span class="panel-title">Entrada</span>
          <div class="panel-actions">
            <label class="btn btn-ghost" title="Cargar archivo">
              <input type="file" accept=".smia,.txt" @change="cargarScript" hidden />
              <span>⬆ Cargar</span>
            </label>
            <button class="btn btn-ghost" @click="limpiarEntrada" title="Limpiar">
              ✕ Limpiar
            </button>
          </div>
        </div>

        <div class="editor-wrapper">
          <div class="line-numbers" ref="lineNumbers">
            <span v-for="n in lineCount" :key="n">{{ n }}</span>
          </div>
          <textarea
            ref="editorRef"
            v-model="script"
            class="editor"
            placeholder="# Escribe comandos aquí...
ls -path=/
cat -path=/archivo.txt
mkdir -path=/nueva_carpeta
# Presiona Ejecutar para ver los resultados"
            spellcheck="false"
            autocomplete="off"
            autocorrect="off"
            @scroll="syncScroll"
            @keydown.tab.prevent="insertTab"
            @keydown.enter.ctrl="ejecutar"
          ></textarea>
        </div>

        <div class="execute-bar">
          <div class="execute-info">
            <span class="line-count">{{ lineCount }} líneas</span>
            <span class="cmd-count">{{ commandCount }} comandos</span>
          </div>
          <button
            class="btn btn-execute"
            :class="{ loading: ejecutando }"
            :disabled="ejecutando || !script.trim()"
            @click="ejecutar"
          >
            <span v-if="!ejecutando" class="btn-execute-inner">
              <span class="execute-icon">▶</span>
              Ejecutar (Ctrl+Enter)
            </span>
            <span v-else class="btn-execute-inner">
              <span class="spinner"></span>
              Ejecutando...
            </span>
          </button>
        </div>
      </section>

      <section class="output-panel">
        <div class="panel-header">
          <span class="panel-icon">◉</span>
          <span class="panel-title">Salida</span>
          <div class="panel-actions">
            <span class="output-counter" v-if="outputLines.length">
              {{ outputLines.length }} líneas
            </span>
            <button class="btn btn-ghost" @click="limpiarSalida" v-if="outputLines.length">
              ✕ Limpiar
            </button>
          </div>
        </div>

        <div class="output-area" ref="outputRef">
          <div v-if="outputLines.length === 0" class="output-empty">
            <div class="empty-icon">◈</div>
            <p>Escribe comandos y presiona <strong>Ejecutar</strong></p>
            <p class="empty-hint">Los resultados aparecerán aquí</p>
          </div>

          <div v-for="(line, i) in outputLines" :key="i" class="output-line" :class="line.type">
            <template v-if="line.type === 'separator'">
              <div class="separator">
                <span class="sep-line"></span>
                <span class="sep-text">{{ line.text }}</span>
                <span class="sep-line"></span>
              </div>
            </template>

            <template v-else-if="line.type === 'command'">
              <div class="cmd-line">
                <span class="cmd-prompt">❯</span>
                <span class="cmd-text">{{ line.text }}</span>
              </div>
            </template>

            <template v-else-if="line.type === 'success'">
              <div class="result-line success">
                <span class="result-icon">✓</span>
                <pre class="result-text">{{ line.text }}</pre>
              </div>
            </template>

            <template v-else-if="line.type === 'error'">
              <div class="result-line error">
                <span class="result-icon">✗</span>
                <pre class="result-text">{{ line.text }}</pre>
              </div>
            </template>

            <template v-else-if="line.type === 'image'">
              <div class="report-image-container">
                <div class="report-image-header">
                  <span class="img-icon">🖼</span>
                  <span>{{ line.label }}</span>
                </div>
                <img :src="line.src" :alt="line.label" class="report-image"
                     @error="line.imgError = true"
                     v-if="!line.imgError" />
                <div v-else class="img-error">No se pudo cargar la imagen</div>
              </div>
            </template>
          </div>
        </div>
      </section>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, nextTick } from 'vue'

import BACKEND from '../config.js'

const props = defineProps({
  sesion: Object,
  backendOk: Boolean
})

const emit = defineEmits(['logout', 'back', 'login', 'openVisualizer'])

// Estado
const script = ref('')
const ejecutando = ref(false)
const outputLines = ref([])
const editorRef = ref(null)
const lineNumbers = ref(null)
const outputRef = ref(null)

// Computed
const lineCount = computed(() => {
  const lines = script.value.split('\n')
  return lines.length
})

const commandCount = computed(() => {
  return script.value.split('\n')
    .filter(l => l.trim() && !l.trim().startsWith('#'))
    .length
})

// Helpers
function syncScroll() {
  if (lineNumbers.value && editorRef.value)
    lineNumbers.value.scrollTop = editorRef.value.scrollTop
}

function insertTab(e) {
  const start = e.target.selectionStart
  const end = e.target.selectionEnd
  script.value = script.value.substring(0, start) + '  ' + script.value.substring(end)
  nextTick(() => {
    e.target.selectionStart = e.target.selectionEnd = start + 2
  })
}

function limpiarEntrada() {
  script.value = ''
}

function limpiarSalida() {
  outputLines.value = []
}

function cargarScript(e) {
  const file = e.target.files[0]
  if (!file) return
  const reader = new FileReader()
  reader.onload = ev => { script.value = ev.target.result }
  reader.readAsText(file)
  e.target.value = ''
}

function scrollOutputBottom() {
  nextTick(() => {
    if (outputRef.value)
      outputRef.value.scrollTop = outputRef.value.scrollHeight
  })
}

function extraerRutaReporte(mensaje) {
  let match = mensaje.match(/generado en (.+\.(jpg|jpeg|png|txt))/i)
  if (match) {
    return match[1].trim()
  }
  return null
}

function rutaAUrl(ruta) {
  if (ruta.startsWith('/')) {
    return 'file://' + ruta
  }
  return ruta
}

// Ejecutar Script
async function ejecutar() {
  if (!script.value.trim() || ejecutando.value) return
  ejecutando.value = true

  const now = new Date().toLocaleTimeString('es', { hour12: false })
  outputLines.value.push({
    type: 'separator',
    text: `Ejecución · ${now}`
  })
  scrollOutputBottom()

  try {
    const res = await fetch(`${BACKEND}/ejecutar`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ script: script.value })
    })

    if (!res.ok) throw new Error(`HTTP ${res.status}`)

    const data = await res.json()

    for (const linea of data.lineas) {
      if (!linea.linea.trim()) continue

      outputLines.value.push({
        type: 'command',
        text: linea.linea.trim()
      })

      if (linea.mensaje && linea.mensaje.trim()) {
        const tipo = linea.valida ? 'success' : 'error'
        outputLines.value.push({
          type: tipo,
          text: linea.mensaje
        })

        const jpgPath = extraerRutaReporte(linea.mensaje)
        if (jpgPath && linea.valida) {
          outputLines.value.push({
            type: 'image',
            src: rutaAUrl(jpgPath),
            label: jpgPath.split('/').pop(),
            imgError: false
          })
        }
      }

      scrollOutputBottom()
      await new Promise(r => setTimeout(r, 30))
    }

  } catch (err) {
    outputLines.value.push({
      type: 'error',
      text: `No se pudo conectar al backend: ${err.message}`
    })
  }

  ejecutando.value = false
  scrollOutputBottom()
}

function goBack() {
  // Volver al explorador de archivos
  emit('back')
}

function goToLogin() {
  emit('login')
}

async function handleLogout() {
  try {
    // Ejecutar comando logout en el backend
    const res = await fetch(`${BACKEND}/ejecutar`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ script: 'logout' })
    })

    if (!res.ok) throw new Error(`HTTP ${res.status}`)

    const data = await res.json()
    
    // Verificar si el logout fue exitoso
    const resultado = data.lineas[0]
    if (resultado && resultado.valida) {
      // Logout exitoso, ahora emitir evento para limpiar sesión
      emit('logout')
    } else {
      // Error en logout, pero aún así cerrar sesión localmente
      console.warn('Logout incompleto en backend:', resultado?.mensaje)
      emit('logout')
    }
  } catch (err) {
    console.error('Error al ejecutar logout:', err)
    // Aún así cerrar sesión localmente
    emit('logout')
  }
}
</script>

<style scoped>
.command-terminal {
  display: flex;
  flex-direction: column;
  height: 100%;
  background: var(--bg-base);
  overflow: hidden;
}

.terminal-header {
  display: flex;
  align-items: center;
  gap: 16px;
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
  flex-shrink: 0;
}

.btn-back:hover {
  border-color: var(--border-glow);
  color: var(--text-primary);
}

.terminal-info {
  flex: 1;
}

.terminal-title {
  font-size: 14px;
  font-weight: 700;
  color: var(--text-primary);
  margin: 0;
}

.terminal-subtitle {
  font-size: 11px;
  color: var(--text-muted);
  margin: 2px 0 0;
}

.terminal-content {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 0;
  flex: 1;
  overflow: hidden;
}

.input-panel,
.output-panel {
  display: flex;
  flex-direction: column;
  overflow: hidden;
  min-height: 0;
}

.input-panel {
  border-right: 1px solid var(--border);
}

.panel-header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 0 12px;
  height: 36px;
  background: var(--bg-surface);
  border-bottom: 1px solid var(--border);
  flex-shrink: 0;
}

.panel-icon {
  color: var(--accent);
  font-size: 10px;
}

.panel-title {
  font-family: var(--font-ui);
  font-size: 11px;
  font-weight: 700;
  color: var(--text-secondary);
  letter-spacing: 1px;
  text-transform: uppercase;
  flex: 1;
}

.panel-actions {
  display: flex;
  align-items: center;
  gap: 4px;
}

.btn {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 4px 8px;
  border-radius: 3px;
  font-family: var(--font-mono);
  font-size: 10px;
  font-weight: 500;
  cursor: pointer;
  border: 1px solid transparent;
  transition: all 0.15s;
}

.btn-ghost {
  background: transparent;
  border-color: var(--border);
  color: var(--text-secondary);
}

.btn-ghost:hover {
  border-color: var(--border-glow);
  color: var(--text-primary);
  background: var(--bg-elevated);
}

.editor-wrapper {
  display: flex;
  flex: 1;
  overflow: hidden;
  background: var(--bg-base);
}

.line-numbers {
  width: 36px;
  padding: 8px 0;
  text-align: right;
  font-size: 10px;
  color: var(--text-muted);
  line-height: 1.6;
  overflow: hidden;
  user-select: none;
  border-right: 1px solid var(--border);
  background: var(--bg-surface);
  flex-shrink: 0;
  padding-right: 6px;
}

.line-numbers span {
  display: block;
}

.editor {
  flex: 1;
  padding: 8px 12px;
  background: transparent;
  color: var(--text-primary);
  font-family: var(--font-mono);
  font-size: 12px;
  line-height: 1.6;
  border: none;
  outline: none;
  resize: none;
  overflow-y: auto;
  tab-size: 2;
  white-space: pre;
  overflow-wrap: normal;
  overflow-x: auto;
}

.editor::placeholder {
  color: var(--text-muted);
  font-style: italic;
  font-size: 11px;
}

.execute-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 12px;
  background: var(--bg-surface);
  border-top: 1px solid var(--border);
  flex-shrink: 0;
}

.execute-info {
  display: flex;
  gap: 12px;
  font-size: 10px;
  color: var(--text-muted);
}

.line-count::before {
  content: '⟨ ';
  color: var(--accent-dim);
}

.cmd-count::before {
  content: '# ';
  color: #ffb839;
}

.btn-execute {
  background: var(--accent);
  color: var(--bg-base);
  border-color: var(--accent);
  padding: 5px 12px;
  font-size: 11px;
  font-weight: 700;
  letter-spacing: 0.3px;
}

.btn-execute:hover:not(:disabled) {
  background: #33ddff;
  box-shadow: 0 0 12px var(--accent-glow);
}

.btn-execute:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}

.btn-execute.loading {
  background: var(--bg-elevated);
  border-color: var(--accent-dim);
  color: var(--accent);
}

.btn-execute-inner {
  display: flex;
  align-items: center;
  gap: 5px;
}

.execute-icon {
  font-size: 9px;
}

.spinner {
  width: 10px;
  height: 10px;
  border: 2px solid var(--accent-dim);
  border-top-color: var(--accent);
  border-radius: 50%;
  animation: spin 0.6s linear infinite;
}

@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}

.output-area {
  flex: 1;
  overflow-y: auto;
  padding: 6px 0;
  min-height: 0;
}

.output-empty {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 100%;
  gap: 8px;
  color: var(--text-muted);
  text-align: center;
}

.empty-icon {
  font-size: 28px;
  color: var(--border-glow);
  margin-bottom: 4px;
}

.empty-hint {
  font-size: 10px;
  color: var(--text-muted);
  opacity: 0.6;
}

.output-line {
  padding: 0 12px;
  font-size: 11px;
}

.separator {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 4px 0 2px;
  margin: 2px 0;
}

.sep-line {
  flex: 1;
  height: 1px;
  background: var(--border);
}

.sep-text {
  font-size: 9px;
  color: var(--text-muted);
  letter-spacing: 0.5px;
  white-space: nowrap;
}

.cmd-line {
  display: flex;
  align-items: baseline;
  gap: 6px;
  padding: 2px 0;
}

.cmd-prompt {
  color: var(--accent);
  font-weight: 700;
  font-size: 11px;
  flex-shrink: 0;
}

.cmd-text {
  color: var(--text-primary);
  font-size: 12px;
}

.result-line {
  display: flex;
  align-items: flex-start;
  gap: 6px;
  padding: 2px 0 2px 12px;
  border-left: 2px solid transparent;
  margin: 0;
  border-radius: 0 2px 2px 0;
}

.result-line.success {
  border-left-color: var(--green);
  background: var(--green-glow);
}

.result-line.error {
  border-left-color: var(--red);
  background: var(--red-glow);
}

.result-icon {
  font-size: 10px;
  font-weight: 700;
  flex-shrink: 0;
  margin-top: 1px;
}

.result-line.success .result-icon {
  color: var(--green);
}

.result-line.error .result-icon {
  color: var(--red);
}

.result-text {
  font-family: var(--font-mono);
  font-size: 11px;
  line-height: 1.5;
  white-space: pre-wrap;
  word-break: break-word;
  margin: 0;
}

.result-line.success .result-text {
  color: #b8f5c8;
}

.result-line.error .result-text {
  color: #ffb3bb;
}

.output-counter {
  font-size: 9px;
  color: var(--text-muted);
  padding: 1px 6px;
  background: var(--bg-elevated);
  border-radius: 8px;
  border: 1px solid var(--border);
}

.report-image-container {
  margin: 4px 0;
  border: 1px solid var(--border-glow);
  border-radius: 4px;
  overflow: hidden;
  background: var(--bg-panel);
}

.report-image-header {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 4px 8px;
  background: var(--bg-elevated);
  border-bottom: 1px solid var(--border);
  font-size: 10px;
  color: var(--text-secondary);
}

.img-icon {
  font-size: 11px;
}

.report-image {
  display: block;
  max-width: 100%;
  height: auto;
  cursor: zoom-in;
}

.report-image:hover {
  filter: brightness(1.05);
}

.img-error {
  padding: 12px;
  color: var(--red);
  font-size: 10px;
  text-align: center;
}

.header-actions {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-shrink: 0;
}

/* ── ESTILO NUEVO DEL BOTÓN VISUALIZADOR ── */
.btn-visualizer {
  padding: 6px 14px;
  background: var(--accent);
  color: var(--bg-base);
  border: 1px solid var(--accent);
  border-radius: 4px;
  font-family: var(--font-mono);
  font-size: 11px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.15s;
}

.btn-visualizer:hover {
  background: #33ddff;
  border-color: #33ddff;
  box-shadow: 0 0 15px var(--accent-glow);
  transform: translateY(-1px);
}

.btn-login {
  padding: 6px 14px;
  background: var(--accent);
  color: var(--bg-base);
  border: 1px solid var(--accent);
  border-radius: 4px;
  font-family: var(--font-mono);
  font-size: 11px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.15s;
}

.btn-login:hover {
  background: #33ddff;
  border-color: #33ddff;
  box-shadow: 0 0 15px var(--accent-glow);
  transform: translateY(-1px);
}

.btn-logout {
  padding: 6px 14px;
  background: var(--red);
  color: white;
  border: 1px solid var(--red);
  border-radius: 4px;
  font-family: var(--font-mono);
  font-size: 11px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.15s;
}

.btn-logout:hover {
  background: #ff6b76;
  border-color: #ff6b76;
  box-shadow: 0 0 15px rgba(255, 71, 87, 0.3);
  transform: translateY(-1px);
}
</style>