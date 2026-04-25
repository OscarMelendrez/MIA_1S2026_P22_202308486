<template>
  <div class="disk-selection">
    <div class="disk-header">
      <h2 class="disk-title">Seleccione un disco que desea visualizar:</h2>
      <p class="disk-subtitle">Los discos disponibles se muestran abajo</p>
    </div>

    <div class="disk-grid" v-if="discos.length > 0">
      <button
        v-for="(disco, index) in discos"
        :key="index"
        class="disk-card"
        @click="selectDisk(disco)"
      >
        <div class="disk-icon">💾</div>
        <div class="disk-name">{{ disco.label }}</div>
        <div class="disk-info">
          <span class="disk-path">{{ disco.path }}</span>
        </div>
      </button>
    </div>

    <div class="disk-empty" v-else>
      <div class="empty-icon">⚠</div>
      <p>No hay discos disponibles</p>
      <p class="empty-hint" v-if="!backendOk">Backend no disponible</p>
      <p class="empty-hint" v-else>Asegúrate de haber montado un disco</p>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, computed } from 'vue'
import BACKEND from '../config.js'

const props = defineProps({ backendOk: Boolean })
const emit = defineEmits(['select'])

const particionesMontadas = ref([])

// Analiza los datos del backend y agrupa particiones por disco (usando su ruta física)
const discos = computed(() => {
  const discosUnicos = new Map()

  particionesMontadas.value.forEach(p => {
    if (!discosUnicos.has(p.path)) {
      // Extrae solo el nombre del archivo (ej. /home/user/discos/A.mia -> A.mia)
      const fileName = p.path.split('/').pop() || p.path
      discosUnicos.set(p.path, {
        label: fileName,
        path: p.path
      })
    }
  })

  return Array.from(discosUnicos.values())
})

async function loadMountedDisks() {
  if (!props.backendOk) return
  try {
    const res = await fetch(`${BACKEND}/mounted`, { method: 'GET' })
    if (res.ok) {
      const data = await res.json()
      // Guardamos la respuesta cruda de tu C++
      particionesMontadas.value = data.particiones || []
    }
  } catch (err) {
    console.error('Error cargando discos del backend:', err)
  }
}

function selectDisk(disco) {
  emit('select', disco)
}

onMounted(() => {
  loadMountedDisks()
})
</script>

<style scoped>
.disk-selection {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 100%;
  padding: 40px;
  background: var(--bg-base);
  gap: 40px;
}

.disk-header {
  text-align: center;
}

.disk-title {
  font-size: 24px;
  font-weight: 700;
  color: var(--text-primary);
  margin-bottom: 8px;
  letter-spacing: 0.5px;
}

.disk-subtitle {
  font-size: 13px;
  color: var(--text-muted);
}

.disk-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
  gap: 20px;
  width: 100%;
  max-width: 600px;
}

.disk-card {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 12px;
  padding: 24px;
  background: var(--bg-surface);
  border: 2px solid var(--border);
  border-radius: 12px;
  cursor: pointer;
  transition: all 0.2s;
  text-align: center;
}

.disk-card:hover {
  border-color: var(--accent);
  background: var(--bg-elevated);
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.2);
  transform: translateY(-4px);
}

.disk-icon {
  font-size: 48px;
}

.disk-name {
  font-family: var(--font-mono);
  font-size: 14px;
  font-weight: 700;
  color: var(--text-primary);
  letter-spacing: 1px;
}

.disk-info {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.disk-path {
  font-size: 11px;
  color: var(--text-muted);
}

.disk-empty {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 12px;
  color: var(--text-muted);
  text-align: center;
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
