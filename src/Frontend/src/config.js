// URL de tu backend
// En local: déjalo vacío '' para que el proxy de Vite funcione
// En producción (S3): pon la URL completa de tu backend, ejemplo:
//   'http://1.2.3.4:8080'  (IP pública de tu servidor)
//   'https://api.tudominio.com'  (si tienes dominio con HTTPS)

const BACKEND = import.meta.env.VITE_BACKEND_URL ?? ''

export default BACKEND
