#include "DiskManagement.h"
#include "../../Utilities/utilities.h"
#include "../../model/structures.h"

#include <filesystem>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <vector>
#include <algorithm>
#include <sstream>
#include <stdexcept>

extern void guardarParticionesMontadas();

namespace DiskManagement {

// ── Helpers ──────────────────────────────────────────────────

static long long toBytes(int val, const std::string& unit) {
    if (unit == "k") return static_cast<long long>(val) * 1024LL;
    if (unit == "m") return static_cast<long long>(val) * 1024LL * 1024LL;
    return static_cast<long long>(val);
}

static char normFit(const std::string& fit) {
    if (fit == "bf" || fit == "b") return 'B';
    if (fit == "ff" || fit == "f") return 'F';
    if (fit == "wf" || fit == "w") return 'W';
    throw std::runtime_error("fit invalido. Usa bf, ff o wf");
}

static ParticionMontada* buscarMontada(const std::string& id) {
    for (auto& pm : particionesMontadas)
        if (pm.id == id) return &pm;
    return nullptr;
}


// ============================================================
//  MKDISK
// ============================================================
std::string Mkdisk(int size, const std::string& fit,
                   const std::string& unit, const std::string& path)
{
    if (fit != "bf" && fit != "ff" && fit != "wf" &&
        fit != "b"  && fit != "f"  && fit != "w")
        throw std::runtime_error("MKDISK: fit debe ser bf, ff o wf");

    if (size <= 0)
        throw std::runtime_error("MKDISK: size debe ser mayor a 0");

    if (unit != "k" && unit != "m")
        throw std::runtime_error("MKDISK: unit debe ser k o m");

    char fitChar = normFit(fit);
    long long tamano = toBytes(size, unit);

    if (!Utilities::CreateFile(path))
        throw std::runtime_error("MKDISK: no se pudo crear: " + path);

    auto file = Utilities::OpenFile(path);
    if (!file.is_open())
        throw std::runtime_error("MKDISK: no se pudo abrir: " + path);

    // Llenar de ceros con buffer de 1024 bytes (igual que el maestro)
    std::vector<char> zeroBuffer(1024, 0);
    for (long long i = 0; i < tamano / 1024; i++) {
        file.seekp(i * 1024);
        file.write(zeroBuffer.data(), 1024);
    }

    // Escribir MBR
    MBR newMBR{};
    newMBR.mbr_tamano        = static_cast<int32_t>(tamano);
    newMBR.mbr_dsk_signature = rand();
    newMBR.dsk_fit           = fitChar;

    // Fecha visible en hex "YYYY-MM-DD HH:MM:SS"
    std::string fecha = getFechaActual();
    std::memcpy(newMBR.mbr_fecha_creacion, fecha.c_str(),
                std::min(fecha.size(), sizeof(newMBR.mbr_fecha_creacion) - 1));

    file.seekp(0);
    file.write(reinterpret_cast<char*>(&newMBR), sizeof(MBR));
    file.flush();
    file.close();

    return "MKDISK: disco creado en '" + path + "' (" +
           std::to_string(tamano) + " bytes, fit=" + fitChar + ")";
}


// ============================================================
//  RMDISK
// ============================================================
std::string Rmdisk(const std::string& path) {
    if (!std::filesystem::exists(path))
        throw std::runtime_error("RMDISK: no existe: " + path);

    if (std::remove(path.c_str()) != 0)
        throw std::runtime_error("RMDISK: no se pudo eliminar: " + path);

    particionesMontadas.erase(
        std::remove_if(particionesMontadas.begin(), particionesMontadas.end(),
            [&](const ParticionMontada& pm){ return pm.path == path; }),
        particionesMontadas.end());

    return "RMDISK: disco eliminado: " + path;
}


// ============================================================
//  FDISK
// ============================================================
std::string Fdisk(int size, const std::string& path,
                  const std::string& name, const std::string& type,
                  const std::string& fit,  const std::string& unit)
{
    if (fit != "b" && fit != "f" && fit != "w" &&
        fit != "bf" && fit != "ff" && fit != "wf")
        throw std::runtime_error("FDISK: fit debe ser b, f o w");

    if (size <= 0)
        throw std::runtime_error("FDISK: size debe ser mayor a 0");

    if (unit != "b" && unit != "k" && unit != "m")
        throw std::runtime_error("FDISK: unit debe ser b, k o m");

    if (name.empty() || name.size() > 15)
        throw std::runtime_error("FDISK: name invalido (1-15 chars)");

    char fitChar = normFit(fit);
    std::string t = type;
    char tipoChar;
    if      (t == "p" || t.empty()) tipoChar = 'P';
    else if (t == "e")              tipoChar = 'E';
    else if (t == "l")              tipoChar = 'L';
    else throw std::runtime_error("FDISK: type invalido (p, e o l)");

    if (!std::filesystem::exists(path))
        throw std::runtime_error("FDISK: disco no encontrado: " + path);

    long long tamano = toBytes(size, unit);

    auto file = Utilities::OpenFile(path);
    if (!file.is_open())
        throw std::runtime_error("FDISK: no se pudo abrir: " + path);

    MBR mbr{};
    file.seekg(0);
    file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    // ── Primaria o Extendida ──────────────────────────────────
    if (tipoChar == 'P' || tipoChar == 'E') {

        int contPE = 0; bool hayExt = false; int slotLibre = -1;

        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_start == -1) {
                if (slotLibre == -1) slotLibre = i;
            } else {
                contPE++;
                if (mbr.mbr_partitions[i].part_type == 'E') hayExt = true;
                if (std::string(mbr.mbr_partitions[i].part_name) == name) {
                    file.close();
                    throw std::runtime_error("FDISK: ya existe particion con ese nombre");
                }
            }
        }

        if (contPE >= 4)
            throw std::runtime_error("FDISK: maximo 4 particiones P+E");
        if (tipoChar == 'E' && hayExt)
            throw std::runtime_error("FDISK: ya existe una extendida");
        if (slotLibre == -1)
            throw std::runtime_error("FDISK: sin slots libres en MBR");

        // Calcular huecos
        struct Hueco { int inicio; int tamaño; };
        std::vector<std::pair<int,int>> usados;
        usados.push_back({0, static_cast<int>(sizeof(MBR))});

        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_start != -1) {
                int s = mbr.mbr_partitions[i].part_start;
                int e = s + mbr.mbr_partitions[i].part_s;
                usados.push_back({s, e});
            }
        }
        std::sort(usados.begin(), usados.end());

        std::vector<Hueco> huecos;
        int cursor = static_cast<int>(sizeof(MBR));
        for (auto& u : usados) {
            if (u.first > cursor) huecos.push_back({cursor, u.first - cursor});
            cursor = std::max(cursor, u.second);
        }
        if (cursor < mbr.mbr_tamano)
            huecos.push_back({cursor, mbr.mbr_tamano - cursor});

        int posInicio = -1;
        if (fitChar == 'F') {
            for (auto& h : huecos)
                if (h.tamaño >= tamano) { posInicio = h.inicio; break; }
        } else if (fitChar == 'B') {
            int mejor = INT32_MAX;
            for (auto& h : huecos)
                if (h.tamaño >= tamano && h.tamaño < mejor)
                    { mejor = h.tamaño; posInicio = h.inicio; }
        } else {
            int peor = -1;
            for (auto& h : huecos)
                if (h.tamaño >= tamano && h.tamaño > peor)
                    { peor = h.tamaño; posInicio = h.inicio; }
        }

        if (posInicio == -1) {
            file.close();
            throw std::runtime_error("FDISK: sin espacio suficiente");
        }

        Partition& p = mbr.mbr_partitions[slotLibre];
        p.part_status      = '0';
        p.part_type        = tipoChar;
        p.part_fit         = fitChar;
        p.part_start       = posInicio;
        p.part_s           = static_cast<int32_t>(tamano);
        p.part_correlative = -1;
        std::memset(p.part_name, 0, 16);
        std::memcpy(p.part_name, name.c_str(), std::min(name.size(), (size_t)15));

        if (tipoChar == 'E') {
            EBR primerEbr{};
            primerEbr.part_start = posInicio + static_cast<int>(sizeof(EBR));
            primerEbr.part_s     = 0;
            primerEbr.part_next  = -1;
            file.seekp(posInicio);
            file.write(reinterpret_cast<char*>(&primerEbr), sizeof(EBR));
        }

        file.seekp(0);
        file.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        file.flush(); file.close();

        return "FDISK: particion '" + name + "' (" +
               std::string(1, tipoChar) + ") creada en byte " +
               std::to_string(posInicio) + " (" +
               std::to_string(tamano) + " bytes)";
    }

    // ── Lógica ───────────────────────────────────────────────
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
        file.close();
        throw std::runtime_error("FDISK: no existe particion extendida");
    }

    int ebrOffset = extStart;
    EBR ebrActual{};
    int ocupadoHasta = extStart + static_cast<int>(sizeof(EBR));

    while (true) {
        file.seekg(ebrOffset);
        file.read(reinterpret_cast<char*>(&ebrActual), sizeof(EBR));
        if (ebrActual.part_s > 0 &&
            std::string(ebrActual.part_name) == name) {
            file.close();
            throw std::runtime_error("FDISK: ya existe logica con ese nombre");
        }
        int fin = ebrActual.part_start + ebrActual.part_s;
        if (fin > ocupadoHasta) ocupadoHasta = fin;
        if (ebrActual.part_next == -1) break;
        ebrOffset = ebrActual.part_next;
    }

    int nuevoOffset = (ebrActual.part_s == 0) ? extStart : ocupadoHasta;

    if (nuevoOffset + static_cast<int>(sizeof(EBR)) + static_cast<int>(tamano)
        > extStart + extSize) {
        file.close();
        throw std::runtime_error("FDISK: la logica excede la extendida");
    }

    EBR nuevoEbr{};
    nuevoEbr.part_fit   = fitChar;
    nuevoEbr.part_start = nuevoOffset + static_cast<int>(sizeof(EBR));
    nuevoEbr.part_s     = static_cast<int>(tamano);
    nuevoEbr.part_next  = -1;
    std::memset(nuevoEbr.part_name, 0, 16);
    std::memcpy(nuevoEbr.part_name, name.c_str(), std::min(name.size(), (size_t)15));

    if (ebrActual.part_s == 0) {
        file.seekp(extStart);
        file.write(reinterpret_cast<char*>(&nuevoEbr), sizeof(EBR));
    } else {
        ebrActual.part_next = nuevoOffset;
        file.seekp(ebrOffset);
        file.write(reinterpret_cast<char*>(&ebrActual), sizeof(EBR));
        file.seekp(nuevoOffset);
        file.write(reinterpret_cast<char*>(&nuevoEbr), sizeof(EBR));
    }

    file.flush(); file.close();
    return "FDISK: particion logica '" + name + "' creada en byte " +
           std::to_string(nuevoEbr.part_start);
}


// ============================================================
//  MOUNT
// ============================================================
std::string Mount(const std::string& path, const std::string& name) {
    if (!std::filesystem::exists(path))
        throw std::runtime_error("MOUNT: disco no encontrado: " + path);

    auto file = Utilities::OpenFile(path);
    if (!file.is_open())
        throw std::runtime_error("MOUNT: no se pudo abrir: " + path);

    MBR mbr{};
    file.seekg(0);
    file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    // Buscar particion por nombre
    int idx = -1;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_start != -1 &&
            std::string(mbr.mbr_partitions[i].part_name) == name) {
            idx = i; break;
        }
    }
    if (idx == -1) {
        file.close();
        throw std::runtime_error("MOUNT: particion '" + name + "' no encontrada");
    }

    Partition& part = mbr.mbr_partitions[idx];
    if (part.part_type != 'P') {
        file.close();
        throw std::runtime_error("MOUNT: solo se montan particiones primarias");
    }

    for (auto& pm : particionesMontadas) {
        if (pm.path == path && pm.nombre == name) {
            file.close();
            throw std::runtime_error("MOUNT: ya montada con id " + pm.id);
        }
    }

    // Generar ID: últimos 2 dígitos carnet + número + letra
    const char* carnet = std::getenv("MIA_CARNET");
    std::string prefijo = "86";
    if (carnet) {
        std::string c(carnet);
        prefijo = c.size() >= 2 ? c.substr(c.size() - 2) : c;
    }

    std::string pathCanon = path;
    try { pathCanon = std::filesystem::canonical(path).string(); } catch (...) {}

    std::vector<std::string> discosUnicos;
    for (auto& pm : particionesMontadas) {
        std::string pc = pm.path;
        try { pc = std::filesystem::canonical(pm.path).string(); } catch (...) {}
        bool nuevo = true;
        for (auto& d : discosUnicos) if (d == pc) { nuevo = false; break; }
        if (nuevo) discosUnicos.push_back(pc);
    }

    char letra = 'A';
    bool discoYaVisto = false;
    for (auto& d : discosUnicos) {
        if (d == pathCanon) { discoYaVisto = true; break; }
        letra++;
    }
    if (!discoYaVisto && !discosUnicos.empty())
        letra = static_cast<char>('A' + discosUnicos.size());

    int numero = 1;
    for (auto& pm : particionesMontadas) {
        std::string pc = pm.path;
        try { pc = std::filesystem::canonical(pm.path).string(); } catch (...) {}
        if (pc == pathCanon) numero++;
    }

    std::string id = prefijo + std::to_string(numero) + letra;

    // Guardar en el MBR del .mia
    part.part_status      = PARTITION_MOUNTED;
    part.part_correlative = numero;
    std::memset(part.part_id, 0, 4);
    for (int c = 0; c < 4 && c < (int)id.size(); c++)
        part.part_id[c] = id[c];

    file.seekp(0);
    file.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    file.flush(); file.close();

    ParticionMontada pm;
    pm.id          = id;
    pm.path        = path;
    pm.nombre      = name;
    pm.start       = part.part_start;
    pm.size        = part.part_s;
    pm.type        = 'P';
    pm.correlativo = numero;
    particionesMontadas.push_back(pm);

    // Guardar ruta del disco para persistencia entre reinicios
    guardarParticionesMontadas();

    return "MOUNT: '" + name + "' montada con id " + id;
}


// ============================================================
//  MOUNTED
// ============================================================
std::string Mounted() {
    if (particionesMontadas.empty())
        return "MOUNTED: no hay particiones montadas.";

    std::ostringstream ss;
    ss << "MOUNTED: particiones montadas ("
       << particionesMontadas.size() << "):\n";
    for (auto& pm : particionesMontadas)
        ss << "  " << pm.id << " -> " << pm.path
           << " [" << pm.nombre << "] inicio=" << pm.start
           << " tamaño=" << pm.size << " bytes\n";
    return ss.str();
}


// ============================================================
//  FDISK DELETE
// ============================================================
std::string FdiskDelete(const std::string& path,
                        const std::string& name,
                        const std::string& deleteOption)
{
    if (deleteOption != "fast" && deleteOption != "full")
        throw std::runtime_error("FDISK: delete debe ser 'fast' o 'full'");

    if (!std::filesystem::exists(path))
        throw std::runtime_error("FDISK: disco no encontrado: " + path);

    auto file = Utilities::OpenFile(path);
    if (!file.is_open())
        throw std::runtime_error("FDISK: no se pudo abrir: " + path);

    MBR mbr{};
    file.seekg(0);
    file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    // Buscar partición primaria o extendida
    int idxPE = -1;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_start != -1 &&
            (mbr.mbr_partitions[i].part_type == 'P' ||
             mbr.mbr_partitions[i].part_type == 'E') &&
            std::string(mbr.mbr_partitions[i].part_name) == name) {
            idxPE = i;
            break;
        }
    }

    if (idxPE == -1) {
        // Buscar como partición lógica dentro de extendida
        int extStart = -1;
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_type == 'E' &&
                mbr.mbr_partitions[i].part_start != -1) {
                extStart = mbr.mbr_partitions[i].part_start;
                break;
            }
        }

        if (extStart == -1) {
            file.close();
            throw std::runtime_error("FDISK: particion '" + name + "' no encontrada");
        }

        // Buscar lógica
        int ebrOffset = extStart;
        EBR ebrActual{};
        int ebrAnteriorOffset = -1;
        EBR ebrAnterior{};
        bool encontrada = false;

        while (true) {
            file.seekg(ebrOffset);
            file.read(reinterpret_cast<char*>(&ebrActual), sizeof(EBR));

            if (ebrActual.part_s > 0 &&
                std::string(ebrActual.part_name) == name) {
                encontrada = true;
                break;
            }

            if (ebrActual.part_next == -1) break;

            ebrAnteriorOffset = ebrOffset;
            ebrAnterior = ebrActual;
            ebrOffset = ebrActual.part_next;
        }

        if (!encontrada) {
            file.close();
            throw std::runtime_error("FDISK: particion logica '" + name + "' no encontrada");
        }

        // Marcar como vacío
        ebrActual.part_s = 0;
        std::memset(ebrActual.part_name, 0, 16);

        if (deleteOption == "full") {
            // Rellenar de ceros desde part_start hasta part_start + tamaño original
            // (pero ya tiene part_s = 0, así que marcamos vacío solamente)
            std::vector<char> zeroBuffer(1024, 0);
            int dataStart = ebrActual.part_start;
            // No podemos escribir ceros sin conocer el tamaño anterior
            // Guardamos el EBR marcado como vacío
        }

        file.seekp(ebrOffset);
        file.write(reinterpret_cast<char*>(&ebrActual), sizeof(EBR));

        // Si había una anterior, actualizar su part_next para saltar esta
        if (ebrAnteriorOffset != -1 && ebrActual.part_next != -1) {
            ebrAnterior.part_next = ebrActual.part_next;
            file.seekp(ebrAnteriorOffset);
            file.write(reinterpret_cast<char*>(&ebrAnterior), sizeof(EBR));
        } else if (ebrAnteriorOffset != -1) {
            ebrAnterior.part_next = -1;
            file.seekp(ebrAnteriorOffset);
            file.write(reinterpret_cast<char*>(&ebrAnterior), sizeof(EBR));
        }

        file.flush();
        file.close();

        // Desmontar si estaba montada
        particionesMontadas.erase(
            std::remove_if(particionesMontadas.begin(), particionesMontadas.end(),
                [&](const ParticionMontada& pm){ return pm.nombre == name && pm.path == path; }),
            particionesMontadas.end());

        return "FDISK: particion logica '" + name + "' eliminada (" +
               deleteOption + " - marcada como vacía)";
    }

    // Es primaria o extendida
    Partition& part = mbr.mbr_partitions[idxPE];

    if (part.part_type == 'E') {
        // Si es extendida, verificar que no tenga lógicas dentro
        int extStart = part.part_start;
        int extSize = part.part_s;
        int ebrOffset = extStart;
        EBR ebrActual{};
        int contLogicas = 0;

        while (true) {
            file.seekg(ebrOffset);
            file.read(reinterpret_cast<char*>(&ebrActual), sizeof(EBR));
            if (ebrActual.part_s > 0) contLogicas++;
            if (ebrActual.part_next == -1) break;
            ebrOffset = ebrActual.part_next;
        }

        if (contLogicas > 0) {
            file.close();
            throw std::runtime_error("FDISK: la extendida contiene particiones logicas. "
                                     "Debe eliminarlas primero");
        }
    }

    // Marcar partición como vacía (part_start = -1)
    part.part_start = -1;
    part.part_s = 0;
    std::memset(part.part_name, 0, 16);
    part.part_status = PARTITION_UNMOUNTED;
    part.part_correlative = -1;
    std::memset(part.part_id, 0, 4);

    if (deleteOption == "full") {
        // Rellenar de ceros el espacio de la partición (para EXT3, importante)
        std::vector<char> zeroBuffer(1024, 0);
        long long inicio = part.part_start;
        long long tamaño = part.part_s;
        for (long long i = 0; i < tamaño / 1024; i++) {
            file.seekp(inicio + i * 1024);
            file.write(zeroBuffer.data(), 1024);
        }
    }

    file.seekp(0);
    file.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    file.flush();
    file.close();

    // Desmontar si estaba montada
    particionesMontadas.erase(
        std::remove_if(particionesMontadas.begin(), particionesMontadas.end(),
            [&](const ParticionMontada& pm){ return pm.nombre == name && pm.path == path; }),
        particionesMontadas.end());

    return "FDISK: particion '" + name + "' eliminada (" +
           deleteOption + " - marca como vacío)";
}


// ============================================================
//  FDISK ADD
// ============================================================
std::string FdiskAdd(const std::string& path,
                     const std::string& name,
                     int add,
                     const std::string& unit)
{
    if (unit != "b" && unit != "k" && unit != "m")
        throw std::runtime_error("FDISK: unit debe ser b, k o m");

    if (!std::filesystem::exists(path))
        throw std::runtime_error("FDISK: disco no encontrado: " + path);

    long long bytesAdd = toBytes(add, unit);
    if (bytesAdd == 0)
        throw std::runtime_error("FDISK: -add debe ser diferente de 0");

    auto file = Utilities::OpenFile(path);
    if (!file.is_open())
        throw std::runtime_error("FDISK: no se pudo abrir: " + path);

    MBR mbr{};
    file.seekg(0);
    file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    // Buscar partición primaria o extendida
    int idxPE = -1;
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_start != -1 &&
            (mbr.mbr_partitions[i].part_type == 'P' ||
             mbr.mbr_partitions[i].part_type == 'E') &&
            std::string(mbr.mbr_partitions[i].part_name) == name) {
            idxPE = i;
            break;
        }
    }

    if (idxPE == -1) {
        // Buscar como partición lógica
        int extStart = -1;
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_type == 'E' &&
                mbr.mbr_partitions[i].part_start != -1) {
                extStart = mbr.mbr_partitions[i].part_start;
                break;
            }
        }

        if (extStart == -1) {
            file.close();
            throw std::runtime_error("FDISK: particion '" + name + "' no encontrada");
        }

        int ebrOffset = extStart;
        EBR ebrActual{};
        bool encontrada = false;

        while (true) {
            file.seekg(ebrOffset);
            file.read(reinterpret_cast<char*>(&ebrActual), sizeof(EBR));

            if (ebrActual.part_s > 0 &&
                std::string(ebrActual.part_name) == name) {
                encontrada = true;
                break;
            }

            if (ebrActual.part_next == -1) break;
            ebrOffset = ebrActual.part_next;
        }

        if (!encontrada) {
            file.close();
            throw std::runtime_error("FDISK: particion logica '" + name + "' no encontrada");
        }

        // Validar nuevo tamaño
        if (bytesAdd < 0 && -bytesAdd >= ebrActual.part_s) {
            file.close();
            throw std::runtime_error("FDISK: no puede restar más del tamaño actual ("
                                     + std::to_string(ebrActual.part_s) + " bytes)");
        }

        // Verificar espacio disponible si agregan bytes
        if (bytesAdd > 0) {
            int extSize = 0;
            for (int i = 0; i < 4; i++) {
                if (mbr.mbr_partitions[i].part_type == 'E' &&
                    mbr.mbr_partitions[i].part_start == extStart) {
                    extSize = mbr.mbr_partitions[i].part_s;
                    break;
                }
            }

            int nuevoTamaño = ebrActual.part_s + static_cast<int>(bytesAdd);
            int extEnd = extStart + extSize;
            int partEnd = ebrActual.part_start + nuevoTamaño;

            if (partEnd > extEnd) {
                file.close();
                throw std::runtime_error("FDISK: no hay espacio suficiente en la extendida "
                                         "para agregar " + std::to_string(bytesAdd) + " bytes");
            }
        }

        ebrActual.part_s += static_cast<int>(bytesAdd);

        file.seekp(ebrOffset);
        file.write(reinterpret_cast<char*>(&ebrActual), sizeof(EBR));
        file.flush();
        file.close();

        return "FDISK: particion logica '" + name + "' modificada. "
               "Nuevo tamaño: " + std::to_string(ebrActual.part_s) + " bytes";
    }

    // Es primaria o extendida
    Partition& part = mbr.mbr_partitions[idxPE];

    // Validar nuevo tamaño
    if (bytesAdd < 0 && -bytesAdd >= part.part_s) {
        file.close();
        throw std::runtime_error("FDISK: no puede restar más del tamaño actual ("
                                 + std::to_string(part.part_s) + " bytes)");
    }

    // Verificar espacio disponible si agregan bytes
    if (bytesAdd > 0) {
        std::vector<std::pair<int,int>> usados;
        usados.push_back({0, static_cast<int>(sizeof(MBR))});

        for (int i = 0; i < 4; i++) {
            if (i != idxPE && mbr.mbr_partitions[i].part_start != -1) {
                int s = mbr.mbr_partitions[i].part_start;
                int e = s + mbr.mbr_partitions[i].part_s;
                usados.push_back({s, e});
            }
        }
        std::sort(usados.begin(), usados.end());

        int partEnd = part.part_start + part.part_s + static_cast<int>(bytesAdd);
        bool espacioOK = (partEnd <= mbr.mbr_tamano);

        if (!espacioOK) {
            file.close();
            throw std::runtime_error("FDISK: no hay espacio suficiente en el disco "
                                     "para agregar " + std::to_string(bytesAdd) + " bytes");
        }
    }

    part.part_s += static_cast<int>(bytesAdd);

    file.seekp(0);
    file.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    file.flush();
    file.close();

    // Actualizar partición montada si aplica
    for (auto& pm : particionesMontadas) {
        if (pm.path == path && pm.nombre == name) {
            pm.size += static_cast<int>(bytesAdd);
        }
    }

    return "FDISK: particion '" + name + "' modificada. "
           "Nuevo tamaño: " + std::to_string(part.part_s) + " bytes";
}

} // namespace DiskManagement