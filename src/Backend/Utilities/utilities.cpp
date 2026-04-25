#include "../Utilities/utilities.h"
#include <filesystem>
#include <fstream>

namespace Utilities {

// ------------------------------------------------------------
//  CreateFile
// ------------------------------------------------------------
bool CreateFile(const std::string& path) {
    // Crear directorios padre si no existen
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec) return false;
    }

    // Crear (o truncar) el archivo
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    return file.good();
}

// ------------------------------------------------------------
//  OpenFile
// ------------------------------------------------------------
std::fstream OpenFile(const std::string& path) {
    return std::fstream(path, std::ios::in | std::ios::out | std::ios::binary);
}

} 