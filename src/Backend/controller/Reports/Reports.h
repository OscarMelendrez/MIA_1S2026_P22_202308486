#pragma once
#include <string>
#include <vector>

// Estructura para registros del journal
struct RegistroJournal {
    std::string operacion;
    std::string path;
    std::string contenido;
    std::string fecha;
};

namespace Reports {
    std::string Rep(const std::string& name,
                    const std::string& outputPath,
                    const std::string& id,
                    const std::string& pathFileLs);
    
    // Generar imagen JPG del journaling
    std::string GenerarImagenJournaling(const std::vector<RegistroJournal>& registros,
                                        const std::string& id);
}