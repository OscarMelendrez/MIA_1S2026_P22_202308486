#include "FileManagement.h"
#include "../../Utilities/utilities.h"
#include "../../model/structures.h"

#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <functional>

// Declaración externa para registrar operaciones en journaling (definida en FileSystem.cpp)
namespace FileSystem {
    void RegistrarOperacion(const std::string& op, const std::string& path, 
                            const std::string& contenido);
}

namespace FileManagement {

// ── Helpers ──────────────────────────────────────────────────

static ParticionMontada* buscarMontada(const std::string& id) {
    for (auto& pm : particionesMontadas)
        if (pm.id == id) return &pm;
    return nullptr;
}

static std::vector<std::string> dividirRuta(const std::string& ruta) {
    std::vector<std::string> partes;
    std::istringstream ss(ruta);
    std::string parte;
    while (std::getline(ss, parte, '/'))
        if (!parte.empty()) partes.push_back(parte);
    return partes;
}

// Buscar inodo por path, retorna índice o -1
static int buscarInodoPorPath(std::fstream& file, const SuperBloque& sb, const std::string& path) {
    if (path == "/") return 0;
    
    auto partes = dividirRuta(path);
    int currentInodo = 0;
    
    for (const auto& parte : partes) {
        Inodo actual{};
        file.seekg(sb.s_inode_start + currentInodo * sizeof(Inodo));
        file.read(reinterpret_cast<char*>(&actual), sizeof(Inodo));
        
        bool encontrado = false;
        for (int i = 0; i < 12; i++) {
            if (actual.i_block[i] == -1) continue;
            
            BloqueCarpeta bc{};
            file.seekg(sb.s_block_start + actual.i_block[i] * sizeof(BloqueCarpeta));
            file.read(reinterpret_cast<char*>(&bc), sizeof(BloqueCarpeta));
            
            for (int j = 0; j < 4; j++) {
                if (bc.b_content[j].b_inodo != -1 && parte == bc.b_content[j].b_name) {
                    currentInodo = bc.b_content[j].b_inodo;
                    encontrado = true;
                    break;
                }
            }
            if (encontrado) break;
        }
        if (!encontrado) return -1;
    }
    return currentInodo;
}

// Obtener nombre del archivo/carpeta desde el path
static std::string obtenerNombre(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos || pos == path.length() - 1) return "";
    return path.substr(pos + 1);
}

// Obtener ruta padre
static std::string obtenerRutaPadre(const std::string& path) {
    if (path == "/") return "/";
    size_t pos = path.find_last_of('/');
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

// Validar permisos: retorna true si tiene permiso
static bool tienePermiso(const Inodo& inodo, int tipo) {
    // tipo: 0=lectura, 1=escritura, 2=ejecución
    // Formato: i_perm[0]=usuario, i_perm[1]=grupo, i_perm[2]=otros
    char perms = (sesionActual.esRoot) ? '7' : (sesionActual.uid == inodo.i_uid ? inodo.i_perm[0] : inodo.i_perm[2]);
    if (perms < '0' || perms > '7') return true; // Sin restricción si no está definido
    int perm_val = perms - '0';
    return (perm_val & (4 >> tipo)) != 0;
}

// Patrón matching para FIND (soporta * y ?)
static bool compararPatron(const std::string& nombre, const std::string& patron) {
    size_t ni = 0, pi = 0;
    while (pi < patron.length()) {
        if (patron[pi] == '?') {
            if (ni >= nombre.length()) return false;
            ni++;
            pi++;
        } else if (patron[pi] == '*') {
            if (pi == patron.length() - 1) return true;
            pi++;
            while (ni <= nombre.length()) {
                if (compararPatron(nombre.substr(ni), patron.substr(pi))) return true;
                ni++;
            }
            return false;
        } else {
            if (ni >= nombre.length() || nombre[ni] != patron[pi]) return false;
            ni++;
            pi++;
        }
    }
    return ni == nombre.length();
}

// Liberar bloques recursivamente
static void liberarBloquesRecursivo(std::fstream& file, const SuperBloque& sb, const Inodo& inodo) {
    char zero = '0';
    for (int i = 0; i < 12; i++) {
        if (inodo.i_block[i] == -1) continue;
        file.seekp(sb.s_bm_block_start + inodo.i_block[i]);
        file.write(&zero, 1);
    }
}

// Obtener próximo bloque disponible
static int obtenerBloqueLibre(std::fstream& file, const SuperBloque& sb) {
    char status;
    for (int i = 0; i < sb.s_blocks_count; i++) {
        file.seekg(sb.s_bm_block_start + i);
        file.read(&status, 1);
        if (status == '0') return i;
    }
    return -1;
}

// Obtener próximo inodo disponible
static int obtenerInodoLibre(std::fstream& file, const SuperBloque& sb) {
    char status;
    for (int i = 0; i < sb.s_inodes_count; i++) {
        file.seekg(sb.s_bm_inode_start + i);
        file.read(&status, 1);
        if (status == '0') return i;
    }
    return -1;
}

// Encontrar entrada en carpeta y obtener su índice
static std::pair<int, int> encontrarEntradaEnCarpeta(std::fstream& file, const SuperBloque& sb, 
                                                       const Inodo& carpeta, const std::string& nombre) {
    for (int i = 0; i < 12; i++) {
        if (carpeta.i_block[i] == -1) continue;
        
        BloqueCarpeta bc{};
        file.seekg(sb.s_block_start + carpeta.i_block[i] * sizeof(BloqueCarpeta));
        file.read(reinterpret_cast<char*>(&bc), sizeof(BloqueCarpeta));
        
        for (int j = 0; j < 4; j++) {
            if (bc.b_content[j].b_inodo != -1 && nombre == bc.b_content[j].b_name) {
                return {i, j};
            }
        }
    }
    return {-1, -1};
}

// Eliminar entrada de una carpeta
static void eliminarEntradaDeCarpeta(std::fstream& file, const SuperBloque& sb, 
                                      Inodo& carpetaPadre, const std::string& nombre) {
    auto [bloqueIdx, entradaIdx] = encontrarEntradaEnCarpeta(file, sb, carpetaPadre, nombre);
    if (bloqueIdx == -1) throw std::runtime_error("Entrada no encontrada");
    
    BloqueCarpeta bc{};
    file.seekg(sb.s_block_start + carpetaPadre.i_block[bloqueIdx] * sizeof(BloqueCarpeta));
    file.read(reinterpret_cast<char*>(&bc), sizeof(BloqueCarpeta));
    
    bc.b_content[entradaIdx].b_inodo = -1;
    std::memset(bc.b_content[entradaIdx].b_name, 0, sizeof(bc.b_content[entradaIdx].b_name));
    
    file.seekp(sb.s_block_start + carpetaPadre.i_block[bloqueIdx] * sizeof(BloqueCarpeta));
    file.write(reinterpret_cast<char*>(&bc), sizeof(BloqueCarpeta));
}


// ============================================================
//  MKDIR
//  Igual al maestro: recursivo por defecto.
//  Con -p no falla si ya existe.
// ============================================================
std::string Mkdir(const std::string& path, bool p) {

    std::cout << "======INICIO MKDIR======\n";

    if (!sesionActual.activa)
        throw std::runtime_error("MKDIR: no hay sesion activa");

    if (path.empty() || path[0] != '/')
        throw std::runtime_error("MKDIR: path invalido. Debe iniciar con '/'");

    ParticionMontada* pm = buscarMontada(sesionActual.idParticion);
    if (!pm) throw std::runtime_error("MKDIR: particion no encontrada");

    auto file = Utilities::OpenFile(pm->path);
    if (!file.is_open())
        throw std::runtime_error("MKDIR: no se pudo abrir disco");

    // Leer MBR y SuperBloque
    MBR mbr{};
    file.seekg(0);
    file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));

    // Buscar la particion montada en el MBR
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

    if (sb.s_magic != EXT2_MAGIC) {
        file.close();
        throw std::runtime_error("MKDIR: particion no formateada como EXT2");
    }

    // Recorrer path segmento a segmento (igual que el maestro)
    std::stringstream ss(path);
    std::string folderName;
    int currentInodeIndex = 0; // raíz

    while (std::getline(ss, folderName, '/')) {
        if (folderName.empty()) continue;

        if (folderName.length() > 255) {
            file.close();
            throw std::runtime_error("MKDIR: nombre demasiado largo: " + folderName);
        }

        // Leer inodo actual
        Inodo current{};
        file.seekg(sb.s_inode_start + currentInodeIndex * sizeof(Inodo));
        file.read(reinterpret_cast<char*>(&current), sizeof(Inodo));

        // Revisar si la carpeta ya existe
        bool found = false;
        for (int i = 0; i < 12 && !found; i++) {
            if (current.i_block[i] == -1) continue;

            BloqueCarpeta fb{};
            file.seekg(sb.s_block_start + current.i_block[i] * sizeof(BloqueCarpeta));
            file.read(reinterpret_cast<char*>(&fb), sizeof(BloqueCarpeta));

            for (int j = 0; j < 4; j++) {
                if (fb.b_content[j].b_inodo != -1 &&
                    folderName == fb.b_content[j].b_name) {
                    currentInodeIndex = fb.b_content[j].b_inodo;
                    found = true;
                    break;
                }
            }
        }

        if (found) continue; // ya existe, continuar al siguiente nivel

        // =======================
        // Crear nueva carpeta
        // =======================
        if (sb.s_free_inodes_count <= 0 || sb.s_free_blocks_count <= 0) {
            file.close();
            throw std::runtime_error("MKDIR: sin espacio en la particion");
        }

        // Buscar inodo libre
        int newInode = -1;
        char one = '1';
        for (int i = 0; i < sb.s_inodes_count; i++) {
            char status;
            file.seekg(sb.s_bm_inode_start + i);
            file.read(&status, 1);
            if (status == '0') {
                newInode = i;
                file.seekp(sb.s_bm_inode_start + i);
                file.write(&one, 1);
                sb.s_free_inodes_count--;
                break;
            }
        }

        // Buscar bloque libre
        int newBlock = -1;
        for (int i = 0; i < sb.s_blocks_count; i++) {
            char status;
            file.seekg(sb.s_bm_block_start + i);
            file.read(&status, 1);
            if (status == '0') {
                newBlock = i;
                file.seekp(sb.s_bm_block_start + i);
                file.write(&one, 1);
                sb.s_free_blocks_count--;
                break;
            }
        }

        if (newInode == -1 || newBlock == -1) {
            file.close();
            throw std::runtime_error("MKDIR: sin inodos o bloques libres");
        }

        // Crear inodo de la nueva carpeta
        Inodo newFolder{};
        newFolder.i_type     = INODO_CARPETA;
        newFolder.i_perm[0]  = '6'; newFolder.i_perm[1] = '6'; newFolder.i_perm[2] = '4';
        newFolder.i_uid      = sesionActual.uid;
        newFolder.i_gid      = sesionActual.gid;
        newFolder.i_s        = 0;
        newFolder.i_block[0] = newBlock;
        { std::string fa = getFechaActual();
          std::strncpy(newFolder.i_ctime, fa.c_str(), 18); newFolder.i_ctime[18] = '\0';
          std::strncpy(newFolder.i_mtime, fa.c_str(), 18); newFolder.i_mtime[18] = '\0';
          std::strncpy(newFolder.i_atime, fa.c_str(), 18); newFolder.i_atime[18] = '\0'; }

        file.seekp(sb.s_inode_start + newInode * sizeof(Inodo));
        file.write(reinterpret_cast<char*>(&newFolder), sizeof(Inodo));

        // Crear bloque de la nueva carpeta con . y ..
        BloqueCarpeta fb{};
        std::strcpy(fb.b_content[0].b_name, ".");
        fb.b_content[0].b_inodo = newInode;
        std::strcpy(fb.b_content[1].b_name, "..");
        fb.b_content[1].b_inodo = currentInodeIndex;
        fb.b_content[2].b_inodo = -1;
        fb.b_content[3].b_inodo = -1;

        file.seekp(sb.s_block_start + newBlock * sizeof(BloqueCarpeta));
        file.write(reinterpret_cast<char*>(&fb), sizeof(BloqueCarpeta));

        // Insertar en el bloque padre
        // Si todos los bloques del padre están llenos, asignar uno nuevo
        bool inserted = false;

        // Primero intentar en bloques existentes del padre
        for (int i = 0; i < 12 && !inserted; i++) {
            if (current.i_block[i] == -1) continue;

            BloqueCarpeta parentFB{};
            file.seekg(sb.s_block_start + current.i_block[i] * sizeof(BloqueCarpeta));
            file.read(reinterpret_cast<char*>(&parentFB), sizeof(BloqueCarpeta));

            for (int j = 0; j < 4; j++) {
                if (parentFB.b_content[j].b_inodo == -1) {
                    std::strcpy(parentFB.b_content[j].b_name, folderName.c_str());
                    parentFB.b_content[j].b_inodo = newInode;

                    file.seekp(sb.s_block_start + current.i_block[i] * sizeof(BloqueCarpeta));
                    file.write(reinterpret_cast<char*>(&parentFB), sizeof(BloqueCarpeta));
                    inserted = true;
                    break;
                }
            }
        }

        // Si no se insertó, asignar un nuevo bloque al padre
        if (!inserted) {
            // Buscar slot libre en i_block del padre
            int slotLibre = -1;
            for (int i = 0; i < 12; i++) {
                if (current.i_block[i] == -1) { slotLibre = i; break; }
            }

            if (slotLibre == -1) {
                file.close();
                throw std::runtime_error("MKDIR: carpeta padre sin espacio para mas entradas");
            }

            // Buscar bloque libre para el padre
            int newParentBlock = -1;
            for (int i = 0; i < sb.s_blocks_count; i++) {
                char status;
                file.seekg(sb.s_bm_block_start + i);
                file.read(&status, 1);
                if (status == '0') {
                    newParentBlock = i;
                    file.seekp(sb.s_bm_block_start + i);
                    file.write(&one, 1);
                    sb.s_free_blocks_count--;
                    break;
                }
            }

            if (newParentBlock == -1) {
                file.close();
                throw std::runtime_error("MKDIR: sin bloques libres para expandir carpeta padre");
            }

            // Crear nuevo bloque del padre con la nueva entrada
            BloqueCarpeta newParentFB{};
            std::strcpy(newParentFB.b_content[0].b_name, folderName.c_str());
            newParentFB.b_content[0].b_inodo = newInode;
            newParentFB.b_content[1].b_inodo = -1;
            newParentFB.b_content[2].b_inodo = -1;
            newParentFB.b_content[3].b_inodo = -1;

            file.seekp(sb.s_block_start + newParentBlock * sizeof(BloqueCarpeta));
            file.write(reinterpret_cast<char*>(&newParentFB), sizeof(BloqueCarpeta));

            // Actualizar i_block del padre
            current.i_block[slotLibre] = newParentBlock;
            file.seekp(sb.s_inode_start + currentInodeIndex * sizeof(Inodo));
            file.write(reinterpret_cast<char*>(&current), sizeof(Inodo));

            inserted = true;
        }

        if (!inserted) {
            file.close();
            throw std::runtime_error("MKDIR: no se pudo insertar en carpeta padre");
        }

        // Crear carpeta fisica en src/discos/<disco>/<ruta>
        try {
            std::filesystem::path discPath(pm->path);
            std::filesystem::path rutaFisica = std::filesystem::path(DISCOS_DIR)
                                              / discPath.stem() / path.substr(1);
            std::filesystem::create_directories(rutaFisica);
        } catch (const std::exception& e) {
            std::cout << "Aviso: no se pudo crear carpeta fisica: " << e.what() << "\n";
        }

        currentInodeIndex = newInode;
    }

    // Guardar SuperBloque actualizado
    file.seekp(part.part_start);
    file.write(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
    file.flush();
    file.close();

    std::cout << "======FIN MKDIR======\n";
    // Registrar operación en journaling
    FileSystem::RegistrarOperacion("mkdir", path, "-");
    return "MKDIR: carpeta '" + path + "' creada (inodo=" +
           std::to_string(currentInodeIndex) + ")";
}


// ============================================================
//  MKFILE
//  Igual al maestro: lee contenido de archivo real (-cont)
//  o genera contenido numerico si viene -size.
// ============================================================
std::string Mkfile(const std::string& path, int size,
                   const std::string& cont, bool r)
{
    std::cout << "======INICIO MKFILE======\n";

    if (!sesionActual.activa)
        throw std::runtime_error("MKFILE: no hay sesion activa");

    if (path.empty() || path[0] != '/')
        throw std::runtime_error("MKFILE: path invalido. Debe iniciar con '/'");

    if (size < 0)
        throw std::runtime_error("MKFILE: el valor de -size no puede ser negativo: " + std::to_string(size));

    ParticionMontada* pm = buscarMontada(sesionActual.idParticion);
    if (!pm) throw std::runtime_error("MKFILE: particion no encontrada");

    // ── Leer contenido ───────────────────────────────────────
    std::string fileContent;

    if (!cont.empty()) {
        // Leer desde archivo real (igual que el maestro)
        std::ifstream contentFile(cont);
        if (!contentFile.is_open())
            throw std::runtime_error("MKFILE: no se pudo abrir archivo: " + cont);

        std::stringstream buffer;
        buffer << contentFile.rdbuf();
        fileContent = buffer.str();
        contentFile.close();
    } else {
        // Generar contenido numerico 0-9 repetido hasta 'size' bytes
        fileContent.reserve(size);
        for (int i = 0; i < size; i++)
            fileContent += static_cast<char>('0' + (i % 10));
    }

    // ── Abrir disco ──────────────────────────────────────────
    auto file = Utilities::OpenFile(pm->path);
    if (!file.is_open())
        throw std::runtime_error("MKFILE: no se pudo abrir disco");

    // Leer MBR y SuperBloque
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

    if (sb.s_magic != EXT2_MAGIC) {
        file.close();
        throw std::runtime_error("MKFILE: particion no formateada como EXT2");
    }

    // ── Separar path en carpetas y nombre de archivo ─────────
    std::stringstream ss(path);
    std::string token;
    std::vector<std::string> folders;

    while (std::getline(ss, token, '/'))
        if (!token.empty()) folders.push_back(token);

    if (folders.empty()) {
        file.close();
        throw std::runtime_error("MKFILE: path invalido");
    }

    std::string filename = folders.back();
    folders.pop_back();

    int currentInodeIndex = 0; // raíz

    // ── Buscar/crear directorio padre ────────────────────────
    for (auto& folder : folders) {

        Inodo current{};
        file.seekg(sb.s_inode_start + currentInodeIndex * sizeof(Inodo));
        file.read(reinterpret_cast<char*>(&current), sizeof(Inodo));

        bool found = false;

        for (int i = 0; i < 12 && !found; i++) {
            if (current.i_block[i] == -1) continue;

            BloqueCarpeta fb{};
            file.seekg(sb.s_block_start + current.i_block[i] * sizeof(BloqueCarpeta));
            file.read(reinterpret_cast<char*>(&fb), sizeof(BloqueCarpeta));

            for (int j = 0; j < 4; j++) {
                if (fb.b_content[j].b_inodo != -1 &&
                    folder == fb.b_content[j].b_name) {
                    currentInodeIndex = fb.b_content[j].b_inodo;
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            if (!r) {
                file.close();
                throw std::runtime_error(
                    "MKFILE: directorio no existe: " + folder + ". Usa -r para crearlo");
            }

            // Crear carpeta padre con -r
            int newInode = -1, newBlock = -1;
            char one = '1';

            for (int i = 0; i < sb.s_inodes_count; i++) {
                char status;
                file.seekg(sb.s_bm_inode_start + i); file.read(&status, 1);
                if (status == '0') {
                    newInode = i;
                    file.seekp(sb.s_bm_inode_start + i); file.write(&one, 1);
                    sb.s_free_inodes_count--;
                    break;
                }
            }
            for (int i = 0; i < sb.s_blocks_count; i++) {
                char status;
                file.seekg(sb.s_bm_block_start + i); file.read(&status, 1);
                if (status == '0') {
                    newBlock = i;
                    file.seekp(sb.s_bm_block_start + i); file.write(&one, 1);
                    sb.s_free_blocks_count--;
                    break;
                }
            }

            if (newInode == -1 || newBlock == -1) {
                file.close();
                throw std::runtime_error("MKFILE: sin espacio para crear carpeta padre");
            }

            Inodo newFolder{};
            newFolder.i_type     = INODO_CARPETA;
            newFolder.i_perm[0]  = '6'; newFolder.i_perm[1] = '6'; newFolder.i_perm[2] = '4';
            newFolder.i_uid      = sesionActual.uid;
            newFolder.i_gid      = sesionActual.gid;
            newFolder.i_s        = 0;
            newFolder.i_block[0] = newBlock;
            { std::string fa = getFechaActual();
              std::strncpy(newFolder.i_ctime, fa.c_str(), 18); newFolder.i_ctime[18] = '\0';
              std::strncpy(newFolder.i_mtime, fa.c_str(), 18); newFolder.i_mtime[18] = '\0';
              std::strncpy(newFolder.i_atime, fa.c_str(), 18); newFolder.i_atime[18] = '\0'; }

            file.seekp(sb.s_inode_start + newInode * sizeof(Inodo));
            file.write(reinterpret_cast<char*>(&newFolder), sizeof(Inodo));

            BloqueCarpeta fb{};
            std::strcpy(fb.b_content[0].b_name, "."); fb.b_content[0].b_inodo = newInode;
            std::strcpy(fb.b_content[1].b_name, ".."); fb.b_content[1].b_inodo = currentInodeIndex;
            fb.b_content[2].b_inodo = -1; fb.b_content[3].b_inodo = -1;

            file.seekp(sb.s_block_start + newBlock * sizeof(BloqueCarpeta));
            file.write(reinterpret_cast<char*>(&fb), sizeof(BloqueCarpeta));

            // Insertar en padre
            Inodo parentInode{};
            file.seekg(sb.s_inode_start + currentInodeIndex * sizeof(Inodo));
            file.read(reinterpret_cast<char*>(&parentInode), sizeof(Inodo));

            for (int i = 0; i < 12; i++) {
                if (parentInode.i_block[i] == -1) continue;
                BloqueCarpeta parentFB{};
                file.seekg(sb.s_block_start + parentInode.i_block[i] * sizeof(BloqueCarpeta));
                file.read(reinterpret_cast<char*>(&parentFB), sizeof(BloqueCarpeta));
                bool ins = false;
                for (int j = 0; j < 4; j++) {
                    if (parentFB.b_content[j].b_inodo == -1) {
                        std::strcpy(parentFB.b_content[j].b_name, folder.c_str());
                        parentFB.b_content[j].b_inodo = newInode;
                        file.seekp(sb.s_block_start + parentInode.i_block[i] * sizeof(BloqueCarpeta));
                        file.write(reinterpret_cast<char*>(&parentFB), sizeof(BloqueCarpeta));
                        ins = true; break;
                    }
                }
                if (ins) break;
            }

            currentInodeIndex = newInode;
        }
    }

    // ── Verificar que el archivo no exista ya ────────────────
    Inodo parentInode{};
    file.seekg(sb.s_inode_start + currentInodeIndex * sizeof(Inodo));
    file.read(reinterpret_cast<char*>(&parentInode), sizeof(Inodo));

    for (int i = 0; i < 12; i++) {
        if (parentInode.i_block[i] == -1) continue;
        BloqueCarpeta fb{};
        file.seekg(sb.s_block_start + parentInode.i_block[i] * sizeof(BloqueCarpeta));
        file.read(reinterpret_cast<char*>(&fb), sizeof(BloqueCarpeta));
        for (int j = 0; j < 4; j++) {
            if (fb.b_content[j].b_inodo != -1 &&
                filename == fb.b_content[j].b_name) {
                file.close();
                throw std::runtime_error("MKFILE: el archivo '" + path + "' ya existe");
            }
        }
    }

    // ── Buscar inodo libre ───────────────────────────────────
    int newInode = -1;
    char one = '1';

    for (int i = 0; i < sb.s_inodes_count; i++) {
        char status;
        file.seekg(sb.s_bm_inode_start + i);
        file.read(&status, 1);
        if (status == '0') {
            newInode = i;
            file.seekp(sb.s_bm_inode_start + i);
            file.write(&one, 1);
            sb.s_free_inodes_count--;
            break;
        }
    }

    if (newInode == -1) {
        file.close();
        throw std::runtime_error("MKFILE: sin inodos libres");
    }

    // ── Crear inodo del archivo ──────────────────────────────
    Inodo fileInode{};
    fileInode.i_uid     = sesionActual.uid;
    fileInode.i_gid     = sesionActual.gid;
    fileInode.i_s       = static_cast<int>(fileContent.size());
    fileInode.i_type    = INODO_ARCHIVO;
    fileInode.i_perm[0] = '6'; fileInode.i_perm[1] = '6'; fileInode.i_perm[2] = '4';
    { std::string fa = getFechaActual();
      std::strncpy(fileInode.i_ctime, fa.c_str(), 18); fileInode.i_ctime[18] = '\0';
      std::strncpy(fileInode.i_mtime, fa.c_str(), 18); fileInode.i_mtime[18] = '\0';
      std::strncpy(fileInode.i_atime, fa.c_str(), 18); fileInode.i_atime[18] = '\0'; }

    // ── Escribir bloques de archivo (igual que el maestro) ───
    int blockIndex = 0;

    for (size_t i = 0; i < fileContent.size() && blockIndex < 12; i += 64) {

        int newBlock = -1;
        for (int j = 0; j < sb.s_blocks_count; j++) {
            char status;
            file.seekg(sb.s_bm_block_start + j);
            file.read(&status, 1);
            if (status == '0') {
                newBlock = j;
                file.seekp(sb.s_bm_block_start + j);
                file.write(&one, 1);
                sb.s_free_blocks_count--;
                break;
            }
        }

        if (newBlock == -1) {
            file.close();
            throw std::runtime_error("MKFILE: sin bloques libres");
        }

        BloqueArchivo fb{};
        std::string chunk = fileContent.substr(i, 64);
        std::memcpy(fb.b_content, chunk.c_str(), chunk.size());

        file.seekp(sb.s_block_start + newBlock * sizeof(BloqueArchivo));
        file.write(reinterpret_cast<char*>(&fb), sizeof(BloqueArchivo));

        fileInode.i_block[blockIndex++] = newBlock;
    }

    // ── Guardar inodo del archivo ────────────────────────────
    file.seekp(sb.s_inode_start + newInode * sizeof(Inodo));
    file.write(reinterpret_cast<char*>(&fileInode), sizeof(Inodo));

    // ── Agregar a carpeta padre ──────────────────────────────
    file.seekg(sb.s_inode_start + currentInodeIndex * sizeof(Inodo));
    file.read(reinterpret_cast<char*>(&parentInode), sizeof(Inodo));

    bool inserted = false;
    for (int i = 0; i < 12 && !inserted; i++) {
        if (parentInode.i_block[i] == -1) continue;

        BloqueCarpeta fb{};
        file.seekg(sb.s_block_start + parentInode.i_block[i] * sizeof(BloqueCarpeta));
        file.read(reinterpret_cast<char*>(&fb), sizeof(BloqueCarpeta));

        for (int j = 0; j < 4; j++) {
            if (fb.b_content[j].b_inodo == -1) {
                std::strcpy(fb.b_content[j].b_name, filename.c_str());
                fb.b_content[j].b_inodo = newInode;

                file.seekp(sb.s_block_start + parentInode.i_block[i] * sizeof(BloqueCarpeta));
                file.write(reinterpret_cast<char*>(&fb), sizeof(BloqueCarpeta));
                inserted = true;
                break;
            }
        }
    }

    if (!inserted)
        std::cout << "MKFILE: aviso - no se pudo insertar en carpeta padre\n";

    // ── Guardar SuperBloque ──────────────────────────────────
    file.seekp(part.part_start);
    file.write(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
    file.flush();

    // ── Crear archivo fisico en src/discos/<disco>/<ruta> ────
    try {
        std::filesystem::path discPath(pm->path);
        std::string rutaRelativa = path.substr(1); // quitar '/' inicial
        std::filesystem::path rutaFisica = std::filesystem::path(DISCOS_DIR)
                                          / discPath.stem() / rutaRelativa;

        // Crear directorios padre si no existen
        std::filesystem::create_directories(rutaFisica.parent_path());

        std::ofstream realFile(rutaFisica);
        if (realFile.is_open()) {
            realFile << fileContent;
            realFile.close();
        }
    } catch (const std::exception& e) {
        std::cout << "Aviso: no se pudo crear archivo fisico: " << e.what() << "\n";
    }

    file.close();

    std::cout << "======FIN MKFILE======\n";
    // Registrar operación en journaling
    FileSystem::RegistrarOperacion("mkfile", path, std::to_string(fileContent.size()));
    return "MKFILE: archivo '" + path + "' creado (" +
           std::to_string(fileContent.size()) + " bytes, inodo=" +
           std::to_string(newInode) + ")";
}

// ============================================================
//  REMOVE - Eliminar archivo o carpeta
// ============================================================
std::string Remove(const std::string& path) {
    if (!sesionActual.activa)
        throw std::runtime_error("REMOVE: no hay sesion activa");
    
    if (path.empty() || path[0] != '/')
        throw std::runtime_error("REMOVE: path invalido");
    
    ParticionMontada* pm = buscarMontada(sesionActual.idParticion);
    if (!pm) throw std::runtime_error("REMOVE: particion no encontrada");
    
    auto file = Utilities::OpenFile(pm->path);
    if (!file.is_open()) throw std::runtime_error("REMOVE: no se pudo abrir disco");
    
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
        throw std::runtime_error("REMOVE: particion no formateada");
    
    int inodoIdx = buscarInodoPorPath(file, sb, path);
    if (inodoIdx == -1)
        throw std::runtime_error("REMOVE: ruta no existe, no tiene permiso o no es accesible");
    
    Inodo inodo{};
    file.seekg(sb.s_inode_start + inodoIdx * sizeof(Inodo));
    file.read(reinterpret_cast<char*>(&inodo), sizeof(Inodo));
    
    // Validar permisos de escritura en carpeta padre
    std::string rutaPadre = obtenerRutaPadre(path);
    int inodoPadreIdx = buscarInodoPorPath(file, sb, rutaPadre);
    if (inodoPadreIdx == -1) throw std::runtime_error("REMOVE: carpeta padre no existe");
    
    Inodo inodoPadre{};
    file.seekg(sb.s_inode_start + inodoPadreIdx * sizeof(Inodo));
    file.read(reinterpret_cast<char*>(&inodoPadre), sizeof(Inodo));
    
    if (!sesionActual.esRoot && sesionActual.uid != inodoPadre.i_uid)
        throw std::runtime_error("REMOVE: no tienes permiso de escritura en la carpeta padre");
    
    // Liberar bloques del inodo
    liberarBloquesRecursivo(file, sb, inodo);
    
    // Marcar inodo como libre
    char zero = '0';
    file.seekp(sb.s_bm_inode_start + inodoIdx);
    file.write(&zero, 1);
    sb.s_free_inodes_count++;
    
    // Eliminar entrada de carpeta padre
    std::string nombre = obtenerNombre(path);
    eliminarEntradaDeCarpeta(file, sb, inodoPadre, nombre);
    
    // Actualizar SuperBloque
    file.seekp(part.part_start);
    file.write(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
    
    file.close();
    // Registrar operación en journaling
    FileSystem::RegistrarOperacion("remove", path, "-");
    return "REMOVE: '" + path + "' eliminado exitosamente";
}

// ============================================================
//  5. RENAME - Renombrar archivo o carpeta
// ============================================================
std::string Rename(const std::string& path, const std::string& name) {
    if (!sesionActual.activa)
        throw std::runtime_error("RENAME: no hay sesion activa");
    
    if (path.empty() || path[0] != '/')
        throw std::runtime_error("RENAME: path invalido");
    
    if (name.empty() || name.length() > 255)
        throw std::runtime_error("RENAME: nombre invalido o demasiado largo");
    
    if (name.find(' ') == std::string::npos && name.find(' ') != std::string::npos)
        throw std::runtime_error("RENAME: espacios en blanco no permitidos");
    
    ParticionMontada* pm = buscarMontada(sesionActual.idParticion);
    if (!pm) throw std::runtime_error("RENAME: particion no encontrada");
    
    auto file = Utilities::OpenFile(pm->path);
    if (!file.is_open()) throw std::runtime_error("RENAME: no se pudo abrir disco");
    
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
        throw std::runtime_error("RENAME: particion no formateada");
    
    // Obtener carpeta padre
    std::string rutaPadre = obtenerRutaPadre(path);
    std::string nombreActual = obtenerNombre(path);
    int inodoPadreIdx = buscarInodoPorPath(file, sb, rutaPadre);
    
    if (inodoPadreIdx == -1)
        throw std::runtime_error("RENAME: carpeta padre no existe");
    
    Inodo inodoPadre{};
    file.seekg(sb.s_inode_start + inodoPadreIdx * sizeof(Inodo));
    file.read(reinterpret_cast<char*>(&inodoPadre), sizeof(Inodo));
    
    if (!sesionActual.esRoot && sesionActual.uid != inodoPadre.i_uid)
        throw std::runtime_error("RENAME: no tienes permiso para renombrar");
    
    // Buscar y actualizar la entrada
    auto [bloqueIdx, entradaIdx] = encontrarEntradaEnCarpeta(file, sb, inodoPadre, nombreActual);
    if (bloqueIdx == -1)
        throw std::runtime_error("RENAME: archivo no encontrado");
    
    BloqueCarpeta bc{};
    file.seekg(sb.s_block_start + inodoPadre.i_block[bloqueIdx] * sizeof(BloqueCarpeta));
    file.read(reinterpret_cast<char*>(&bc), sizeof(BloqueCarpeta));
    
    std::memset(bc.b_content[entradaIdx].b_name, 0, sizeof(bc.b_content[entradaIdx].b_name));
    std::strncpy(bc.b_content[entradaIdx].b_name, name.c_str(), name.length());
    
    file.seekp(sb.s_block_start + inodoPadre.i_block[bloqueIdx] * sizeof(BloqueCarpeta));
    file.write(reinterpret_cast<char*>(&bc), sizeof(BloqueCarpeta));
    
    file.close();
    // Registrar operación en journaling
    FileSystem::RegistrarOperacion("rename", path, name);
    return "RENAME: '" + path + "' renombrado a '" + name + "'";
}

// ============================================================
//  6. COPY - Copiar archivo o carpeta
// ============================================================
std::string Copy(const std::string& path, const std::string& destino) {
    if (!sesionActual.activa)
        throw std::runtime_error("COPY: no hay sesion activa");
    
    if (path.empty() || path[0] != '/')
        throw std::runtime_error("COPY: path invalido");
    
    if (destino.empty() || destino[0] != '/')
        throw std::runtime_error("COPY: destino invalido");
    
    ParticionMontada* pm = buscarMontada(sesionActual.idParticion);
    if (!pm) throw std::runtime_error("COPY: particion no encontrada");
    
    auto file = Utilities::OpenFile(pm->path);
    if (!file.is_open()) throw std::runtime_error("COPY: no se pudo abrir disco");
    
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
        throw std::runtime_error("COPY: particion no formateada");
    
    int inodoOrigen = buscarInodoPorPath(file, sb, path);
    if (inodoOrigen == -1)
        throw std::runtime_error("COPY: archivo no existe o no tiene permiso de lectura");
    
    Inodo origen{};
    file.seekg(sb.s_inode_start + inodoOrigen * sizeof(Inodo));
    file.read(reinterpret_cast<char*>(&origen), sizeof(Inodo));
    
    if (!tienePermiso(origen, 0))
        throw std::runtime_error("COPY: no tienes permiso de lectura");
    
    // Validar destino
    std::string rutaPadreDest = obtenerRutaPadre(destino);
    std::string nombreDest = obtenerNombre(destino);
    int inodoPadreDest = buscarInodoPorPath(file, sb, rutaPadreDest);
    
    if (inodoPadreDest == -1)
        throw std::runtime_error("COPY: carpeta destino no existe");
    
    Inodo padreDest{};
    file.seekg(sb.s_inode_start + inodoPadreDest * sizeof(Inodo));
    file.read(reinterpret_cast<char*>(&padreDest), sizeof(Inodo));
    
    if (!sesionActual.esRoot && sesionActual.uid != padreDest.i_uid)
        throw std::runtime_error("COPY: no tienes permiso de escritura en destino");
    
    // Crear nuevo inodo
    int nuevoInodo = obtenerInodoLibre(file, sb);
    if (nuevoInodo == -1)
        throw std::runtime_error("COPY: sin espacio para inodos");
    
    char uno = '1';
    file.seekp(sb.s_bm_inode_start + nuevoInodo);
    file.write(&uno, 1);
    sb.s_free_inodes_count--;
    
    // Copiar bloques
    Inodo copia = origen;
    copia.i_uid = sesionActual.uid;
    copia.i_gid = sesionActual.gid;
    std::strcpy(copia.i_mtime, getFechaActual().c_str());
    
    for (int i = 0; i < 12; i++) {
        if (origen.i_block[i] == -1) continue;
        
        int nuevoBloque = obtenerBloqueLibre(file, sb);
        if (nuevoBloque == -1)
            throw std::runtime_error("COPY: sin espacio en bloques");
        
        file.seekp(sb.s_bm_block_start + nuevoBloque);
        file.write(&uno, 1);
        sb.s_free_blocks_count--;
        
        // Copiar contenido del bloque
        BloqueArchivo contenido{};
        file.seekg(sb.s_block_start + origen.i_block[i] * sizeof(BloqueArchivo));
        file.read(reinterpret_cast<char*>(&contenido), sizeof(BloqueArchivo));
        
        file.seekp(sb.s_block_start + nuevoBloque * sizeof(BloqueArchivo));
        file.write(reinterpret_cast<char*>(&contenido), sizeof(BloqueArchivo));
        
        copia.i_block[i] = nuevoBloque;
    }
    
    // Guardar nuevo inodo
    file.seekp(sb.s_inode_start + nuevoInodo * sizeof(Inodo));
    file.write(reinterpret_cast<char*>(&copia), sizeof(Inodo));
    
    // Agregar entrada en carpeta padre destino
    bool agregado = false;
    for (int i = 0; i < 12; i++) {
        if (padreDest.i_block[i] == -1) continue;
        
        BloqueCarpeta bc{};
        file.seekg(sb.s_block_start + padreDest.i_block[i] * sizeof(BloqueCarpeta));
        file.read(reinterpret_cast<char*>(&bc), sizeof(BloqueCarpeta));
        
        for (int j = 0; j < 4; j++) {
            if (bc.b_content[j].b_inodo == -1) {
                bc.b_content[j].b_inodo = nuevoInodo;
                std::strncpy(bc.b_content[j].b_name, nombreDest.c_str(), nombreDest.length());
                
                file.seekp(sb.s_block_start + padreDest.i_block[i] * sizeof(BloqueCarpeta));
                file.write(reinterpret_cast<char*>(&bc), sizeof(BloqueCarpeta));
                agregado = true;
                break;
            }
        }
        if (agregado) break;
    }
    
    if (!agregado)
        throw std::runtime_error("COPY: no hay espacio en carpeta destino");
    
    // Actualizar SuperBloque
    file.seekp(part.part_start);
    file.write(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
    
    file.close();
    return "COPY: '" + path + "' copiado a '" + destino + "'";
}

// ============================================================
//  7. MOVE - Mover archivo o carpeta
// ============================================================
std::string Move(const std::string& path, const std::string& destino) {
    if (!sesionActual.activa)
        throw std::runtime_error("MOVE: no hay sesion activa");
    
    if (path.empty() || path[0] != '/')
        throw std::runtime_error("MOVE: path invalido");
    
    if (destino.empty() || destino[0] != '/')
        throw std::runtime_error("MOVE: destino invalido");
    
    ParticionMontada* pm = buscarMontada(sesionActual.idParticion);
    if (!pm) throw std::runtime_error("MOVE: particion no encontrada");
    
    auto file = Utilities::OpenFile(pm->path);
    if (!file.is_open()) throw std::runtime_error("MOVE: no se pudo abrir disco");
    
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
        throw std::runtime_error("MOVE: particion no formateada");
    
    // Validar origen
    std::string rutaPadreOrigen = obtenerRutaPadre(path);
    std::string nombreOrigen = obtenerNombre(path);
    int inodoPadreOrigen = buscarInodoPorPath(file, sb, rutaPadreOrigen);
    
    if (inodoPadreOrigen == -1)
        throw std::runtime_error("MOVE: ruta origen no existe");
    
    Inodo padreOrigen{};
    file.seekg(sb.s_inode_start + inodoPadreOrigen * sizeof(Inodo));
    file.read(reinterpret_cast<char*>(&padreOrigen), sizeof(Inodo));
    
    if (!sesionActual.esRoot && sesionActual.uid != padreOrigen.i_uid)
        throw std::runtime_error("MOVE: no tienes permiso de escritura en origen");
    
    // Validar destino
    std::string rutaPadreDest = obtenerRutaPadre(destino);
    std::string nombreDest = obtenerNombre(destino);
    int inodoPadreDest = buscarInodoPorPath(file, sb, rutaPadreDest);
    
    if (inodoPadreDest == -1)
        throw std::runtime_error("MOVE: carpeta destino no existe");
    
    Inodo padreDest{};
    file.seekg(sb.s_inode_start + inodoPadreDest * sizeof(Inodo));
    file.read(reinterpret_cast<char*>(&padreDest), sizeof(Inodo));
    
    if (!sesionActual.esRoot && sesionActual.uid != padreDest.i_uid)
        throw std::runtime_error("MOVE: no tienes permiso de escritura en destino");
    
    // Buscar inodo a mover
    auto [bloqueIdx, entradaIdx] = encontrarEntradaEnCarpeta(file, sb, padreOrigen, nombreOrigen);
    if (bloqueIdx == -1)
        throw std::runtime_error("MOVE: archivo no encontrado");
    
    int inodoAMover = -1;
    BloqueCarpeta bcOrigen{};
    file.seekg(sb.s_block_start + padreOrigen.i_block[bloqueIdx] * sizeof(BloqueCarpeta));
    file.read(reinterpret_cast<char*>(&bcOrigen), sizeof(BloqueCarpeta));
    inodoAMover = bcOrigen.b_content[entradaIdx].b_inodo;
    
    // Copiar entrada a destino
    bool agregado = false;
    for (int i = 0; i < 12; i++) {
        if (padreDest.i_block[i] == -1) continue;
        
        BloqueCarpeta bcDest{};
        file.seekg(sb.s_block_start + padreDest.i_block[i] * sizeof(BloqueCarpeta));
        file.read(reinterpret_cast<char*>(&bcDest), sizeof(BloqueCarpeta));
        
        for (int j = 0; j < 4; j++) {
            if (bcDest.b_content[j].b_inodo == -1) {
                bcDest.b_content[j].b_inodo = inodoAMover;
                std::strncpy(bcDest.b_content[j].b_name, nombreDest.c_str(), nombreDest.length());
                
                file.seekp(sb.s_block_start + padreDest.i_block[i] * sizeof(BloqueCarpeta));
                file.write(reinterpret_cast<char*>(&bcDest), sizeof(BloqueCarpeta));
                agregado = true;
                break;
            }
        }
        if (agregado) break;
    }
    
    if (!agregado)
        throw std::runtime_error("MOVE: no hay espacio en carpeta destino");
    
    // Eliminar entrada de origen
    bcOrigen.b_content[entradaIdx].b_inodo = -1;
    std::memset(bcOrigen.b_content[entradaIdx].b_name, 0, sizeof(bcOrigen.b_content[entradaIdx].b_name));
    
    file.seekp(sb.s_block_start + padreOrigen.i_block[bloqueIdx] * sizeof(BloqueCarpeta));
    file.write(reinterpret_cast<char*>(&bcOrigen), sizeof(BloqueCarpeta));
    
    // Actualizar SuperBloque
    file.seekp(part.part_start);
    file.write(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
    
    file.close();
    return "MOVE: '" + path + "' movido a '" + destino + "'";
}

// ============================================================
//  8. FIND - Buscar archivo o carpeta por patrón
// ============================================================
std::string Find(const std::string& path, const std::string& name) {
    if (!sesionActual.activa)
        throw std::runtime_error("FIND: no hay sesion activa");
    
    if (path.empty() || path[0] != '/')
        throw std::runtime_error("FIND: path invalido");
    
    if (name.empty())
        throw std::runtime_error("FIND: patrón de búsqueda invalido");
    
    ParticionMontada* pm = buscarMontada(sesionActual.idParticion);
    if (!pm) throw std::runtime_error("FIND: particion no encontrada");
    
    auto file = Utilities::OpenFile(pm->path);
    if (!file.is_open()) throw std::runtime_error("FIND: no se pudo abrir disco");
    
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
        throw std::runtime_error("FIND: particion no formateada");
    
    int startInodo = buscarInodoPorPath(file, sb, path);
    if (startInodo == -1)
        throw std::runtime_error("FIND: ruta no existe");
    
    // Función recursiva para buscar
    std::string resultado = "# " + path + "\n";
    std::vector<std::string> resultados;
    
    std::function<void(int, const std::string&)> buscarRecursivo = 
        [&](int inodoIdx, const std::string& rutaActual) {
        Inodo inodo{};
        file.seekg(sb.s_inode_start + inodoIdx * sizeof(Inodo));
        file.read(reinterpret_cast<char*>(&inodo), sizeof(Inodo));
        
        for (int i = 0; i < 12; i++) {
            if (inodo.i_block[i] == -1) continue;
            
            BloqueCarpeta bc{};
            file.seekg(sb.s_block_start + inodo.i_block[i] * sizeof(BloqueCarpeta));
            file.read(reinterpret_cast<char*>(&bc), sizeof(BloqueCarpeta));
            
            for (int j = 0; j < 4; j++) {
                if (bc.b_content[j].b_inodo == -1) continue;
                
                std::string nombreArchivo(bc.b_content[j].b_name);
                if (compararPatron(nombreArchivo, name)) {
                    std::string rutaCompleta = (rutaActual == "/") ? 
                        "/" + nombreArchivo : rutaActual + "/" + nombreArchivo;
                    resultados.push_back(rutaCompleta);
                }
            }
        }
    };
    
    buscarRecursivo(startInodo, path);
    
    for (const auto& r : resultados) {
        resultado += "# " + r + "\n";
    }
    
    file.close();
    return resultado.empty() ? "# Sin resultados" : resultado;
}

// ============================================================
//  9. CHOWN - Cambiar propietario
// ============================================================
std::string Chown(const std::string& path, const std::string& usuario, bool r) {
    if (!sesionActual.activa)
        throw std::runtime_error("CHOWN: no hay sesion activa");
    
    if (!sesionActual.esRoot)
        throw std::runtime_error("CHOWN: solo root puede cambiar propietarios");
    
    if (path.empty() || path[0] != '/')
        throw std::runtime_error("CHOWN: path invalido");
    
    if (usuario.empty())
        throw std::runtime_error("CHOWN: usuario invalido");
    
    ParticionMontada* pm = buscarMontada(sesionActual.idParticion);
    if (!pm) throw std::runtime_error("CHOWN: particion no encontrada");
    
    auto file = Utilities::OpenFile(pm->path);
    if (!file.is_open()) throw std::runtime_error("CHOWN: no se pudo abrir disco");
    
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
        throw std::runtime_error("CHOWN: particion no formateada");
    
    int inodoIdx = buscarInodoPorPath(file, sb, path);
    if (inodoIdx == -1)
        throw std::runtime_error("CHOWN: ruta no existe");
    
    Inodo inodo{};
    file.seekg(sb.s_inode_start + inodoIdx * sizeof(Inodo));
    file.read(reinterpret_cast<char*>(&inodo), sizeof(Inodo));
    
    // Cambiar UID (simplificado: convertir nombre a número)
    int newUid = std::stoi(usuario) > 0 ? std::stoi(usuario) : 1;
    inodo.i_uid = newUid;
    std::strcpy(inodo.i_mtime, getFechaActual().c_str());
    
    file.seekp(sb.s_inode_start + inodoIdx * sizeof(Inodo));
    file.write(reinterpret_cast<char*>(&inodo), sizeof(Inodo));
    
    file.close();
    std::string recursivo = r ? " (recursivamente)" : "";
    // Registrar operación en journaling
    FileSystem::RegistrarOperacion("chown", path, usuario);
    return "CHOWN: propietario cambiado a '" + usuario + "' para '" + path + "'" + recursivo;
}

// ============================================================
//  10. CHMOD - Cambiar permisos
// ============================================================
std::string Chmod(const std::string& path, const std::string& ugo, bool r) {
    if (!sesionActual.activa)
        throw std::runtime_error("CHMOD: no hay sesion activa");
    
    if (path.empty() || path[0] != '/')
        throw std::runtime_error("CHMOD: path invalido");
    
    if (ugo.empty() || ugo.length() != 3)
        throw std::runtime_error("CHMOD: permisos invalidos (formato: 3 dígitos UGO)");
    
    for (char c : ugo) {
        if (c < '0' || c > '7')
            throw std::runtime_error("CHMOD: cada dígito debe estar entre 0 y 7");
    }
    
    ParticionMontada* pm = buscarMontada(sesionActual.idParticion);
    if (!pm) throw std::runtime_error("CHMOD: particion no encontrada");
    
    auto file = Utilities::OpenFile(pm->path);
    if (!file.is_open()) throw std::runtime_error("CHMOD: no se pudo abrir disco");
    
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
        throw std::runtime_error("CHMOD: particion no formateada");
    
    int inodoIdx = buscarInodoPorPath(file, sb, path);
    if (inodoIdx == -1)
        throw std::runtime_error("CHMOD: ruta no existe");
    
    Inodo inodo{};
    file.seekg(sb.s_inode_start + inodoIdx * sizeof(Inodo));
    file.read(reinterpret_cast<char*>(&inodo), sizeof(Inodo));
    
    // Validar que el usuario actual sea propietario o root
    if (!sesionActual.esRoot && sesionActual.uid != inodo.i_uid)
        throw std::runtime_error("CHMOD: solo el propietario o root puede cambiar permisos");
    
    // Actualizar permisos
    inodo.i_perm[0] = ugo[0];  // Usuario
    inodo.i_perm[1] = ugo[1];  // Grupo
    inodo.i_perm[2] = ugo[2];  // Otros
    std::strcpy(inodo.i_mtime, getFechaActual().c_str());
    
    file.seekp(sb.s_inode_start + inodoIdx * sizeof(Inodo));
    file.write(reinterpret_cast<char*>(&inodo), sizeof(Inodo));
    
    file.close();
    std::string recursivo = r ? " (recursivamente)" : "";
    // Registrar operación en journaling
    FileSystem::RegistrarOperacion("chmod", path, ugo);
    return "CHMOD: permisos de '" + path + "' cambiados a " + ugo + recursivo;
}

// ============================================================
//  LS - Listar directorio para el Frontend en formato Pipe (|)
// ============================================================
std::string Ls(const std::string& path) {
    if (!sesionActual.activa) throw std::runtime_error("LS: no hay sesion activa");
    
    ParticionMontada* pm = buscarMontada(sesionActual.idParticion);
    if (!pm) throw std::runtime_error("LS: particion no encontrada");
    
    auto file = Utilities::OpenFile(pm->path);
    if (!file.is_open()) throw std::runtime_error("LS: no se pudo abrir disco");
    
    // Leer estructuras
    MBR mbr{};
    file.seekg(0); file.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    Partition part{};
    for (int i = 0; i < 4; i++) {
        if (mbr.mbr_partitions[i].part_start == pm->start) { part = mbr.mbr_partitions[i]; break; }
    }
    SuperBloque sb{};
    file.seekg(part.part_start); file.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
    
    // Buscar la carpeta que se quiere listar
    int inodoCarpetaIdx = buscarInodoPorPath(file, sb, path);
    if (inodoCarpetaIdx == -1) throw std::runtime_error("LS: la ruta no existe");
    
    Inodo carpeta{};
    file.seekg(sb.s_inode_start + inodoCarpetaIdx * sizeof(Inodo));
    file.read(reinterpret_cast<char*>(&carpeta), sizeof(Inodo));
    
    if (carpeta.i_type != INODO_CARPETA) throw std::runtime_error("LS: la ruta no es una carpeta");
    
    std::string salida = "";
    
    // Recorrer los 12 apuntadores directos de la carpeta
    for (int i = 0; i < 12; i++) {
        if (carpeta.i_block[i] == -1) continue;
        
        BloqueCarpeta bc{};
        file.seekg(sb.s_block_start + carpeta.i_block[i] * sizeof(BloqueCarpeta));
        file.read(reinterpret_cast<char*>(&bc), sizeof(BloqueCarpeta));
        
        // Revisar los 4 espacios (archivos/carpetas) dentro de este bloque
        for (int j = 0; j < 4; j++) {
            int targetInodoIdx = bc.b_content[j].b_inodo;
            if (targetInodoIdx != -1) {
                std::string nombre = bc.b_content[j].b_name;
                
                // Omitir "." y ".." para que no saturen la vista en el Frontend
                if (nombre == "." || nombre == "..") continue;
                
                // Leer el inodo del archivo hijo para saber su tamaño y tipo
                Inodo target{};
                file.seekg(sb.s_inode_start + targetInodoIdx * sizeof(Inodo));
                file.read(reinterpret_cast<char*>(&target), sizeof(Inodo));
                
                // Extraer variables para el frontend
                std::string tipo = (target.i_type == INODO_CARPETA) ? "d" : "f";
                int tamano = target.i_s;
                std::string permisos = std::string(1, target.i_perm[0]) + target.i_perm[1] + target.i_perm[2];
                
                // Unir todo en el formato "Nombre | Tipo | Tamaño | Permisos"
                salida += nombre + " | " + tipo + " | " + std::to_string(tamano) + " | " + permisos + "\n";
            }
        }
    }
    
    file.close();
    return salida; // Si está vacío, devolverá un string en blanco
}

} // namespace FileManagement