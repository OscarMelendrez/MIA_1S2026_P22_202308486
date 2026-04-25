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
        if (c == '\"') {
            enComillas = !enComillas;
            token += c;
        } else if (c == ' ' && !enComillas) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        } else {
            token += c;
        }
    }
    if (!token.empty()) tokens.push_back(token);
    return tokens;
}

// Retorna un mapa de parámetros. Llaves en minúscula (ej: "size", "path")
static std::map<std::string, std::string> parseParams(const std::vector<std::string>& tokens) {
    std::map<std::string, std::string> params;
    for (size_t i = 1; i < tokens.size(); i++) {
        std::string t = tokens[i];
        if (t.empty() || t[0] != '-') continue;

        size_t eqPos = t.find('=');
        if (eqPos == std::string::npos) {
            // flag booleano (ej: -r)
            std::string key = toLower(t.substr(1));
            params[key] = "true";
        } else {
            // llave=valor
            std::string key = toLower(t.substr(1, eqPos - 1));
            std::string val = t.substr(eqPos + 1);
            // Quitar comillas del valor si las tiene
            if (val.size() >= 2 && val.front() == '\"' && val.back() == '\"') {
                val = val.substr(1, val.size() - 2);
            }
            params[key] = val;
        }
    }
    return params;
}

static std::string getParam(const std::map<std::string, std::string>& params,
                            const std::string& key,
                            const std::string& cmdContext = "")
{
    auto it = params.find(toLower(key));
    if (it != params.end()) return it->second;
    if (!cmdContext.empty())
        throw std::runtime_error(cmdContext + ": falta parametro obligatorio -" + key);
    return "";
}

static int getIntParam(const std::map<std::string, std::string>& params,
                       const std::string& key,
                       const std::string& cmdContext = "")
{
    std::string v = getParam(params, key, cmdContext);
    try {
        return std::stoi(v);
    } catch (...) {
        throw std::runtime_error(cmdContext + ": el parametro -" + key + " debe ser un numero");
    }
}


// ============================================================
//  ANÁLISIS DE UNA SOLA LÍNEA
// ============================================================
LineAnalysis ProcesarLinea(const std::string& linea) {
    LineAnalysis r;
    r.linea  = linea;
    r.valida = true;

    std::string str = trim(linea);
    if (str.empty()) {
        r.valida  = false;
        r.mensaje = "";
        return r;
    }
    if (str[0] == '#') {
        r.valida  = true;
        r.mensaje = "(Comentario ignorado)";
        return r;
    }

    std::vector<std::string> tokens = tokenizar(str);
    if (tokens.empty()) {
        r.valida  = false;
        r.mensaje = "";
        return r;
    }

    std::string cmd = toLower(tokens[0]);
    auto params = parseParams(tokens);

    try {
        // ── DISK MANAGEMENT ───────────────────────────────────
        if (cmd == "mkdisk") {
            int size         = getIntParam(params, "size", "MKDISK");
            std::string path = getParam(params, "path", "MKDISK");
            std::string fit  = toLower(getParam(params, "fit"));
            std::string unit = toLower(getParam(params, "unit"));
            if (fit.empty())  fit  = "ff";
            if (unit.empty()) unit = "m";
            r.mensaje = DiskManagement::Mkdisk(size, fit, unit, path);
        }
        else if (cmd == "rmdisk") {
            std::string path = getParam(params, "path", "RMDISK");
            r.mensaje = DiskManagement::Rmdisk(path);
        }
        else if (cmd == "fdisk") {
            std::string path = getParam(params, "path", "FDISK");
            std::string name = getParam(params, "name", "FDISK");

            auto itDelete = params.find("delete");
            auto itAdd    = params.find("add");

            if (itDelete != params.end()) {
                std::string delOpt = toLower(itDelete->second);
                r.mensaje = DiskManagement::FdiskDelete(path, name, delOpt);
            }
            else if (itAdd != params.end()) {
                int add = 0;
                try { add = std::stoi(itAdd->second); }
                catch (...) { throw std::runtime_error("FDISK: -add debe ser numérico"); }
                std::string unit = toLower(getParam(params, "unit"));
                if (unit.empty()) unit = "k";
                r.mensaje = DiskManagement::FdiskAdd(path, name, add, unit);
            }
            else {
                int size         = getIntParam(params, "size", "FDISK");
                std::string type = toLower(getParam(params, "type"));
                std::string fit  = toLower(getParam(params, "fit"));
                std::string unit = toLower(getParam(params, "unit"));

                if (type.empty()) type = "p";
                if (fit.empty())  fit  = "wf";
                if (unit.empty()) unit = "k";

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
        else if (cmd == "unmount") {
            std::string id = getParam(params, "id", "UNMOUNT");
            r.mensaje = FileSystem::Unmount(id);
        }

        // ── FILE SYSTEM (MKFS) ────────────────────────────────
        else if (cmd == "mkfs") {
            std::string id   = getParam(params, "id", "MKFS");
            std::string type = toLower(getParam(params, "type"));
            if (type.empty()) type = "full";
            r.mensaje = FileSystem::Mkfs(id, type);
        }

        // ── USER MANAGEMENT ───────────────────────────────────
        else if (cmd == "login") {
            std::string user = getParam(params, "user", "LOGIN");
            std::string pass = getParam(params, "pass", "LOGIN");
            std::string id   = getParam(params, "id", "LOGIN");
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
            std::string grp  = getParam(params, "grp", "MKUSR");
            r.mensaje = Users::Mkusr(user, pass, grp);
        }
        else if (cmd == "rmusr") {
            std::string user = getParam(params, "user", "RMUSR");
            r.mensaje = Users::Rmusr(user);
        }
        else if (cmd == "chgrp") {
            std::string user = getParam(params, "user", "CHGRP");
            std::string grp  = getParam(params, "grp", "CHGRP");
            r.mensaje = Users::Chgrp(user, grp);
        }

        // ── FILE MANAGEMENT ───────────────────────────────────
        else if (cmd == "mkfile") {
            std::string path = getParam(params, "path", "MKFILE");
            bool rFlag       = (params.find("r") != params.end());
            int size         = 0;
            if (params.find("size") != params.end())
                size = getIntParam(params, "size");
            std::string cont = getParam(params, "cont");

            r.mensaje = FileManagement::Mkfile(path, size, cont, rFlag);
        }
        else if (cmd == "mkdir") {
            std::string path = getParam(params, "path", "MKDIR");
            bool pFlag       = (params.find("p") != params.end());
            r.mensaje = FileManagement::Mkdir(path, pFlag);
        }
        else if (cmd == "cat") {
            std::vector<std::string> files;
            for (int i = 1; i <= 9; i++) {
                std::string k = "file" + std::to_string(i);
                if (params.find(k) != params.end())
                    files.push_back(params[k]);
            }
            r.mensaje = FileSystem::Cat(files);
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
            std::string destino = getParam(params, "destino", "COPY");
            r.mensaje = FileManagement::Copy(path, destino);
        }
        else if (cmd == "move") {
            std::string path = getParam(params, "path", "MOVE");
            std::string destino = getParam(params, "destino", "MOVE");
            r.mensaje = FileManagement::Move(path, destino);
        }
        else if (cmd == "find") {
            std::string path = getParam(params, "path", "FIND");
            std::string name = getParam(params, "name", "FIND");
            r.mensaje = FileManagement::Find(path, name);
        }
        else if (cmd == "chown") {
            std::string path = getParam(params, "path", "CHOWN");
            std::string usr  = getParam(params, "user", "CHOWN");
            bool rFlag       = (params.find("r") != params.end());
            r.mensaje = FileManagement::Chown(path, usr, rFlag);
        }
        else if (cmd == "chmod") {
            std::string path = getParam(params, "path", "CHMOD");
            std::string ugo  = getParam(params, "ugo", "CHMOD");
            bool rFlag       = (params.find("r") != params.end());
            r.mensaje = FileManagement::Chmod(path, ugo, rFlag);
        }

        // ── REPORTS & UTILS ───────────────────────────────────
        else if (cmd == "rep") {
            std::string name       = toLower(getParam(params, "name", "REP"));
            std::string outputPath = getParam(params, "path", "REP");
            std::string repId      = getParam(params, "id", "REP");
            std::string pathFileLs = (name == "ls" || name == "file") ? params["path_file_ls"] : "";
            r.mensaje = Reports::Rep(name, outputPath, repId, pathFileLs);
        }
        else if (cmd == "loss") {
            std::string id = getParam(params, "id", "LOSS");
            r.mensaje = FileSystem::Loss(id);
        }
        else if (cmd == "journaling") {
            std::string id = getParam(params, "id", "JOURNALING");
            r.mensaje = FileSystem::Journaling(id);
        }
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
        if (!la.linea.empty()) {
            resultado.lineas.push_back(la);
            if (!la.valida) {
                // Si quieres que el script se detenga en el primer error,
                // descomenta la siguiente linea:
                // break; 
            }
        }
    }

    return resultado;
}

} // namespace Analyzer