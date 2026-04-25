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
    std::set<std::string> rutas;
    for (const auto& pm : particionesMontadas)
        rutas.insert(pm.path);

    std::ifstream fin(rutasFile());
    if (fin.is_open()) {
        std::string linea;
        while (std::getline(fin, linea)) {
            if (!linea.empty()) rutas.insert(linea);
        }
        fin.close();
    }

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

        std::string id     = std::string(p.part_id,   strnlen(p.part_id,   4));
        std::string nombre = std::string(p.part_name, strnlen(p.part_name, 16));
        if (id.empty()) continue;

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
// ============================================================
static void escanearDiscos() {
    std::set<std::string> rutasEscaneadas;

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

    if (std::filesystem::exists(DISCOS_DIR)) {
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(DISCOS_DIR))
        {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".mia") continue;
            std::string path = entry.path().string();
            if (rutasEscaneadas.count(path)) continue;
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

    // ── /ejecutar (POST + OPTIONS) ───────────────────────────
    CROW_ROUTE(app, "/ejecutar")
        .methods(crow::HTTPMethod::POST, crow::HTTPMethod::OPTIONS)
        ([](const crow::request& req, crow::response& res) {
            if (req.method == crow::HTTPMethod::OPTIONS)
                CorsPreflightHandler(req, res);
            else
                EjecutarScriptHandler(req, res);
        });

    // ── /mounted (GET + OPTIONS) ─────────────────────────────
    CROW_ROUTE(app, "/mounted")
        .methods(crow::HTTPMethod::GET, crow::HTTPMethod::OPTIONS)
        ([](const crow::request& req, crow::response& res) {
            if (req.method == crow::HTTPMethod::OPTIONS)
                CorsPreflightHandler(req, res);
            else
                MountedHandler(req, res);
        });

    // ── /reportes/:filename (GET + OPTIONS) ──────────────────
    CROW_ROUTE(app, "/reportes/<string>")
        .methods(crow::HTTPMethod::GET, crow::HTTPMethod::OPTIONS)
        ([](const crow::request& req, crow::response& res, std::string filename) {
            if (req.method == crow::HTTPMethod::OPTIONS)
                CorsPreflightHandler(req, res);
            else
                ReportesHandler(req, res, filename);
        });

    app.bindaddr("0.0.0.0").port(8080).multithreaded().run();
    return 0;
}