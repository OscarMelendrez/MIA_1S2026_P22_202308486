#pragma once
#include <string>
#include <vector>

namespace FileSystem {

// Formatea una partición montada como EXT2.
// id   : ID de la partición montada (ej: "861A")
// type : "full" (por defecto, único tipo soportado)
std::string Mkfs(const std::string& id,
                 const std::string& type = "full");

// Desmonta una partición del sistema.
// id : ID de la partición a desmontar (ej: "861A")
std::string Unmount(const std::string& id);

// Muestra el contenido de uno o varios archivos encadenados.
// archivos : lista de rutas absolutas dentro del sistema EXT2
//            (ej: {"/home/user/a.txt", "/home/user/b.txt"})
// Requiere sesión activa y permiso de lectura.
std::string Cat(const std::vector<std::string>& archivos);

// Simula pérdida del sistema de archivos EXT2/EXT3
// id : ID de la partición (ej: "861A")
// Formatea bloques de bitmap de inodos, bitmap de bloques, 
// área de inodos y área de bloques con '\0'
std::string Loss(const std::string& id);

// Muestra el journaling (registro de transacciones)
// id : ID de la partición (ej: "861A")
// Retorna tabla con Operacion, Path, Contenido, Fecha
std::string Journaling(const std::string& id);

// Registra una operación en el journal global
void RegistrarOperacion(const std::string& op, const std::string& path, 
                        const std::string& contenido);

}