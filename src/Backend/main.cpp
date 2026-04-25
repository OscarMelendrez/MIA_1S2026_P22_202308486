#include "external/crow_all.h"
#include "controller/handler.h"
#include "model/structures.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <set>
#include <map>

// ── Variables globales ───────────────────────────────────────
std::vector<ParticionMontada> particionesMontadas;
SesionActiva                  sesionActual;
std::string                   DISCOS_DIR = "";
std::map<std::string, std::string> reporteRutas;

// Archivo donde guardamos las rutas de discos conocidos
static std::string rutasFile() {
    return DISCOS_DIR + "/discos_rutas.txt";
}

// ============================================================
//  Guardar rutas de todos los discos conocidos
// ============================================================
void guardarParticionesMontadas() {
    // Recopilar rutas únicas de los discos montados
    std::set<std::string> rutas;
    for (const auto& pm : particionesMontadas)
        rutas.insert(pm.path);

    // Leer rutas ya guardadas para no perder las anteriores
    std::ifstream fin(rutasFile());
    if (fin.is_open()) {
        std::string linea;
        while (std::getline(fin, linea)) {
            if (!linea.empty()) rutas.insert(linea);
        }
        fin.close();
    }

    // Escribir todas las rutas
    std::ofstream fout(rutasFile());
    for (const auto& r : rutas)
        fout << r << "\n";
}

// ============================================================
//  Calcular ruta de src/discos/ relativa al ejecutable
// ============================================================
static std::string calcularDirDiscos() {
    std::filesystem::path exe = std::filesystem::canonical("/proc/self/exe");
    std::filesystem::path srcDir = exe.parent_path()  // build/
                                      .parent_path()  // Backend/
                                      .parent_path(); // src/
    return (srcDir / "discos").string();
}

// ============================================================
//  Escanear un .mia y restaurar particiones montadas
// ============================================================
static void escanearDisco(const std::string& path) {
    if (!std::filesystem::exists(path)) return;

    std::ifstream disco(path, std::ios::binary);
    if (!disco.is_open()) return;

    MBR mbr;
    disco.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    disco.close();

    for (int i = 0; i < 4; i++) {
        Partition& p = mbr.mbr_partitions[i];
        if (p.part_start == -1)   continue;
        if (p.part_type  != 'P')  continue;
        if (p.part_status != '1') continue;

        std::string id     = std::string(p.part_id,
                                 strnlen(p.part_id, 4));
        std::string nombre = std::string(p.part_name,
                                 strnlen(p.part_name, 16));
        if (id.empty()) continue;

        // Evitar duplicados
        bool yaExiste = false;
        for (const auto& pm : particionesMontadas)
            if (pm.id == id && pm.path == path)
                { yaExiste = true; break; }
        if (yaExiste) continue;

        ParticionMontada pm;
        pm.id          = id;
        pm.path        = path;
        pm.nombre      = nombre;
        pm.start       = p.part_start;
        pm.size        = p.part_s;
        pm.type        = 'P';
        pm.correlativo = p.part_correlative;
        particionesMontadas.push_back(pm);

        std::cout << "  [OK] Restaurada: " << id
                  << " -> " << path
                  << " [" << nombre << "]\n";
    }
}

// ============================================================
//  Restaurar particiones al iniciar
//  1. Lee discos_rutas.txt (rutas guardadas previamente)
//  2. Escanea src/discos/ recursivamente por si hay .mia nuevos
// ============================================================
static void escanearDiscos() {
    std::set<std::string> rutasEscaneadas;

    // 1. Leer rutas guardadas en discos_rutas.txt
    std::ifstream fin(rutasFile());
    if (fin.is_open()) {
        std::string linea;
        while (std::getline(fin, linea)) {
            if (linea.empty()) continue;
            rutasEscaneadas.insert(linea);
            escanearDisco(linea);
        }
        fin.close();
    }

    // 2. Escanear src/discos/ recursivamente para .mia locales
    if (std::filesystem::exists(DISCOS_DIR)) {
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(DISCOS_DIR))
        {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".mia") continue;
            std::string path = entry.path().string();
            if (rutasEscaneadas.count(path)) continue; // ya lo procesamos
            escanearDisco(path);
        }
    }
}

// ============================================================
int main() {

    DISCOS_DIR = calcularDirDiscos();
    std::error_code ec;
    std::filesystem::create_directories(DISCOS_DIR, ec);

    std::cout << "╔══════════════════════════════════════╗\n";
    std::cout << "║   ExtreamFS Backend  |  Puerto 8080  ║\n";
    std::cout << "╚══════════════════════════════════════╝\n";
    std::cout << "  Carpeta de discos: " << DISCOS_DIR << "\n";
    std::cout << "  Rutas guardadas:   " << rutasFile() << "\n";

    std::cout << "\nRestaurando particiones montadas...\n";
    escanearDiscos();
    if (particionesMontadas.empty())
        std::cout << "  (ninguna particion montada previamente)\n";
    else
        std::cout << "  Total restauradas: "
                  << particionesMontadas.size() << "\n";

    std::cout << "\n  POST /ejecutar  <- ejecutar script\n";
    std::cout << "  GET  /mounted   <- ver particiones montadas\n\n";

    crow::SimpleApp app;

    CROW_ROUTE(app, "/ejecutar")
        .methods(crow::HTTPMethod::POST)
        ([](const crow::request& req, crow::response& res) {
            EjecutarScriptHandler(req, res);
        });

    CROW_ROUTE(app, "/mounted")
        .methods(crow::HTTPMethod::GET)
        ([](const crow::request& req, crow::response& res) {
            MountedHandler(req, res);
        });

    CROW_ROUTE(app, "/reportes/<string>")
        .methods(crow::HTTPMethod::GET)
        ([](const crow::request& req, crow::response& res, std::string filename) {
            ReportesHandler(req, res, filename);
        });

    CROW_ROUTE(app, "/<path>")
        .methods(crow::HTTPMethod::OPTIONS)
        ([](const crow::request& req, crow::response& res, std::string) {
            CorsPreflightHandler(req, res);
        });

    app.port(8080).multithreaded().run();
    return 0;
}