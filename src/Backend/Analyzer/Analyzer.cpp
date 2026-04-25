#include "Analyzer.h"
#include "../DiskManagement/DiskManagement.h"
#include "../FileSystem/FileSystem.h"
#include "../UserManagement/UserManagement.h"
#include "../FileManagement/FileManagement.h"
#include "../Reports/Reports.h"
#include "../../model/structures.h"

#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <stdexcept>

namespace Analyzer {

// ============================================================
//  UTILIDADES DE PARSING
// ============================================================

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Tokeniza respetando comillas dobles
// "mkdisk -size=5 -path=\"/mis discos/d.mia\""
// → ["mkdisk", "-size=5", "-path=\"/mis discos/d.mia\""]
static std::vector<std::string> tokenizar(const std::string& linea) {
    std::vector<std::string> tokens;
    std::string token;
    bool enComillas = false;

    for (char c : linea) {
        if (c == '"') {
            enComillas = !enComillas;
            token += c;
        } else if (c == ' ' && !enComillas) {
            if (!token.empty()) { tokens.push_back(token); token.clear(); }
        } else {
            token += c;
        }
    }
    if (!token.empty()) tokens.push_back(token);
    return tokens;
}

// Convierte ["-size=100", "-unit=K"] → {"size":"100","unit":"k"}
// Las claves y valores se pasan a minúsculas.
// Los flags sin valor (ej: -r, -p) se guardan con valor "".
static std::map<std::string,std::string> parsearParams(
    const std::vector<std::string>& tokens, int desde = 1)
{
    std::map<std::string,std::string> p;
    for (size_t i = desde; i < tokens.size(); i++) {
        std::string t = tokens[i];
        if (t.empty() || t[0] != '-') continue;

        size_t eq = t.find('=');
        if (eq == std::string::npos) {
            // flag sin valor: -r, -p
            p[toLower(t.substr(1))] = "";
            continue;
        }
        std::string clave = toLower(t.substr(1, eq - 1));
        std::string valor = t.substr(eq + 1);

        // Quitar comillas del valor
        if (valor.size() >= 2 && valor.front() == '"' && valor.back() == '"')
            valor = valor.substr(1, valor.size() - 2);

        // El valor se guarda en minúsculas EXCEPTO para paths y nombres
        // (paths son case-sensitive en Linux)
        bool esPath = (clave == "path" || clave == "cont" ||
                       clave == "path_file_ls");

        // file1..file10 también son rutas — preservar case original
        if (!esPath && clave.size() >= 5 && clave.substr(0, 4) == "file") {
            bool todosDigitos = true;
            for (size_t k = 4; k < clave.size(); k++)
                if (!isdigit(clave[k])) { todosDigitos = false; break; }
            if (todosDigitos) esPath = true;
        }

        bool esNombre = (clave == "name" || clave == "user" ||
                         clave == "pass"  || clave == "grp"  ||
                         clave == "id");

        if (!esPath && !esNombre) valor = toLower(valor);

        // Si es un path de disco (.mia o cont) y NO es absoluto,
        // resolverlo relativo a DISCOS_DIR (src/discos/)
        if (esPath && clave != "path_file_ls" && !valor.empty()
            && valor[0] != '/') {
            valor = DISCOS_DIR + "/" + valor;
        }

        p[clave] = valor;
    }
    return p;
}

// Helper: obtener param obligatorio o lanzar error
static std::string getParam(const std::map<std::string,std::string>& p,
                             const std::string& clave,
                             const std::string& cmd)
{
    auto it = p.find(clave);
    if (it == p.end() || it->second.empty())
        throw std::runtime_error(cmd + ": falta el parametro -" + clave);
    return it->second;
}


// ============================================================
//  PROCESADOR DE UNA LÍNEA
// ============================================================
static LineAnalysis ProcesarLinea(const std::string& lineaOrig) {
    LineAnalysis r;
    r.linea  = lineaOrig;
    r.valida = true;

    std::string linea = trim(lineaOrig);

    // Línea vacía
    if (linea.empty()) { r.mensaje = ""; return r; }

    // Comentario (#): se muestra tal cual en el área de salida
    if (linea[0] == '#') { r.mensaje = linea; return r; }

    auto tokens  = tokenizar(linea);
    if (tokens.empty()) { r.mensaje = ""; return r; }

    std::string cmd = toLower(tokens[0]);
    auto params = parsearParams(tokens, 1);

    try {

        // ── ADMINISTRACIÓN DE DISCOS ──────────────────────────

        if (cmd == "mkdisk") {
            int size = std::stoi(getParam(params, "size", "MKDISK"));
            std::string fit  = params.count("fit")  ? params["fit"]  : "ff";
            std::string unit = params.count("unit") ? params["unit"] : "m";
            std::string path = getParam(params, "path", "MKDISK");
            r.mensaje = DiskManagement::Mkdisk(size, fit, unit, path);
        }

        else if (cmd == "rmdisk") {
            std::string path = getParam(params, "path", "RMDISK");
            r.mensaje = DiskManagement::Rmdisk(path);
        }

        else if (cmd == "fdisk") {
            // Verificar si es un comando delete o add
            if (params.count("delete")) {
                // fdisk -delete=fast|full -name=Partition1 -path=/home/Disco1.dk
                std::string path = getParam(params, "path", "FDISK");
                std::string name = getParam(params, "name", "FDISK");
                std::string deleteOpt = params["delete"];
                
                // Validar opción de delete
                if (deleteOpt != "fast" && deleteOpt != "full") {
                    r.valida = false;
                    r.mensaje = "FDISK: valor de -delete inválido. Use 'fast' o 'full'";
                    return r;
                }
                
                r.mensaje = DiskManagement::FdiskDelete(path, name, deleteOpt);
            }
            else if (params.count("add")) {
                // fdisk -add=100 -unit=k -name=Partition1 -path=/home/Disco1.dk
                std::string path = getParam(params, "path", "FDISK");
                std::string name = getParam(params, "name", "FDISK");
                int add = 0;
                
                try {
                    add = std::stoi(params["add"]);
                } catch (...) {
                    r.valida = false;
                    r.mensaje = "FDISK: valor de -add inválido: " + params["add"];
                    return r;
                }
                
                std::string unit = params.count("unit") ? params["unit"] : "k";
                r.mensaje = DiskManagement::FdiskAdd(path, name, add, unit);
            }
            else {
                // fdisk normal para crear partición
                int size = std::stoi(getParam(params, "size", "FDISK"));
                std::string path = getParam(params, "path", "FDISK");
                std::string name = getParam(params, "name", "FDISK");
                std::string type = params.count("type") ? params["type"] : "p";
                std::string fit  = params.count("fit")  ? params["fit"]  : "wf";
                std::string unit = params.count("unit") ? params["unit"] : "k";
                r.mensaje = DiskManagement::Fdisk(size, path, name, type, fit, unit);
            }
        }

        else if (cmd == "mount") {
            std::string path = getParam(params, "path", "MOUNT");
            std::string name = getParam(params, "name", "MOUNT");
            r.mensaje = DiskManagement::Mount(path, name);
        }

        else if (cmd == "mounted") {
            r.mensaje = DiskManagement::Mounted();
        }

        else if (cmd == "mkfs") {
            std::string id   = getParam(params, "id", "MKFS");
            std::string type = params.count("type") ? params["type"] : "full";
            r.mensaje = FileSystem::Mkfs(id, type);
        }

        // ── SISTEMA DE ARCHIVOS (implementar más adelante) ────

        else if (cmd == "cat") {
            // Recolectar todos los -file1, -file2, -file3... en orden
            std::vector<std::string> archivos;
            for (int i = 1; i <= 10; i++) {
                std::string key = "file" + std::to_string(i);
                if (params.count(key)) archivos.push_back(params[key]);
            }
            r.mensaje = FileSystem::Cat(archivos);
        }

        else if (cmd == "login") {
            std::string user = getParam(params, "user", "LOGIN");
            std::string pass = getParam(params, "pass", "LOGIN");
            std::string id   = getParam(params, "id",   "LOGIN");
            r.mensaje = Users::Login(user, pass, id);
        }

        else if (cmd == "logout") {
            r.mensaje = Users::Logout();
        }

        else if (cmd == "mkgrp") {
            std::string name = getParam(params, "name", "MKGRP");
            r.mensaje = Users::Mkgrp(name);
        }

        else if (cmd == "rmgrp") {
            std::string name = getParam(params, "name", "RMGRP");
            r.mensaje = Users::Rmgrp(name);
        }

        else if (cmd == "mkusr") {
            std::string user = getParam(params, "user", "MKUSR");
            std::string pass = getParam(params, "pass", "MKUSR");
            std::string grp  = getParam(params, "grp",  "MKUSR");
            r.mensaje = Users::Mkusr(user, pass, grp);
        }

        else if (cmd == "rmusr") {
            std::string user = getParam(params, "user", "RMUSR");
            r.mensaje = Users::Rmusr(user);
        }

        else if (cmd == "chgrp") {
            std::string user = getParam(params, "user", "CHGRP");
            std::string grp  = getParam(params, "grp",  "CHGRP");
            r.mensaje = Users::Chgrp(user, grp);
        }

        else if (cmd == "mkfile") {
            std::string path = getParam(params, "path", "MKFILE");
            int size = 0;
            if (params.count("size") && !params.at("size").empty()) {
                std::string sizeStr = params.at("size");
                // Detectar números negativos explícitamente
                if (!sizeStr.empty() && sizeStr[0] == '-') {
                    r.valida  = false;
                    r.mensaje = "MKFILE: el valor de -size no puede ser negativo: " + sizeStr;
                    return r;
                }
                try {
                    size = std::stoi(sizeStr);
                } catch (...) {
                    r.valida  = false;
                    r.mensaje = "MKFILE: valor de -size invalido: " + sizeStr;
                    return r;
                }
                if (size < 0) {
                    r.valida  = false;
                    r.mensaje = "MKFILE: el valor de -size no puede ser negativo: " + sizeStr;
                    return r;
                }
            }
            std::string cont = params.count("cont") ? params.at("cont") : "";
            bool rec = params.count("r");
            r.mensaje = FileManagement::Mkfile(path, size, cont, rec);
        }

        else if (cmd == "mkdir") {
            std::string path = getParam(params, "path", "MKDIR");
            bool p = params.count("p");
            r.mensaje = FileManagement::Mkdir(path, p);
        }

        else if (cmd == "unmount") {
            std::string id = getParam(params, "id", "UNMOUNT");
            r.mensaje = FileSystem::Unmount(id);
        }

        else if (cmd == "mkfs") {
            std::string id   = getParam(params, "id", "MKFS");
            std::string type = params.count("type") ? params["type"] : "full";
            r.mensaje = FileSystem::Mkfs(id, type);
        }

        else if (cmd == "remove") {
            std::string path = getParam(params, "path", "REMOVE");
            r.mensaje = FileManagement::Remove(path);
        }

        else if (cmd == "rename") {
            std::string path = getParam(params, "path", "RENAME");
            std::string name = getParam(params, "name", "RENAME");
            r.mensaje = FileManagement::Rename(path, name);
        }

        else if (cmd == "copy") {
            std::string path = getParam(params, "path", "COPY");
            std::string dest = getParam(params, "destino", "COPY");
            r.mensaje = FileManagement::Copy(path, dest);
        }

        else if (cmd == "move") {
            std::string path = getParam(params, "path", "MOVE");
            std::string dest = getParam(params, "destino", "MOVE");
            r.mensaje = FileManagement::Move(path, dest);
        }

        else if (cmd == "find") {
            std::string path = getParam(params, "path", "FIND");
            std::string name = getParam(params, "name", "FIND");
            r.mensaje = FileManagement::Find(path, name);
        }

        else if (cmd == "chown") {
            std::string path = getParam(params, "path", "CHOWN");
            std::string user = getParam(params, "usuario", "CHOWN");
            bool r_flag = params.count("r");
            r.mensaje = FileManagement::Chown(path, user, r_flag);
        }

        else if (cmd == "chmod") {
            std::string path = getParam(params, "path", "CHMOD");
            std::string ugo = getParam(params, "ugo", "CHMOD");
            bool r_flag = params.count("r");
            r.mensaje = FileManagement::Chmod(path, ugo, r_flag);
        }

        else if (cmd == "rep") {
            // Según el enunciado:
            // -name        = tipo de reporte (mbr, ebr, disk, inode, block, etc.)
            // -path        = ruta donde SE GUARDA el reporte (.jpg o .txt)
            // -id          = ID de la partición montada
            // -path_file_ls = opcional, ruta del archivo/carpeta en EXT2 (para file y ls)
            std::string name        = getParam(params, "name", "REP");
            std::string outputPath  = getParam(params, "path", "REP");
            std::string repId       = params.count("id")           ? params["id"]           : "";
            std::string pathFileLs  = params.count("path_file_ls") ? params["path_file_ls"] : "";
            r.mensaje = Reports::Rep(name, outputPath, repId, pathFileLs);
        }

        else if (cmd == "loss") {
            // LOSS: Simular pérdida del sistema de archivos EXT3
            // loss -id=861A
            std::string id = getParam(params, "id", "LOSS");
            r.mensaje = FileSystem::Loss(id);
        }

        else if (cmd == "journaling") {
            // JOURNALING: Mostrar registro de transacciones
            // journaling -id=861A
            std::string id = getParam(params, "id", "JOURNALING");
            r.mensaje = FileSystem::Journaling(id);
        }
        // ==========================================
        else if (cmd == "ls") {
            // Comando interno para el frontend: ls -path=/ruta
            std::string path = getParam(params, "path", "LS");
            r.mensaje = FileManagement::Ls(path);
        }

        // ── COMANDO NO RECONOCIDO ─────────────────────────────
        else {
            r.valida  = false;
            r.mensaje = "ERROR: comando '" + tokens[0] + "' no reconocido.";
        }

    } catch (const std::exception& e) {
        r.valida  = false;
        r.mensaje = std::string("ERROR: ") + e.what();
    }

    return r;
}


// ============================================================
//  PUNTO DE ENTRADA PÚBLICO
// ============================================================
ScriptResult Analyze(const std::string& scriptTexto) {
    ScriptResult resultado;
    resultado.exito = true;

    std::istringstream stream(scriptTexto);
    std::string linea;

    while (std::getline(stream, linea)) {
        LineAnalysis la = ProcesarLinea(linea);
        resultado.lineas.push_back(la);
    }

    return resultado;
}

} // namespace Analyzer