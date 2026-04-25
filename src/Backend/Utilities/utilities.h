#pragma once
#include <fstream>
#include <string>

namespace Utilities {

// Crea el archivo binario (y los directorios padre si no existen)
bool CreateFile(const std::string& path);

// Abre un archivo .mia existente en modo lectura/escritura binario
std::fstream OpenFile(const std::string& path);

// Escribe cualquier struct en una posición específica del archivo
template<typename T>
bool WriteObject(std::fstream& file, const T& data, std::streampos pos) {
    file.seekp(pos);
    file.write(reinterpret_cast<const char*>(&data), sizeof(T));
    return file.good();
}

// Lee cualquier struct desde una posición específica del archivo
template<typename T>
bool ReadObject(std::fstream& file, T& data, std::streampos pos) {
    file.seekg(pos);
    file.read(reinterpret_cast<char*>(&data), sizeof(T));
    return file.good();
}

}