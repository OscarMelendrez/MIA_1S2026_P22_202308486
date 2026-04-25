#include "../external/crow_all.h"   // necesario para la implementacion
#include <nlohmann/json.hpp>
#include "../Analyzer/Analyzer.h"
#include "../model/result.h"
#include "../model/structures.h"

// ============================================================
//  POST /ejecutar
// ============================================================
void EjecutarScriptHandler(const crow::request& req, crow::response& res) {

    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        res.code = 400;
        res.set_header("Content-Type", "application/json");
        res.set_header("Access-Control-Allow-Origin", "*");
        res.write(nlohmann::json{
            {"exito",  false},
            {"error",  "Body debe ser JSON con campo 'script'"},
            {"lineas", nlohmann::json::array()}
        }.dump());
        res.end();
        return;
    }

    if (!body.contains("script") || !body["script"].is_string()) {
        res.code = 400;
        res.set_header("Content-Type", "application/json");
        res.set_header("Access-Control-Allow-Origin", "*");
        res.write(nlohmann::json{
            {"exito",  false},
            {"error",  "Campo 'script' requerido (string)"},
            {"lineas", nlohmann::json::array()}
        }.dump());
        res.end();
        return;
    }

    try {
        ScriptResult resultado = Analyzer::Analyze(
            body["script"].get<std::string>()
        );
        nlohmann::json resp = resultado;
        res.code = 200;
        res.set_header("Content-Type",                "application/json");
        res.set_header("Access-Control-Allow-Origin", "*");
        res.write(resp.dump(4));

    } catch (const std::exception& e) {
        res.code = 500;
        res.set_header("Content-Type", "application/json");
        res.set_header("Access-Control-Allow-Origin", "*");
        res.write(nlohmann::json{
            {"exito",  false},
            {"error",  std::string("Error interno: ") + e.what()},
            {"lineas", nlohmann::json::array()}
        }.dump());
    }

    res.end();
}

// ============================================================
//  GET /mounted
// ============================================================
void MountedHandler(const crow::request& /*req*/, crow::response& res) {
    nlohmann::json lista = nlohmann::json::array();

    for (const auto& pm : particionesMontadas) {
        lista.push_back({
            {"id",          pm.id},
            {"path",        pm.path},
            {"nombre",      pm.nombre},
            {"correlativo", pm.correlativo},
            {"start",       pm.start},
            {"size",        pm.size}
        });
    }

    res.code = 200;
    res.set_header("Content-Type",                "application/json");
    res.set_header("Access-Control-Allow-Origin", "*");
    res.write(nlohmann::json{{"particiones", lista}}.dump(4));
    res.end();
}

// ============================================================
//  OPTIONS /* — CORS preflight
// ============================================================
void CorsPreflightHandler(const crow::request& /*req*/, crow::response& res) {
    res.code = 204;
    res.set_header("Access-Control-Allow-Origin",  "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
    res.end();
}

// ============================================================
//  GET /reportes/:filename
//  Sirve imágenes .jpg y archivos .txt generados por REP
// ============================================================
void ReportesHandler(const crow::request& /*req*/, crow::response& res,
                     const std::string& filename)
{
    // Construir ruta absoluta al archivo
    std::string filePath = DISCOS_DIR + "/reportes/" + filename;

    // Seguridad: no permitir path traversal
    if (filename.find("..") != std::string::npos ||
        filename.find('/') != std::string::npos) {
        res.code = 400;
        res.end();
        return;
    }

    std::ifstream f(filePath, std::ios::binary);
    if (!f.is_open()) {
        res.code = 404;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.write("Archivo no encontrado: " + filename);
        res.end();
        return;
    }

    // Leer contenido
    std::string content((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
    f.close();

    // Content-Type según extensión
    std::string ext = filename.substr(filename.rfind('.') + 1);
    std::string mime = "application/octet-stream";
    if (ext == "jpg" || ext == "jpeg") mime = "image/jpeg";
    else if (ext == "png")             mime = "image/png";
    else if (ext == "txt")             mime = "text/plain; charset=utf-8";

    res.code = 200;
    res.set_header("Content-Type",                mime);
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Cache-Control",               "no-cache");
    res.write(content);
    res.end();
}