<template>
  <div class="partition-selection">
    <div class="partition-header">
      <button class="btn-back" @click="goBack" title="Volver a seleccionar disco">
        ← Atrás
      </button>
      <div class="header-content">
        <h2 class="partition-title">Seleccione la partición que desea visualizar:</h2>
        <p class="partition-subtitle">Disco: {{ discoSeleccionado?.label }}</p>
      </div>
    </div>

    <div class="partition-grid" v-if="particiones.length > 0">
      <button
        v-for="(partition, index) in particiones"
        :key="index"
        class="partition-card"
        @click="selectPartition(partition)"
      >
        <div class="partition-icon">📁</div>
        <div class="partition-name">{{ partition.label }}</div>
        <div class="partition-info">
          <span class="partition-size">{{ formatSize(partition.size) }}</span>
          <span class="partition-status">{{ partition.status }}</span>
        </div>
      </button>
    </div>

    <div class="partition-empty" v-else>
      <div class="empty-icon">⚠</div>
      <p>No hay particiones disponibles</p>
      <p class="empty-hint">Crea particiones usando comandos fdisk y mount</p>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'

import BACKEND from '../config.js'

const props = defineProps({
  discoSeleccionado: Object
})

const emit = defineEmits(['select', 'back'])

const particionesData = ref([])

const particiones = computed(() => {
  // Retornar particiones del disco seleccionado
  return particionesData.value.map((p, index) => ({
    label: `partición${index + 1}`,
    id: p.id,
    size: p.size,
    status: 'Montada',
    path: p.path,
    nombre: p.nombre
  }))
})

async function loadPartitions() {
  try {
    const res = await fetch(`${BACKEND}/mounted`, { method: 'GET' })
    if (res.ok) {
      const data = await res.json()
      particionesData.value = data.particiones || []
    }
  } catch (err) {
    console.error('Error loading partitions:', err)
  }
}

function formatSize(bytes) {
  if (bytes >= 1048576) return `${(bytes / 1048576).toFixed(1)} MB`
  if (bytes >= 1024) return `${(bytes / 1024).toFixed(1)} KB`
  return `${bytes} B`
}

function selectPartition(partition) {
  emit('select', partition)
}

function goBack() {
  emit('back')
}

onMounted(() => {
  loadPartitions()
})
</script>

<style scoped>
.partition-selection {
  display: flex;
  flex-direction: column;
  height: 100%;
  padding: 40px;
  background: var(--bg-base);
  gap: 30px;
}

.partition-header {
  display: flex;
  align-items: center;
  gap: 20px;
}

.btn-back {
  padding: 8px 16px;
  background: var(--bg-surface);
  border: 1px solid var(--border);
  border-radius: 6px;
  color: var(--text-secondary);
  font-family: var(--font-mono);
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.15s;
  flex-shrink: 0;
}

.btn-back:hover {
  border-color: var(--border-glow);
  color: var(--text-primary);
  background: var(--bg-elevated);
}

.header-content {
  flex: 1;
}

.partition-title {
  font-size: 20px;
  font-weight: 700;
  color: var(--text-primary);
  margin-bottom: 4px;
  letter-spacing: 0.5px;
}

.partition-subtitle {
  font-size: 12px;
  color: var(--text-muted);
}

.partition-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
  gap: 16px;
  flex: 1;
}

.partition-card {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 12px;
  padding: 20px;
  background: var(--bg-surface);
  border: 2px solid var(--border);
  border-radius: 10px;
  cursor: pointer;
  transition: all 0.2s;
  text-align: center;
}

.partition-card:hover {
  border-color: var(--accent);
  background: var(--bg-elevated);
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.2);
  transform: translateY(-2px);
}

.partition-icon {
  font-size: 40px;
}

.partition-name {
  font-family: var(--font-mono);
  font-size: 13px;
  font-weight: 700;
  color: var(--text-primary);
  letter-spacing: 0.5px;
}

.partition-info {
  display: flex;
  flex-direction: column;
  gap: 2px;
  font-size: 11px;
}

.partition-size {
  color: var(--text-muted);
}

.partition-status {
  color: var(--green);
  font-weight: 600;
}

.partition-empty {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 12px;
  color: var(--text-muted);
  text-align: center;
  flex: 1;
}

.empty-icon {
  font-size: 48px;
  color: var(--border-glow);
}

.empty-hint {
  font-size: 12px;
  color: var(--text-muted);
  opacity: 0.6;
}
</style>
