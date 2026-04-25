#pragma once
#include <string>

namespace DiskManagement {

// Crea un disco virtual (.mia) lleno de ceros con su MBR
// size  : número a convertir según unit
// fit   : "bf" | "ff" | "wf"
// unit  : "k" | "m"
// path  : ruta absoluta del archivo a crear
std::string Mkdisk(int size,
                   const std::string& fit,
                   const std::string& unit,
                   const std::string& path);

// Crea una partición dentro de un disco existente
// size  : número a convertir según unit
// path  : ruta del .mia
// name  : nombre de la partición (máx 15 chars)
// type  : "p" | "e" | "l"
// fit   : "bf" | "ff" | "wf"
// unit  : "b" | "k" | "m"
std::string Fdisk(int size,
                  const std::string& path,
                  const std::string& name,
                  const std::string& type,
                  const std::string& fit,
                  const std::string& unit);

// Elimina una partición o marca su espacio como vacío
// path   : ruta del .mia
// name   : nombre de la partición
// delete : "fast" (marca vacío) | "full" (marca vacío + rellena con 0)
std::string FdiskDelete(const std::string& path,
                        const std::string& name,
                        const std::string& deleteOption);

// Agrega o resta espacio a una partición existente
// path : ruta del .mia
// name : nombre de la partición
// add  : número positivo (agregar) o negativo (quitar)
// unit : "b" | "k" | "m"
std::string FdiskAdd(const std::string& path,
                     const std::string& name,
                     int add,
                     const std::string& unit);

// Elimina un disco (.mia)
// path  : ruta del .mia a eliminar
std::string Rmdisk(const std::string& path);

// Monta una partición primaria en memoria RAM
// path  : ruta del .mia
// name  : nombre de la partición a montar
std::string Mount(const std::string& path,
                  const std::string& name);

// Muestra todas las particiones montadas en memoria
std::string Mounted();

}