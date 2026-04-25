#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <stdexcept>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "../model/result.h"
#include "../model/structures.h"

//  UTILIDADES DE PARSING

// Convierte un string a minusculas (los comandos son case-insensitive)
static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

// Trim de espacios al inicio y fin
static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::vector<std::string> tokenizarLinea(const std::string& linea) {
    std::vector<std::string> tokens;
    std::string token;
    bool enComillas = false;

    for (size_t i = 0; i < linea.size(); i++) {
        char c = linea[i];

        if (c == '"') {
            enComillas = !enComillas;
            // Las comillas se incluyen para poder extraer el valor limpio despues
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

static std::map<std::string, std::string> parsearParametros(
    const std::vector<std::string>& tokens, int desde = 1)
{
    std::map<std::string, std::string> params;

    for (size_t i = desde; i < tokens.size(); i++) {
        std::string t = tokens[i];
        if (t.empty() || t[0] != '-') continue;

        // Buscar el '='
        size_t eq = t.find('=');
        if (eq == std::string::npos) {
            // Parametro sin valor (ej: -r, -p)
            std::string clave = toLower(t.substr(1));
            params[clave] = "";
            continue;
        }

        std::string clave = toLower(t.substr(1, eq - 1));
        std::string valor = t.substr(eq + 1);

        // Quitar comillas del valor si las tiene
        if (!valor.empty() && valor.front() == '"' && valor.back() == '"') {
            valor = valor.substr(1, valor.size() - 2);
        }

        params[clave] = valor;
    }
    return params;
}

template<typename T>
static bool leerStruct(FILE* f, long offset, T& dest) {
    if (fseek(f, offset, SEEK_SET) != 0) return false;
    return fread(&dest, sizeof(T), 1, f) == 1;
}

template<typename T>
static bool escribirStruct(FILE* f, long offset, const T& src) {
    if (fseek(f, offset, SEEK_SET) != 0) return false;
    return fwrite(&src, sizeof(T), 1, f) == 1;
}

// Helper: convierte "B","K","M" a bytes
static long long convertirUnidad(long long valor, const std::string& unit) {
    std::string u = toLower(unit);
    if (u == "k") return valor * 1024LL;
    if (u == "m") return valor * 1024LL * 1024LL;
    return valor; // bytes por defecto
}

// ------------------------------------------------------------
//  MKDISK
// ------------------------------------------------------------
static std::string EjecutarMkdisk(const std::map<std::string, std::string>& params) {
    // Validar parametros obligatorios
    if (params.find("size") == params.end())
        throw std::runtime_error("MKDISK: falta el parametro -size");
    if (params.find("path") == params.end())
        throw std::runtime_error("MKDISK: falta el parametro -path");

    long long size = std::stoll(params.at("size"));
    if (size <= 0)
        throw std::runtime_error("MKDISK: -size debe ser mayor que 0");

    std::string unit = "m"; // default: megabytes
    if (params.find("unit") != params.end()) {
        unit = toLower(params.at("unit"));
        if (unit != "k" && unit != "m")
            throw std::runtime_error("MKDISK: -unit solo acepta K o M");
    }

    char fit = 'F'; // default: first fit
    if (params.find("fit") != params.end()) {
        std::string f = toLower(params.at("fit"));
        if      (f == "bf") fit = 'B';
        else if (f == "ff") fit = 'F';
        else if (f == "wf") fit = 'W';
        else throw std::runtime_error("MKDISK: -fit solo acepta BF, FF o WF");
    }

    std::string path = params.at("path");
    long long tamanoBytes = convertirUnidad(size, unit);

    // Crear directorios si no existen
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    // Crear el archivo binario lleno de ceros
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("MKDISK: no se pudo crear el archivo: " + path);

    // Usar buffer de 1024 bytes para eficiencia
    const int BUF = 1024;
    char buf[BUF] = {0};
    long long restante = tamanoBytes;
    while (restante > 0) {
        long long escribir = (restante >= BUF) ? BUF : restante;
        fwrite(buf, 1, static_cast<size_t>(escribir), f);
        restante -= escribir;
    }

    // Escribir MBR al inicio
    MBR mbr;
    mbr.mbr_tamano         = static_cast<int>(tamanoBytes);
    mbr.mbr_fecha_creacion = time(nullptr);
    mbr.mbr_dsk_signature  = rand();
    mbr.dsk_fit            = fit;
    escribirStruct(f, 0, mbr);

    fclose(f);

    return "MKDISK: disco creado exitosamente en " + path +
           " (" + std::to_string(tamanoBytes) + " bytes)";
}

// ------------------------------------------------------------
//  RMDISK
// ------------------------------------------------------------
static std::string EjecutarRmdisk(const std::map<std::string, std::string>& params) {
    if (params.find("path") == params.end())
        throw std::runtime_error("RMDISK: falta el parametro -path");

    std::string path = params.at("path");
    if (!std::filesystem::exists(path))
        throw std::runtime_error("RMDISK: el archivo no existe: " + path);

    if (std::remove(path.c_str()) != 0)
        throw std::runtime_error("RMDISK: no se pudo eliminar: " + path);

    return "RMDISK: disco eliminado: " + path;
}

// ------------------------------------------------------------
//  FDISK
// ------------------------------------------------------------
static std::string EjecutarFdisk(const std::map<std::string, std::string>& params) {
    if (params.find("path") == params.end())
        throw std::runtime_error("FDISK: falta el parametro -path");
    if (params.find("name") == params.end())
        throw std::runtime_error("FDISK: falta el parametro -name");

    std::string path = params.at("path");
    if (!std::filesystem::exists(path))
        throw std::runtime_error("FDISK: el disco no existe: " + path);

    char tipo = 'P'; // default primaria
    if (params.find("type") != params.end()) {
        std::string t = toLower(params.at("type"));
        if      (t == "p") tipo = 'P';
        else if (t == "e") tipo = 'E';
        else if (t == "l") tipo = 'L';
        else throw std::runtime_error("FDISK: -type invalido (P, E o L)");
    }

    char fit = 'W'; // default worst fit para FDISK
    if (params.find("fit") != params.end()) {
        std::string f = toLower(params.at("fit"));
        if      (f == "bf") fit = 'B';
        else if (f == "ff") fit = 'F';
        else if (f == "wf") fit = 'W';
        else throw std::runtime_error("FDISK: -fit invalido (BF, FF, WF)");
    }

    std::string unit = "k"; // default kilobytes para FDISK
    if (params.find("unit") != params.end()) {
        unit = toLower(params.at("unit"));
        if (unit != "b" && unit != "k" && unit != "m")
            throw std::runtime_error("FDISK: -unit solo acepta B, K o M");
    }

    if (params.find("size") == params.end())
        throw std::runtime_error("FDISK: falta el parametro -size");

    long long size = std::stoll(params.at("size"));
    if (size <= 0)
        throw std::runtime_error("FDISK: -size debe ser mayor que 0");

    long long tamanoBytes = convertirUnidad(size, unit);
    std::string nombre    = params.at("name");

    FILE* f = fopen(path.c_str(), "r+b");
    if (!f) throw std::runtime_error("FDISK: no se pudo abrir: " + path);

    MBR mbr;
    if (!leerStruct(f, 0, mbr)) {
        fclose(f);
        throw std::runtime_error("FDISK: no se pudo leer el MBR");
    }

    if (tipo == 'P' || tipo == 'E') {
        // Contar particiones P+E existentes y verificar restricciones
        int contPE = 0;
        bool hayExtendida = false;
        int slotLibre = -1;

        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_start == -1) {
                if (slotLibre == -1) slotLibre = i;
            } else {
                contPE++;
                if (mbr.mbr_partitions[i].part_type == 'E') hayExtendida = true;
                // Verificar nombre duplicado
                if (std::string(mbr.mbr_partitions[i].part_name) == nombre) {
                    fclose(f);
                    throw std::runtime_error("FDISK: ya existe una particion con ese nombre");
                }
            }
        }

        if (contPE >= 4) {
            fclose(f);
            throw std::runtime_error("FDISK: maximo 4 particiones primarias+extendidas");
        }
        if (tipo == 'E' && hayExtendida) {
            fclose(f);
            throw std::runtime_error("FDISK: ya existe una particion extendida en este disco");
        }
        if (slotLibre == -1) {
            fclose(f);
            throw std::runtime_error("FDISK: no hay slots libres en el MBR");
        }

        // Calcular posicion de inicio segun el ajuste (fit)
        // Construir lista de espacios usados
        std::vector<std::pair<int,int>> usados; 
        usados.push_back({0, static_cast<int>(sizeof(MBR))}); // el MBR mismo

        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_start != -1) {
                int s = mbr.mbr_partitions[i].part_start;
                int e = s + mbr.mbr_partitions[i].part_s;
                usados.push_back({s, e});
            }
        }
        std::sort(usados.begin(), usados.end());

        // Encontrar huecos libres
        struct Hueco { int inicio; int tamaño; };
        std::vector<Hueco> huecos;
        int cursor = static_cast<int>(sizeof(MBR));

        for (auto& u : usados) {
            if (u.first > cursor) {
                huecos.push_back({cursor, u.first - cursor});
            }
            cursor = std::max(cursor, u.second);
        }
        // Hueco al final
        if (cursor < mbr.mbr_tamano) {
            huecos.push_back({cursor, mbr.mbr_tamano - cursor});
        }

        // Aplicar algoritmo de ajuste
        int posInicio = -1;
        if (fit == 'F') { // First Fit
            for (auto& h : huecos) {
                if (h.tamaño >= tamanoBytes) { posInicio = h.inicio; break; }
            }
        } else if (fit == 'B') { // Best Fit
            int mejorTam = INT32_MAX;
            for (auto& h : huecos) {
                if (h.tamaño >= tamanoBytes && h.tamaño < mejorTam) {
                    mejorTam  = h.tamaño;
                    posInicio = h.inicio;
                }
            }
        } else { // Worst Fit
            int peorTam = -1;
            for (auto& h : huecos) {
                if (h.tamaño >= tamanoBytes && h.tamaño > peorTam) {
                    peorTam   = h.tamaño;
                    posInicio = h.inicio;
                }
            }
        }

        if (posInicio == -1) {
            fclose(f);
            throw std::runtime_error("FDISK: no hay suficiente espacio en el disco");
        }

        // Crear la particion
        Partition& p = mbr.mbr_partitions[slotLibre];
        p.part_status = '0';
        p.part_type   = tipo;
        p.part_fit    = fit;
        p.part_start  = posInicio;
        p.part_s      = static_cast<int>(tamanoBytes);
        std::strncpy(p.part_name, nombre.c_str(), 15);
        p.part_name[15] = '\0';
        p.part_correlative = -1;

        // Si es extendida, crear el primer EBR en su inicio
        if (tipo == 'E') {
            EBR ebr;
            ebr.part_start = posInicio;
            ebr.part_s     = 0; // Primer EBR no tiene logica aun
            ebr.part_next  = -1;
            escribirStruct(f, posInicio, ebr);
        }

        escribirStruct(f, 0, mbr);
        fclose(f);

        return "FDISK: particion '" + nombre + "' creada en byte " +
               std::to_string(posInicio) + " (" +
               std::to_string(tamanoBytes) + " bytes)";
    }

    // tipo == 'L' (logica dentro de extendida)
    // Buscar la particion extendida
    int extStart = -1, extSize = 0;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_type == 'E' &&
            mbr.mbr_partitions[i].part_start != -1) {
            extStart = mbr.mbr_partitions[i].part_start;
            extSize  = mbr.mbr_partitions[i].part_s;
            break;
        }
    }
    if (extStart == -1) {
        fclose(f);
        throw std::runtime_error("FDISK: no existe particion extendida para crear logica");
    }

    // Recorrer lista enlazada de EBRs
    int ebrOffset = extStart;
    EBR ebrActual;
    EBR ebrAnterior;
    int offsetAnterior = -1;
    int ocupadoHasta   = extStart + static_cast<int>(sizeof(EBR));

    while (true) {
        if (!leerStruct(f, ebrOffset, ebrActual)) break;

        // Verificar nombre duplicado
        if (ebrActual.part_s > 0 &&
            std::string(ebrActual.part_name) == nombre) {
            fclose(f);
            throw std::runtime_error("FDISK: ya existe una particion logica con ese nombre");
        }

        int fin = ebrActual.part_start + ebrActual.part_s;
        if (fin > ocupadoHasta) ocupadoHasta = fin;

        if (ebrActual.part_next == -1) break; // llegamos al ultimo
        offsetAnterior = ebrOffset;
        ebrAnterior    = ebrActual;
        ebrOffset      = ebrActual.part_next;
    }

    // El nuevo EBR va despues del ultimo espacio ocupado
    int nuevoEbrOffset = (ebrActual.part_s == 0)
                         ? extStart           // primer EBR vacio
                         : ocupadoHasta;

    if (nuevoEbrOffset + static_cast<int>(sizeof(EBR)) + tamanoBytes
        > extStart + extSize) {
        fclose(f);
        throw std::runtime_error("FDISK: particion logica excede el tamaño de la extendida");
    }

    // Crear el nuevo EBR
    EBR nuevoEbr;
    nuevoEbr.part_fit   = fit;
    nuevoEbr.part_start = nuevoEbrOffset + static_cast<int>(sizeof(EBR));
    nuevoEbr.part_s     = static_cast<int>(tamanoBytes);
    nuevoEbr.part_next  = -1;
    std::strncpy(nuevoEbr.part_name, nombre.c_str(), 15);
    nuevoEbr.part_name[15] = '\0';

    if (ebrActual.part_s == 0) {
        // Reemplazar el EBR vacio inicial
        escribirStruct(f, extStart, nuevoEbr);
    } else {
        // Enlazar el EBR anterior con el nuevo
        ebrActual.part_next = nuevoEbrOffset;
        escribirStruct(f, ebrOffset, ebrActual);
        escribirStruct(f, nuevoEbrOffset, nuevoEbr);
    }

    fclose(f);
    return "FDISK: particion logica '" + nombre + "' creada en byte " +
           std::to_string(nuevoEbr.part_start);
}

// ------------------------------------------------------------
//  MOUNT
// ------------------------------------------------------------
static std::string EjecutarMount(const std::map<std::string, std::string>& params) {
    if (params.find("path") == params.end())
        throw std::runtime_error("MOUNT: falta el parametro -path");
    if (params.find("name") == params.end())
        throw std::runtime_error("MOUNT: falta el parametro -name");

    std::string path   = params.at("path");
    std::string nombre = params.at("name");

    if (!std::filesystem::exists(path))
        throw std::runtime_error("MOUNT: disco no encontrado: " + path);

    FILE* f = fopen(path.c_str(), "r+b");
    if (!f) throw std::runtime_error("MOUNT: no se pudo abrir: " + path);

    MBR mbr;
    if (!leerStruct(f, 0, mbr)) {
        fclose(f);
        throw std::runtime_error("MOUNT: no se pudo leer el MBR");
    }

    // Buscar la particion por nombre
    int idx = -1;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_start != -1 &&
            std::string(mbr.mbr_partitions[i].part_name) == nombre) {
            idx = i;
            break;
        }
    }
    if (idx == -1) {
        fclose(f);
        throw std::runtime_error("MOUNT: no se encontro la particion '" + nombre + "'");
    }

    Partition& part = mbr.mbr_partitions[idx];
    if (part.part_type != 'P') {
        fclose(f);
        throw std::runtime_error("MOUNT: solo se montan particiones primarias");
    }

    // Determinar letra y numero del ID
    // Logica: misma letra si es el mismo disco, letra siguiente si es otro disco
    char letra  = 'A';
    int  numero = 1;

    // Contar cuantas particiones del MISMO disco ya estan montadas
    // y cuantos discos DISTINTOS hay (para la letra)
    std::string pathNorm = std::filesystem::canonical(
        std::filesystem::path(path)).string();

    std::vector<std::string> discosYaMontados;
    for (auto& pm : particionesMontadas) {
        std::string pmNorm = pm.path;
        try { pmNorm = std::filesystem::canonical(pm.path).string(); }
        catch (...) {}

        bool discoDiferente = true;
        for (auto& d : discosYaMontados) {
            if (d == pmNorm) { discoDiferente = false; break; }
        }
        if (discoDiferente) discosYaMontados.push_back(pmNorm);
    }

    // Calcular letra segun discos distintos
    int discosDistintos = 0;
    for (auto& d : discosYaMontados) {
        if (d != pathNorm) discosDistintos++;
    }
    letra = static_cast<char>('A' + discosDistintos);

    // Calcular numero de particion en ese disco
    for (auto& pm : particionesMontadas) {
        std::string pmNorm = pm.path;
        try { pmNorm = std::filesystem::canonical(pm.path).string(); }
        catch(...) {}
        if (pmNorm == pathNorm) numero++;
    }

    // Obtener los ultimos 2 digitos del carnet desde variable de entorno
    // (o un valor hardcoded que el estudiante debe cambiar)
    const char* carnet = std::getenv("MIA_CARNET");
    std::string prefijo = carnet ? std::string(carnet).substr(
        std::string(carnet).size() >= 2 ? std::string(carnet).size() - 2 : 0) : "00";

    std::string id = prefijo + std::to_string(numero) + letra;

    // Verificar que no este ya montada
    for (auto& pm : particionesMontadas) {
        if (pm.path == path && pm.nombre == nombre) {
            fclose(f);
            throw std::runtime_error("MOUNT: la particion ya esta montada con id " + pm.id);
        }
    }

    // Actualizar la particion en disco
    part.part_status = '1';
    part.part_correlative = numero;
    std::strncpy(part.part_id, id.c_str(), 3);
    part.part_id[3] = '\0';

    escribirStruct(f, 0, mbr);
    fclose(f);

    // Registrar en memoria
    ParticionMontada pm;
    pm.id          = id;
    pm.path        = path;
    pm.nombre      = nombre;
    pm.start       = part.part_start;
    pm.size        = part.part_s;
    pm.type        = 'P';
    pm.correlativo = numero;
    particionesMontadas.push_back(pm);

    return "MOUNT: particion '" + nombre + "' montada con id " + id;
}

// ------------------------------------------------------------
//  MOUNTED
// ------------------------------------------------------------
static std::string EjecutarMounted() {
    if (particionesMontadas.empty())
        return "MOUNTED: no hay particiones montadas.";

    std::string salida = "MOUNTED: particiones montadas:\n";
    for (auto& pm : particionesMontadas) {
        salida += "  " + pm.id + " -> " + pm.path +
                  " [" + pm.nombre + "]\n";
    }
    return salida;
}

static LineAnalysis ProcesarLinea(const std::string& lineaOriginal) {
    LineAnalysis resultado;
    resultado.linea  = lineaOriginal;
    resultado.valida = true;

    std::string linea = trim(lineaOriginal);

    // Ignorar lineas vacias
    if (linea.empty()) {
        resultado.mensaje = "";
        return resultado;
    }

    // Comentarios (inician con #): mostrarlos tal cual
    if (linea[0] == '#') {
        resultado.mensaje = linea;
        return resultado;
    }

    // Tokenizar y extraer comando
    auto tokens = tokenizarLinea(linea);
    if (tokens.empty()) {
        resultado.mensaje = "";
        return resultado;
    }

    std::string comando = toLower(tokens[0]);
    auto params = parsearParametros(tokens, 1);

    try {
        if      (comando == "mkdisk")  resultado.mensaje = EjecutarMkdisk(params);
        else if (comando == "rmdisk")  resultado.mensaje = EjecutarRmdisk(params);
        else if (comando == "fdisk")   resultado.mensaje = EjecutarFdisk(params);
        else if (comando == "mount")   resultado.mensaje = EjecutarMount(params);
        else if (comando == "mounted") resultado.mensaje = EjecutarMounted();
        else {
            // Comando reconocido en el enunciado pero aun no implementado
            resultado.valida  = false;
            resultado.mensaje = "Comando '" + tokens[0] + "' aun no implementado.";
        }
    } catch (const std::exception& e) {
        resultado.valida  = false;
        resultado.mensaje = std::string("ERROR: ") + e.what();
    }

    return resultado;
}

ScriptResult EjecutarScript(const std::string& scriptTexto) {
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