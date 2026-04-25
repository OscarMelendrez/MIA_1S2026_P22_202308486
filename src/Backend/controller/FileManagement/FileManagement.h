#pragma once
#include <string>

namespace FileManagement {

// Crea un archivo dentro del sistema EXT2.
// path : ruta absoluta del archivo a crear (ej: /home/user/a.txt)
// size : tamaño en bytes del archivo (contenido: 0-9 repetido)
// cont : ruta en el disco real de donde cargar el contenido
// r    : true = crear carpetas padre si no existen
std::string Mkfile(const std::string& path,
                   int                size,
                   const std::string& cont,
                   bool               r);

// Crea una carpeta dentro del sistema EXT2.
// path : ruta absoluta de la carpeta a crear
// p    : true = crear carpetas padre si no existen
std::string Mkdir(const std::string& path,
                  bool               p);

// Elimina un archivo o carpeta del sistema EXT2.
// path : ruta del archivo o carpeta a eliminar
std::string Remove(const std::string& path);

// Renombra un archivo o carpeta dentro del sistema EXT2.
// path : ruta actual del archivo o carpeta
// name : nuevo nombre (solo el nombre, no ruta completa)
std::string Rename(const std::string& path,
                   const std::string& name);

// Copia un archivo o carpeta y su contenido a otro destino.
// path    : ruta origen del archivo o carpeta
// destino : ruta destino donde se copiará
std::string Copy(const std::string& path,
                 const std::string& destino);

// Mueve un archivo o carpeta a otro destino (cambia referencias del padre).
// path    : ruta origen del archivo o carpeta
// destino : ruta destino carpeta donde se moverá
std::string Move(const std::string& path,
                 const std::string& destino);

// Busca archivos o carpetas por nombre con comodines (? y *).
// path : ruta base donde buscar
// name : patrón de búsqueda (? = un carácter, * = varios caracteres)
std::string Find(const std::string& path,
                 const std::string& name);

// Cambia el propietario de un archivo o carpeta.
// path    : ruta del archivo o carpeta
// usuario : nuevo propietario (nombre de usuario)
// r       : true = recursivo (afecta contenido de carpeta)
std::string Chown(const std::string& path,
                  const std::string& usuario,
                  bool               r = false);

// Cambia los permisos de un archivo o carpeta.
// path    : ruta del archivo o carpeta
// ugo     : permisos en formato UGO (ej: "755", "644")
// r       : true = recursivo (afecta contenido de carpeta)
std::string Chmod(const std::string& path,
                  const std::string& ugo,
                  bool               r = false);

} // namespace FileManagement