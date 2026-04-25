#include "FileSystem.h"
#include "../../Utilities/utilities.h"
#include "../../model/structures.h"
#include "../Reports/Reports.h"

#include <iostream>
#include <cstring>
#include <ctime>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <iomanip>

namespace FileSystem {

// ── Helpers ──────────────────────────────────────────────────

static ParticionMontada* buscarMontada(const std::string& id) {
    for (auto& pm : particionesMontadas)
        if (pm.id == id) return &pm;
    return nullptr;
}

static SuperBloque leerSB(std::fstream& file, int partStart) {
    SuperBloque sb;
    if (!Utilities::ReadObject(file, sb, partStart))
        throw std::runtime_error("no se pudo leer el SuperBloque");
    if (sb.s_magic != EXT2_MAGIC)
        throw std::runtime_error("la particion no esta formateada como EXT2");
    return sb;
}

static int resolverRuta(std::fstream& file, const SuperBloque& sb,
                         const std::string& ruta)
{
    if (ruta.empty() || ruta[0] != '/')
        throw std::runtime_error("ruta debe ser absoluta");

    std::vector<std::string> partes;
    std::istringstream ss(ruta);
    std::string parte;
    while (std::getline(ss, parte, '/'))
        if (!parte.empty()) partes.push_back(parte);

    int inodoActual = 0;
    for (const auto& nombre : partes) {
        Inodo inodo;
        Utilities::ReadObject(file, inodo,
            sb.s_inode_start + inodoActual * static_cast<int>(sizeof(Inodo)));

        if (inodo.i_type != INODO_CARPETA) return -1;

        bool encontrado = false;
        for (int b = 0; b < 12 && !encontrado; b++) {
            if (inodo.i_block[b] == -1) continue;

            BloqueCarpeta bloque;
            Utilities::ReadObject(file, bloque,
                sb.s_block_start + inodo.i_block[b] * BLOCK_SIZE);

            for (int e = 0; e < 4 && !encontrado; e++) {
                if (bloque.b_content[e].b_inodo == -1) continue;
                if (std::string(bloque.b_content[e].b_name) == nombre) {
                    inodoActual = bloque.b_content[e].b_inodo;
                    encontrado  = true;
                }
            }
        }
        if (!encontrado) return -1;
    }
    return inodoActual;
}

static bool tienePermisoLectura(const Inodo& inodo) {
    if (sesionActual.esRoot) return true;
    int digito = 0;
    if      (sesionActual.uid == inodo.i_uid) digito = inodo.i_perm[0] - '0';
    else if (sesionActual.gid == inodo.i_gid) digito = inodo.i_perm[1] - '0';
    else                                       digito = inodo.i_perm[2] - '0';
    return digito >= 4;
}


// ============================================================
//  MKFS
// ============================================================
std::string Mkfs(const std::string& id, const std::string& /*type*/) {

    ParticionMontada* pm = buscarMontada(id);
    if (!pm)
        throw std::runtime_error("MKFS: id '" + id + "' no encontrado. Usa MOUNT primero");

    auto file = Utilities::OpenFile(pm->path);
    if (!file.is_open())
        throw std::runtime_error("MKFS: no se pudo abrir: " + pm->path);

    int partStart = pm->start;
    int partSize  = pm->size;

    int n = calcularNumEstructuras(partSize);
    if (n <= 0) {
        file.close();
        throw std::runtime_error("MKFS: particion demasiado pequeña");
    }

    // ── SuperBloque ──────────────────────────────────────────
    std::string fecha = getFechaActual();
    SuperBloque sb;
    sb.s_filesystem_type   = 2;
    sb.s_inodes_count      = n;
    sb.s_blocks_count      = n * 3;
    sb.s_free_inodes_count = n - 2;
    sb.s_free_blocks_count = n * 3 - 2;
    sb.s_mnt_count         = 1;
    sb.s_magic             = EXT2_MAGIC;
    sb.s_inode_s           = static_cast<int>(sizeof(Inodo));
    sb.s_block_s           = BLOCK_SIZE;
    sb.s_bm_inode_start    = partStart + static_cast<int>(sizeof(SuperBloque));
    sb.s_bm_block_start    = sb.s_bm_inode_start + n;
    sb.s_inode_start       = sb.s_bm_block_start + (n * 3);
    sb.s_block_start       = sb.s_inode_start + n * static_cast<int>(sizeof(Inodo));
    sb.s_firts_ino         = sb.s_inode_start + static_cast<int>(sizeof(Inodo));
    sb.s_first_blo         = sb.s_block_start + BLOCK_SIZE;
    std::strncpy(sb.s_mtime,  fecha.c_str(), 18); sb.s_mtime[18]  = '\0';
    std::strncpy(sb.s_umtime, fecha.c_str(), 18); sb.s_umtime[18] = '\0';

    Utilities::WriteObject(file, sb, partStart);

    // ── Bitmaps ──────────────────────────────────────────────
    char cero = '0', uno = '1';
    for (int i = 0; i < n;     i++) { file.seekp(sb.s_bm_inode_start + i); file.write(&cero, 1); }
    for (int i = 0; i < n * 3; i++) { file.seekp(sb.s_bm_block_start + i); file.write(&cero, 1); }

    // Marcar inodos 0 y 1 ocupados (raíz + users.txt)
    file.seekp(sb.s_bm_inode_start);     file.write(&uno, 1);
    file.seekp(sb.s_bm_inode_start + 1); file.write(&uno, 1);
    // Marcar bloques 0 y 1 ocupados
    file.seekp(sb.s_bm_block_start);     file.write(&uno, 1);
    file.seekp(sb.s_bm_block_start + 1); file.write(&uno, 1);

    // ── Inodo raíz (índice 0) ────────────────────────────────
    Inodo root;
    root.i_uid      = 1;
    root.i_gid      = 1;
    root.i_s        = 0;
    root.i_type     = INODO_CARPETA;
    root.i_perm[0]  = '7'; root.i_perm[1] = '7'; root.i_perm[2] = '7';
    root.i_block[0] = 0;
    std::strncpy(root.i_ctime, fecha.c_str(), 18); root.i_ctime[18] = '\0';
    std::strncpy(root.i_mtime, fecha.c_str(), 18); root.i_mtime[18] = '\0';
    std::strncpy(root.i_atime, fecha.c_str(), 18); root.i_atime[18] = '\0';
    Utilities::WriteObject(file, root, sb.s_inode_start);

    // ── Bloque carpeta raíz (índice 0) ───────────────────────
    BloqueCarpeta rootBlock;
    std::strncpy(rootBlock.b_content[0].b_name, ".",         11);
    rootBlock.b_content[0].b_inodo = 0;
    std::strncpy(rootBlock.b_content[1].b_name, "..",        11);
    rootBlock.b_content[1].b_inodo = 0;
    std::strncpy(rootBlock.b_content[2].b_name, "users.txt", 11);
    rootBlock.b_content[2].b_inodo = 1;
    rootBlock.b_content[3].b_inodo = -1;
    Utilities::WriteObject(file, rootBlock, sb.s_block_start);

    // ── Inodo users.txt (índice 1) ───────────────────────────
    std::string usersTxt = "1, G, root\n1, U, root, root, 123\n";
    int usersTxtSize = static_cast<int>(usersTxt.size());

    Inodo inodoUsers;
    inodoUsers.i_uid      = 1;
    inodoUsers.i_gid      = 1;
    inodoUsers.i_s        = usersTxtSize;
    inodoUsers.i_type     = INODO_ARCHIVO;
    inodoUsers.i_perm[0]  = '6'; inodoUsers.i_perm[1] = '6'; inodoUsers.i_perm[2] = '4';
    inodoUsers.i_block[0] = 1;
    std::strncpy(inodoUsers.i_ctime, fecha.c_str(), 18); inodoUsers.i_ctime[18] = '\0';
    std::strncpy(inodoUsers.i_mtime, fecha.c_str(), 18); inodoUsers.i_mtime[18] = '\0';
    std::strncpy(inodoUsers.i_atime, fecha.c_str(), 18); inodoUsers.i_atime[18] = '\0';
    Utilities::WriteObject(file, inodoUsers,
        sb.s_inode_start + static_cast<int>(sizeof(Inodo)));

    // ── Bloque archivo users.txt (índice 1) ──────────────────
    BloqueArchivo bloqueUsers;
    std::memcpy(bloqueUsers.b_content, usersTxt.c_str(),
                std::min(usersTxtSize, BLOCK_SIZE));
    Utilities::WriteObject(file, bloqueUsers, sb.s_block_start + BLOCK_SIZE);

    file.flush();
    file.close();

    return "MKFS: particion '" + pm->nombre + "' (id=" + id +
           ") formateada como EXT2. Inodos=" + std::to_string(n) +
           " Bloques=" + std::to_string(n * 3);
}


// ============================================================
//  CAT
// ============================================================
std::string Cat(const std::vector<std::string>& archivos) {

    if (!sesionActual.activa)
        throw std::runtime_error("CAT: no hay sesion activa. Usa LOGIN primero");
    if (archivos.empty())
        throw std::runtime_error("CAT: especifica al menos -file1=<ruta>");

    ParticionMontada* pm = buscarMontada(sesionActual.idParticion);
    if (!pm) throw std::runtime_error("CAT: particion no encontrada");

    auto file = Utilities::OpenFile(pm->path);
    if (!file.is_open())
        throw std::runtime_error("CAT: no se pudo abrir: " + pm->path);

    SuperBloque sb = leerSB(file, pm->start);
    std::ostringstream salida;

    // Nombre del disco sin extensión para construir ruta física
    // Ej: /home/user/Discos/disco1.mia -> disco1
    std::filesystem::path discPath(pm->path);
    std::string discNombre = discPath.stem().string(); // sin .mia
    // Carpeta espejo física: DISCOS_DIR/<nombre_disco>/
    std::string discoFisicoDir = DISCOS_DIR + "/" + discNombre;

    for (size_t f = 0; f < archivos.size(); f++) {
        const std::string& ruta = archivos[f];

        // ── Intento 1: leer desde EXT2 en el .mia ────────────
        int idxInodo = resolverRuta(file, sb, ruta);

        if (idxInodo != -1) {
            Inodo inodo;
            Utilities::ReadObject(file, inodo,
                sb.s_inode_start + idxInodo * static_cast<int>(sizeof(Inodo)));

            if (inodo.i_type != INODO_ARCHIVO) {
                file.close();
                throw std::runtime_error("CAT: '" + ruta + "' no es un archivo");
            }
            if (!tienePermisoLectura(inodo)) {
                file.close();
                throw std::runtime_error("CAT: sin permiso de lectura: " + ruta);
            }

            for (int b = 0; b < 12; b++) {
                if (inodo.i_block[b] == -1) break;
                BloqueArchivo bloque;
                Utilities::ReadObject(file, bloque,
                    sb.s_block_start + inodo.i_block[b] * BLOCK_SIZE);
                int leidos    = b * BLOCK_SIZE;
                int restantes = inodo.i_s - leidos;
                if (restantes <= 0) break;
                int bytes = (restantes >= BLOCK_SIZE) ? BLOCK_SIZE : restantes;
                salida.write(bloque.b_content, bytes);
            }

        } else {
            // ── Intento 2: leer archivo físico en carpeta del disco ──
            // Construye la ruta física: DISCOS_DIR/<disco>/<ruta_ext2>
            // Ej: ruta=/home/oscar/docs/notas.txt
            //  -> src/discos/disco1/home/oscar/docs/notas.txt
            std::string rutaFisica = discoFisicoDir + ruta;

            if (!std::filesystem::exists(rutaFisica)) {
                file.close();
                throw std::runtime_error("CAT: archivo no encontrado: " + ruta +
                    "\n  (buscado en EXT2 y en: " + rutaFisica + ")");
            }

            std::ifstream fisico(rutaFisica);
            if (!fisico.is_open()) {
                file.close();
                throw std::runtime_error("CAT: no se pudo abrir archivo fisico: " + rutaFisica);
            }

            std::string contenidoFisico((std::istreambuf_iterator<char>(fisico)),
                                          std::istreambuf_iterator<char>());
            fisico.close();
            salida << contenidoFisico;
        }

        if (f < archivos.size() - 1) salida << "\n";
    }

    file.close();
    std::string contenido = salida.str();
    return contenido.empty() ? "CAT: el archivo esta vacio" : contenido;
}

// ============================================================
//  UNMOUNT
// ============================================================
std::string Unmount(const std::string& id) {
    auto it = std::find_if(particionesMontadas.begin(), particionesMontadas.end(),
        [&](const ParticionMontada& pm){ return pm.id == id; });
    
    if (it == particionesMontadas.end())
        throw std::runtime_error("UNMOUNT: particion '" + id + "' no encontrada");
    
    std::string nombre = it->nombre;
    std::string path = it->path;
    
    // Desmontar de memoria
    particionesMontadas.erase(it);
    
    // Actualizar MBR del disco para marcar como desmontada
    auto file = Utilities::OpenFile(path);
    if (file.is_open()) {
        MBR mbr{};
        file.seekg(0);
        file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
        
        for (int i = 0; i < 4; i++) {
            if (mbr.mbr_partitions[i].part_start != -1 &&
                std::string(mbr.mbr_partitions[i].part_name) == nombre) {
                mbr.mbr_partitions[i].part_status = PARTITION_UNMOUNTED;
                std::memset(mbr.mbr_partitions[i].part_id, 0, 4);
                mbr.mbr_partitions[i].part_correlative = -1;
                
                file.seekp(0);
                file.write(reinterpret_cast<char*>(&mbr), sizeof(MBR));
                file.flush();
                break;
            }
        }
        file.close();
    }
    
    return "UNMOUNT: particion '" + id + "' (" + nombre + ") desmontada";
}

// ============================================================
//  LOSS - Simular pérdida del sistema de archivos EXT3
// ============================================================
std::string Loss(const std::string& id) {
    ParticionMontada* pm = buscarMontada(id);
    if (!pm)
        throw std::runtime_error("LOSS: particion '" + id + "' no encontrada o no montada");

    auto file = Utilities::OpenFile(pm->path);
    if (!file.is_open())
        throw std::runtime_error("LOSS: no se pudo abrir disco");

    // Leer MBR para obtener SuperBloque
    MBR mbr{};
    file.seekg(0);
    file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    Partition part{};
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_start == pm->start) {
            part = mbr.mbr_partitions[i];
            break;
        }
    }

    SuperBloque sb{};
    file.seekg(part.part_start);
    file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));

    if (sb.s_magic != EXT2_MAGIC)
        throw std::runtime_error("LOSS: particion no formateada como EXT2");

    // Crear buffer de ceros (\0 - null bytes)
    const int BUFFER_SIZE = 4096;
    char zero_buffer[BUFFER_SIZE] = {0};

    // ── 1. Limpiar bitmap de inodos ──────────────────────────
    std::cout << "Limpiando bitmap de inodos..." << std::endl;
    file.seekp(sb.s_bm_inode_start);
    int bytes_written = 0;
    while (bytes_written < sb.s_inodes_count) {
        int to_write = (sb.s_inodes_count - bytes_written < BUFFER_SIZE) 
                       ? (sb.s_inodes_count - bytes_written) 
                       : BUFFER_SIZE;
        file.write(zero_buffer, to_write);
        bytes_written += to_write;
    }

    // ── 2. Limpiar bitmap de bloques ─────────────────────────
    std::cout << "Limpiando bitmap de bloques..." << std::endl;
    file.seekp(sb.s_bm_block_start);
    bytes_written = 0;
    while (bytes_written < sb.s_blocks_count) {
        int to_write = (sb.s_blocks_count - bytes_written < BUFFER_SIZE) 
                       ? (sb.s_blocks_count - bytes_written) 
                       : BUFFER_SIZE;
        file.write(zero_buffer, to_write);
        bytes_written += to_write;
    }

    // ── 3. Limpiar área de inodos ────────────────────────────
    std::cout << "Limpiando área de inodos..." << std::endl;
    file.seekp(sb.s_inode_start);
    bytes_written = 0;
    long total_inode_bytes = sb.s_inodes_count * static_cast<long>(sizeof(Inodo));
    while (bytes_written < total_inode_bytes) {
        int to_write = (total_inode_bytes - bytes_written < BUFFER_SIZE) 
                       ? (total_inode_bytes - bytes_written) 
                       : BUFFER_SIZE;
        file.write(zero_buffer, to_write);
        bytes_written += to_write;
    }

    // ── 4. Limpiar área de bloques ───────────────────────────
    std::cout << "Limpiando área de bloques..." << std::endl;
    file.seekp(sb.s_block_start);
    bytes_written = 0;
    long total_block_bytes = sb.s_blocks_count * static_cast<long>(BLOCK_SIZE);
    while (bytes_written < total_block_bytes) {
        int to_write = (total_block_bytes - bytes_written < BUFFER_SIZE) 
                       ? (total_block_bytes - bytes_written) 
                       : BUFFER_SIZE;
        file.write(zero_buffer, to_write);
        bytes_written += to_write;
    }

    file.flush();
    file.close();

    std::cout << "Limpieza completada." << std::endl;
    return "LOSS: particion '" + id + "' simulada como perdida. ";
}

// ============================================================
//  JOURNALING - Mostrar registro de transacciones
// ============================================================
// Variable global para almacenar registro de operaciones
static std::vector<RegistroJournal> journalGlobal;

// Función helper para registrar una operación en el journal
void RegistrarOperacion(const std::string& op, const std::string& path, 
                        const std::string& contenido) {
    RegistroJournal reg;
    reg.operacion = op;
    reg.path = path;
    reg.contenido = contenido;
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char buf[20];
    strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M", t);
    reg.fecha = std::string(buf);
    journalGlobal.push_back(reg);
}

std::string Journaling(const std::string& id) {
    ParticionMontada* pm = buscarMontada(id);
    if (!pm)
        throw std::runtime_error("JOURNALING: particion '" + id + "' no encontrada o no montada");

    // Generar imagen usando Reports
    std::string resultado = Reports::GenerarImagenJournaling(journalGlobal, id);
    
    return resultado;
}

} // namespace FileSystem