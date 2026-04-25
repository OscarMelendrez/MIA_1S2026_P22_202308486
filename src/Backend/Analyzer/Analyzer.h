#pragma once
#include <string>
#include "../model/result.h"

namespace Analyzer {

// Procesa el texto completo del textarea de entrada.
// Devuelve ScriptResult listo para serializar a JSON.
ScriptResult Analyze(const std::string& scriptTexto);

} // namespace Analyzer