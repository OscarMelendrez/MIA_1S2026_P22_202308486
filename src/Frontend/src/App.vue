<template>
  <div class="app">

    <!-- ── Header ─────────────────────────────────────────── -->
    <header class="header" v-if="currentView !== 'command-terminal' && currentView !== 'login'">
      <div class="header-left">
        <div class="logo-icon">
          <span class="logo-bracket">[</span>
          <span class="logo-text">EXT</span>
          <span class="logo-bracket">]</span>
        </div>
        <div class="header-titles">
          <h1 class="title">ExtreamFS</h1>
          <span class="subtitle">Simulador EXT2 · MIA 1S-2026 · 202308486</span>
        </div>
      </div>
      <div class="header-right">
        <div class="status-pill" :class="backendOk ? 'online' : 'offline'">
          <span class="status-dot"></span>
          {{ backendOk ? 'Backend Online' : 'Backend Offline' }}
        </div>
        <div v-if="sesion.activa" class="session-pill">
          <span class="session-icon">◈</span>
          {{ sesion.usuario }} · {{ sesion.particion }}
        </div>
        <button class="btn btn-ghost" @click="logout" title="Cerrar sesión">
          ✕ Cerrar Sesión
        </button>
      </div>
    </header>

    <!-- ── Home View ────────────────────────────────────── -->
    <component v-if="currentView === 'home'" :is="components.home" 
      @login="goToLogin" />

    <!-- ── Login View ────────────────────────────────────── -->
    <component v-else-if="currentView === 'login'" :is="components.login" 
      @login="handleLogin" 
      @cancel="handleCancelLogin"
      :backendOk="backendOk" />

    <!-- ── Command Terminal (Main) ──────────────────────────– -->
    <component v-else-if="currentView === 'command-terminal'" :is="components.commandTerminal"
      :sesion="sesion"
      :backendOk="backendOk"
      @logout="logout"
      @login="goToLogin" />

    <!-- ── Main Layout ────────────────────────────────────── -->
    <main class="main" v-else>

      <!-- ── Disk Selection ──────────────────────────────────– -->
      <component v-if="currentView === 'disk-selection'" :is="components.diskSelection"
        :backendOk="backendOk"
        @select="handleDiskSelect" />

      <!-- ── Partition Selection ──────────────────────────────– -->
      <component v-else-if="currentView === 'partition-selection'" :is="components.partitionSelection"
        :discoSeleccionado="discoSeleccionado"
        @select="handlePartitionSelect"
        @back="handleBackFromPartitionSelection" />

      <!-- ── File Explorer ────────────────────────────────────– -->
      <component v-else-if="currentView === 'file-explorer'" :is="components.fileExplorer"
        :sesion="sesion"
        :discoSeleccionado="discoSeleccionado"
        @navigate="handleFileExplorerNavigate"
        @viewFile="handleViewFile"
        @back="handleBackFromFileExplorer" />

      <!-- ── Command Terminal ─────────────────────────────────– -->
      <component v-else-if="currentView === 'command-terminal'" :is="components.commandTerminal"
        :sesion="sesion"
        :backendOk="backendOk"
        @logout="logout"
        @back="handleBackFromCommandTerminal" />

    </main>
  </div>
</template>

<script setup>
import { ref, computed, watch, nextTick, onMounted } from 'vue'
import HomePage from './components/HomePage.vue'
import LoginPage from './components/LoginPage.vue'
import DiskSelection from './components/DiskSelection.vue'
import PartitionSelection from './components/PartitionSelection.vue'
import FileExplorer from './components/FileExplorer.vue'
import CommandTerminal from './components/CommandTerminal.vue'

const BACKEND = ''

// ── Estado Global ──────────────────────────────────────────
const currentView = ref('command-terminal')
const backendOk = ref(false)
const sesion = ref({ activa: false, usuario: 'root', particion: '', id: '' })
const discoSeleccionado = ref(null)

const components = {
  home: HomePage,
  login: LoginPage,
  diskSelection: DiskSelection,
  partitionSelection: PartitionSelection,
  fileExplorer: FileExplorer,
  commandTerminal: CommandTerminal
}

// ── Funciones de navegación ────────────────────────────────
function goToLogin() {
  currentView.value = 'login'
}

function handleCancelLogin() {
  currentView.value = 'command-terminal'
}

function handleLogin(loginData) {
  sesion.value = {
    activa: true,
    usuario: loginData.usuario,
    particion: loginData.particion,
    id: loginData.id
  }
  currentView.value = 'command-terminal'
}

function handleDiskSelect(disco) {
  discoSeleccionado.value = disco
  currentView.value = 'partition-selection'
}

function handlePartitionSelect(partition) {
  currentView.value = 'file-explorer'
}

function handleBackFromPartitionSelection() {
  currentView.value = 'disk-selection'
}

function handleFileExplorerNavigate(data) {
  currentView.value = 'command-terminal'
}

function handleBackFromFileExplorer() {
  currentView.value = 'partition-selection'
}

function handleBackFromCommandTerminal() {
  currentView.value = 'file-explorer'
}

function handleViewFile(file) {
  // Mostrar contenido del archivo
  currentView.value = 'file-explorer'
}

function logout() {
  sesion.value = { activa: false, usuario: 'root', particion: '', id: '' }
  discoSeleccionado.value = null
  currentView.value = 'command-terminal'
}

// ── Ping al backend ───────────────────────────────────────
async function pingBackend() {
  try {
    const res = await fetch(`${BACKEND}/mounted`, { method: 'GET' })
    backendOk.value = res.ok
  } catch {
    backendOk.value = false
  }
}

onMounted(() => {
  pingBackend()
  setInterval(pingBackend, 10000)
})
</script>

<style scoped>
/* ── Layout ────────────────────────────────────────────────── */
.app {
  display: flex;
  flex-direction: column;
  height: 100vh;
  background: var(--bg-base);
  overflow: hidden;
}

/* ── Header ────────────────────────────────────────────────── */
.header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 20px;
  height: 52px;
  background: var(--bg-surface);
  border-bottom: 1px solid var(--border);
  flex-shrink: 0;
  position: relative;
}

.header::after {
  content: '';
  position: absolute;
  bottom: 0; left: 0; right: 0;
  height: 1px;
  background: linear-gradient(90deg, transparent, var(--accent-dim), transparent);
  opacity: 0.4;
}

.header-left  { display: flex; align-items: center; gap: 14px; }
.header-right { display: flex; align-items: center; gap: 10px; }

.logo-icon {
  font-family: var(--font-mono);
  font-weight: 700;
  font-size: 15px;
  letter-spacing: 1px;
}
.logo-bracket { color: var(--accent); }
.logo-text    { color: var(--text-primary); }

.title {
  font-family: var(--font-ui);
  font-size: 17px;
  font-weight: 800;
  color: var(--text-primary);
  letter-spacing: 0.5px;
}

.subtitle {
  display: block;
  font-size: 10px;
  color: var(--text-muted);
  letter-spacing: 0.3px;
  margin-top: 1px;
}

.status-pill {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 3px 10px;
  border-radius: 20px;
  font-size: 11px;
  font-weight: 500;
  border: 1px solid;
}

.status-pill.online {
  background: var(--green-glow);
  border-color: var(--green-dim);
  color: var(--green);
}

.status-pill.offline {
  background: var(--red-glow);
  border-color: var(--red-dim);
  color: var(--red);
}

.status-dot {
  width: 6px; height: 6px;
  border-radius: 50%;
  background: currentColor;
  animation: pulse 2s infinite;
}

@keyframes pulse {
  0%, 100% { opacity: 1; }
  50%       { opacity: 0.4; }
}

.session-pill {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 3px 10px;
  border-radius: 20px;
  font-size: 11px;
  background: var(--accent-glow);
  border: 1px solid var(--border-glow);
  color: var(--accent);
}

.session-icon { font-size: 9px; }

.btn-ghost {
  background: transparent;
  border-color: var(--border);
  color: var(--text-secondary);
  padding: 5px 12px;
  border-radius: 4px;
  border: 1px solid;
  cursor: pointer;
  font-size: 11px;
  font-weight: 500;
  transition: all 0.15s;
}

.btn-ghost:hover {
  border-color: var(--border-glow);
  color: var(--text-primary);
  background: var(--bg-elevated);
}

/* ── Main ──────────────────────────────────────────────────── */
.main {
  flex: 1;
  overflow: hidden;
  min-height: 0;
}
</style>