<template>
  <div class="home-page">
    <div class="home-container">
      <!-- Logo y Título -->
      <div class="home-header">
        <div class="logo-icon">
          <span class="logo-bracket">[</span>
          <span class="logo-text">EXT</span>
          <span class="logo-bracket">]</span>
        </div>
        <h1 class="home-title">ExtreamFS</h1>
        <p class="home-subtitle">Simulador EXT2 · MIA 1S-2026</p>
      </div>

      <!-- Descripción -->
      <div class="home-description">
        <p class="description-text">
          Sistema de gestión de archivos que permite simular las operaciones fundamentales 
          de un sistema de archivos EXT2, incluyendo creación de discos, particiones, 
          archivos y gestión de permisos.
        </p>
      </div>

      <!-- Botón de Inicio de Sesión -->
      <button class="btn-login-main" @click="goToLogin">
        <span class="btn-icon">▶</span>
        <span class="btn-text">Iniciar Sesión</span>
      </button>

      <!-- Información adicional -->
      <div class="home-info">
        <div class="info-item">
          <span class="info-icon">💾</span>
          <span class="info-label">Gestión de Discos</span>
        </div>
        <div class="info-item">
          <span class="info-icon">📂</span>
          <span class="info-label">Explorador de Archivos</span>
        </div>
        <div class="info-item">
          <span class="info-icon">⌨️</span>
          <span class="info-label">Terminal de Comandos</span>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
const emit = defineEmits(['login'])

function goToLogin() {
  emit('login')
}
</script>

<style scoped>
.home-page {
  display: flex;
  align-items: center;
  justify-content: center;
  height: 100%;
  background: linear-gradient(135deg, var(--bg-base) 0%, var(--bg-surface) 100%);
  padding: 40px 20px;
}

.home-container {
  width: 100%;
  max-width: 500px;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 40px;
  text-align: center;
}

.home-header {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 16px;
}

.logo-icon {
  font-family: var(--font-mono);
  font-weight: 700;
  font-size: 48px;
  letter-spacing: 2px;
}

.logo-bracket {
  color: var(--accent);
}

.logo-text {
  color: var(--text-primary);
}

.home-title {
  font-size: 36px;
  font-weight: 800;
  color: var(--text-primary);
  margin: 0;
  letter-spacing: 0.5px;
}

.home-subtitle {
  font-size: 13px;
  color: var(--text-muted);
  letter-spacing: 0.5px;
  margin: 0;
}

.home-description {
  padding: 20px;
}

.description-text {
  font-size: 14px;
  color: var(--text-secondary);
  line-height: 1.8;
  margin: 0;
}

.btn-login-main {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 12px;
  padding: 14px 32px;
  background: var(--accent);
  color: var(--bg-base);
  border: 2px solid var(--accent);
  border-radius: 8px;
  font-family: var(--font-mono);
  font-size: 14px;
  font-weight: 700;
  letter-spacing: 0.5px;
  cursor: pointer;
  transition: all 0.3s;
  min-width: 280px;
}

.btn-login-main:hover {
  background: #33ddff;
  border-color: #33ddff;
  box-shadow: 0 0 30px var(--accent-glow);
  transform: translateY(-2px);
}

.btn-icon {
  font-size: 16px;
}

.btn-text {
  text-transform: uppercase;
  letter-spacing: 1px;
}

.home-info {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 20px;
  width: 100%;
  margin-top: 20px;
}

.info-item {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
  padding: 16px;
  background: var(--bg-surface);
  border: 1px solid var(--border);
  border-radius: 8px;
  transition: all 0.2s;
}

.info-item:hover {
  border-color: var(--accent-dim);
  background: var(--bg-elevated);
}

.info-icon {
  font-size: 28px;
}

.info-label {
  font-size: 11px;
  color: var(--text-muted);
  font-weight: 600;
  text-align: center;
}

@media (max-width: 600px) {
  .home-title {
    font-size: 28px;
  }

  .home-info {
    grid-template-columns: 1fr;
  }

  .btn-login-main {
    min-width: auto;
    width: 100%;
  }
}
</style>
