<template>
  <div class="login-container">
    <div class="login-box">
      <button class="btn-back-top" @click="handleBack" title="Regresar a terminal">
        ← Regresar
      </button>

      <div class="login-header">
        <div class="logo-icon">
          <span class="logo-bracket">[</span>
          <span class="logo-text">EXT</span>
          <span class="logo-bracket">]</span>
        </div>
        <h1 class="login-title">ExtreamFS</h1>
        <p class="login-subtitle">Simulador EXT2 · MIA 1S-2026</p>
      </div>

      <form @submit.prevent="handleLogin" class="login-form">
        <div class="form-group">
          <label for="id-particion" class="form-label">ID Partición</label>
          <input
            id="id-particion"
            v-model="formData.id"
            type="text"
            placeholder="861A"
            class="form-input"
            required
          />
        </div>

        <div class="form-group">
          <label for="usuario" class="form-label">Usuario</label>
          <input
            id="usuario"
            v-model="formData.usuario"
            type="text"
            placeholder="root"
            class="form-input"
            required
          />
        </div>

        <div class="form-group">
          <label for="contrasena" class="form-label">Contraseña</label>
          <input
            id="contrasena"
            v-model="formData.contrasena"
            type="password"
            placeholder="••••••••"
            class="form-input"
            required
          />
        </div>

        <button type="submit" class="btn btn-login" :disabled="loading || !backendOk">
          <span v-if="!loading" class="btn-text">Iniciar Sesión</span>
          <span v-else class="btn-text">
            <span class="spinner"></span>
            Conectando...
          </span>
        </button>

        <div v-if="error" class="error-message">
          <span class="error-icon">✗</span>
          {{ error }}
        </div>

        <div v-if="!backendOk" class="warning-message">
          <span class="warning-icon">⚠</span>
          Backend no disponible
        </div>
      </form>
    </div>
  </div>
</template>

<script setup>
import { ref } from 'vue'

import BACKEND from '../config.js'

const props = defineProps({
  backendOk: Boolean
})

const emit = defineEmits(['login', 'cancel'])

const formData = ref({
  id: '861A',
  usuario: 'root',
  contrasena: '123'
})

const loading = ref(false)
const error = ref('')

async function handleLogin() {
  if (!props.backendOk) {
    error.value = 'Backend no disponible'
    return
  }

  loading.value = true
  error.value = ''

  try {
    // Ejecutar comando login
    const script = `login -user=${formData.value.usuario} -pass=${formData.value.contrasena} -id=${formData.value.id}`
    
    const res = await fetch(`${BACKEND}/ejecutar`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ script })
    })

    if (!res.ok) throw new Error(`HTTP ${res.status}`)

    const data = await res.json()
    
    // Verificar si el login fue exitoso
    const resultado = data.lineas[0]
    if (resultado.valida) {
      emit('login', {
        id: formData.value.id,
        usuario: formData.value.usuario,
        particion: formData.value.id
      })
    } else {
      error.value = resultado.mensaje || 'Error al iniciar sesión'
    }

  } catch (err) {
    error.value = `No se pudo conectar: ${err.message}`
  }

  loading.value = false
}

function handleBack() {
  emit('cancel')
}
</script>

<style scoped>
.login-container {
  display: flex;
  align-items: center;
  justify-content: center;
  height: 100%;
  background: linear-gradient(135deg, var(--bg-base) 0%, var(--bg-surface) 100%);
}

.login-box {
  width: 100%;
  max-width: 380px;
  padding: 40px;
  background: var(--bg-surface);
  border: 1px solid var(--border-glow);
  border-radius: 12px;
  box-shadow: 0 20px 60px rgba(0, 0, 0, 0.3);
}

.login-header {
  text-align: center;
  margin-bottom: 32px;
}

.logo-icon {
  font-family: var(--font-mono);
  font-weight: 700;
  font-size: 32px;
  letter-spacing: 2px;
  margin-bottom: 16px;
}

.logo-bracket {
  color: var(--accent);
}

.logo-text {
  color: var(--text-primary);
}

.login-title {
  font-size: 28px;
  font-weight: 800;
  color: var(--text-primary);
  margin: 12px 0 4px;
  letter-spacing: 0.5px;
}

.login-subtitle {
  font-size: 12px;
  color: var(--text-muted);
  letter-spacing: 0.5px;
}

.login-form {
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.form-group {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.form-label {
  font-size: 12px;
  font-weight: 600;
  color: var(--text-secondary);
  text-transform: uppercase;
  letter-spacing: 1px;
}

.form-input {
  padding: 10px 12px;
  background: var(--bg-base);
  border: 1px solid var(--border);
  border-radius: 6px;
  color: var(--text-primary);
  font-family: var(--font-mono);
  font-size: 13px;
  transition: all 0.15s;
}

.form-input:focus {
  outline: none;
  border-color: var(--accent);
  box-shadow: 0 0 0 2px var(--accent-glow);
}

.form-input::placeholder {
  color: var(--text-muted);
}

.btn-login {
  padding: 11px 16px;
  background: var(--accent);
  color: var(--bg-base);
  border: 1px solid var(--accent);
  border-radius: 6px;
  font-family: var(--font-mono);
  font-size: 12px;
  font-weight: 700;
  letter-spacing: 0.5px;
  cursor: pointer;
  transition: all 0.2s;
  margin-top: 8px;
}

.btn-login:hover:not(:disabled) {
  background: #33ddff;
  box-shadow: 0 0 20px var(--accent-glow);
  transform: translateY(-1px);
}

.btn-login:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.btn-text {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
}

.spinner {
  width: 12px;
  height: 12px;
  border: 2px solid rgba(255, 255, 255, 0.3);
  border-top-color: white;
  border-radius: 50%;
  animation: spin 0.6s linear infinite;
}

@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}

.error-message {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 10px 12px;
  background: var(--red-glow);
  border: 1px solid var(--red-dim);
  border-radius: 6px;
  color: #ffb3bb;
  font-size: 12px;
}

.error-icon {
  color: var(--red);
  font-weight: 700;
  flex-shrink: 0;
}

.warning-message {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 10px 12px;
  background: rgba(255, 184, 57, 0.1);
  border: 1px solid rgba(255, 184, 57, 0.3);
  border-radius: 6px;
  color: #ffb839;
  font-size: 12px;
}

.warning-icon {
  font-weight: 700;
  flex-shrink: 0;
}

.btn-back-top {
  position: absolute;
  top: 20px;
  left: 20px;
  padding: 6px 12px;
  background: transparent;
  border: 1px solid var(--border);
  border-radius: 4px;
  color: var(--text-secondary);
  font-family: var(--font-mono);
  font-size: 11px;
  cursor: pointer;
  transition: all 0.15s;
}

.btn-back-top:hover {
  border-color: var(--accent);
  color: var(--accent);
  background: var(--accent-glow);
}
</style>
