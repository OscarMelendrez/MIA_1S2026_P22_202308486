#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>


struct LineAnalysis {
    std::string linea;    // Texto original de la linea
    bool        valida;   // true  = ejecutado sin error
                          // false = error o linea ignorada
    std::string mensaje;  // Mensaje que se muestra en el textarea de salida
};

// Serializacion JSON automatica para crow/nlohmann
inline void to_json(nlohmann::json& j, const LineAnalysis& a) {
    j = nlohmann::json{
        {"linea",   a.linea},
        {"valida",  a.valida},
        {"mensaje", a.mensaje}
    };
}


struct ScriptResult {
    bool                      exito;      // false si hubo error critico
    std::string               error;      // mensaje de error critico (si aplica)
    std::vector<LineAnalysis> lineas;     // resultado linea por linea
};

inline void to_json(nlohmann::json& j, const ScriptResult& r) {
    j = nlohmann::json{
        {"exito",  r.exito},
        {"error",  r.error},
        {"lineas", r.lineas}
    };
}