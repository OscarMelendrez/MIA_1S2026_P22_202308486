#pragma once
// ============================================================
//  structures.h  -  ExtreamFS  |  MIA Proyecto 1  1S-2026
//  Fechas como char[19] "YYYY-MM-DD HH:MM:SS" — visibles en hex
// ============================================================

#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <cmath>
#include <map>

// ============================================================
//  HELPER: obtener fecha actual como string legible
// ============================================================
inline std::string getFechaActual() {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char buf[20];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
    return std::string(buf);
}

#pragma pack(1)

// ============================================================
// 1. PARTITION
// ============================================================
struct Partition {
    char    part_status;
    char    part_type;
    char    part_fit;
    int32_t part_start;
    int32_t part_s;
    char    part_name[16];
    int32_t part_correlative;
    char    part_id[4];

    Partition() {
        part_status      = '0';
        part_type        = 'P';
        part_fit         = 'W';
        part_start       = -1;
        part_s           = 0;
        part_correlative = -1;
        std::memset(part_name, 0, sizeof(part_name));
        std::memset(part_id,   0, sizeof(part_id));
    }
};

// ============================================================
// 2. MBR
//    mbr_fecha_creacion como char[19] visible en hex editor
// ============================================================
struct MBR {
    int32_t   mbr_tamano;
    char      mbr_fecha_creacion[19]; // "YYYY-MM-DD HH:MM:SS"
    int32_t   mbr_dsk_signature;
    char      dsk_fit;
    Partition mbr_partitions[4];

    MBR() {
        mbr_tamano        = 0;
        mbr_dsk_signature = 0;
        dsk_fit           = 'F';
        std::memset(mbr_fecha_creacion, 0, sizeof(mbr_fecha_creacion));
    }
};

// ============================================================
// 3. EBR
// ============================================================
struct EBR {
    char    part_mount;
    char    part_fit;
    int32_t part_start;
    int32_t part_s;
    int32_t part_next;
    char    part_name[16];

    EBR() {
        part_mount = '0';
        part_fit   = 'W';
        part_start = -1;
        part_s     = 0;
        part_next  = -1;
        std::memset(part_name, 0, sizeof(part_name));
    }
};

// ============================================================
// 4. SUPERBLOQUE
//    s_mtime y s_umtime como char[19] visible en hex editor
// ============================================================
struct SuperBloque {
    int32_t s_filesystem_type;
    int32_t s_inodes_count;
    int32_t s_blocks_count;
    int32_t s_free_blocks_count;
    int32_t s_free_inodes_count;
    char    s_mtime[19];   // "YYYY-MM-DD HH:MM:SS"
    char    s_umtime[19];  // "YYYY-MM-DD HH:MM:SS"
    int32_t s_mnt_count;
    int32_t s_magic;
    int32_t s_inode_s;
    int32_t s_block_s;
    int32_t s_firts_ino;
    int32_t s_first_blo;
    int32_t s_bm_inode_start;
    int32_t s_bm_block_start;
    int32_t s_inode_start;
    int32_t s_block_start;

    SuperBloque() {
        s_filesystem_type   = 2;
        s_inodes_count      = 0;
        s_blocks_count      = 0;
        s_free_blocks_count = 0;
        s_free_inodes_count = 0;
        s_mnt_count         = 0;
        s_magic             = 0xEF53;
        s_inode_s           = 0;
        s_block_s           = 0;
        s_firts_ino         = -1;
        s_first_blo         = -1;
        s_bm_inode_start    = -1;
        s_bm_block_start    = -1;
        s_inode_start       = -1;
        s_block_start       = -1;
        std::memset(s_mtime,  0, sizeof(s_mtime));
        std::memset(s_umtime, 0, sizeof(s_umtime));
    }
};

// ============================================================
// 5. INODO
//    i_atime, i_ctime, i_mtime como char[19] visible en hex
// ============================================================
struct Inodo {
    int32_t i_uid;
    int32_t i_gid;
    int32_t i_s;
    char    i_atime[19];  // "YYYY-MM-DD HH:MM:SS"
    char    i_ctime[19];  // "YYYY-MM-DD HH:MM:SS"
    char    i_mtime[19];  // "YYYY-MM-DD HH:MM:SS"
    int32_t i_block[15];
    char    i_type;
    char    i_perm[3];

    Inodo() {
        i_uid  = 0;
        i_gid  = 0;
        i_s    = 0;
        i_type = '0';
        for (int i = 0; i < 15; i++) i_block[i] = -1;
        std::memset(i_atime, 0, sizeof(i_atime));
        std::memset(i_ctime, 0, sizeof(i_ctime));
        std::memset(i_mtime, 0, sizeof(i_mtime));
        std::memset(i_perm,  0, sizeof(i_perm));
    }
};

// ============================================================
// 6. CONTENIDO + BLOQUE CARPETA  (64 bytes)
// ============================================================
struct Contenido {
    char    b_name[256];
    int32_t b_inodo;

    Contenido() {
        std::memset(b_name, 0, sizeof(b_name));
        b_inodo = -1;
    }
};

struct BloqueCarpeta {
    Contenido b_content[4];
};

// ============================================================
// 7. BLOQUE DE ARCHIVO  (64 bytes)
// ============================================================
struct BloqueArchivo {
    char b_content[64];

    BloqueArchivo() {
        std::memset(b_content, 0, sizeof(b_content));
    }
};

// ============================================================
// 8. BLOQUE DE APUNTADORES  (64 bytes)
// ============================================================
struct BloqueApuntadores {
    int32_t b_pointers[16];

    BloqueApuntadores() {
        for (int i = 0; i < 16; i++) b_pointers[i] = -1;
    }
};

// ============================================================
// 9. INFORMATION
// ============================================================
struct Information {
    char i_operation[10];  // Contiene la operación que se realizó (ej. "mkdir", "mkfile")[cite: 255].
    char i_path[32];       // Contiene el path exacto donde se realizó la operación[cite: 255].
    char i_content[64];    // Contiene todo el contenido, aplicable si la operación es sobre un archivo[cite: 255].
    float i_date;          // Contiene la fecha en la que se hizo la operación[cite: 255].
};

// ============================================================
// 10. JOURNAL
// ============================================================
struct Journal {
    int j_count;             // Lleva el conteo secuencial del journal actual[cite: 250].
    Information j_content;   // Contiene toda la información detallada de la acción que se hizo[cite: 250].
};

#pragma pack()

// ============================================================
//  CONSTANTES
// ============================================================
constexpr int  EXT2_MAGIC          = 0xEF53;
constexpr int  BLOCK_SIZE          = 64;
constexpr char INODO_CARPETA       = '0';
constexpr char INODO_ARCHIVO       = '1';
constexpr char PARTITION_MOUNTED   = '1';
constexpr char PARTITION_UNMOUNTED = '0';
constexpr char PART_PRIMARIA       = 'P';
constexpr char PART_EXTENDIDA      = 'E';
constexpr char PART_LOGICA         = 'L';
constexpr char FIT_BEST            = 'B';
constexpr char FIT_FIRST           = 'F';
constexpr char FIT_WORST           = 'W';

// ============================================================
//  FORMULA MKFS
// ============================================================
inline int calcularNumEstructuras(int tamanoParticion) {
    double divisor = 1.0
                   + 3.0
                   + static_cast<double>(sizeof(Inodo))
                   + 3.0 * static_cast<double>(BLOCK_SIZE);

    double n = (static_cast<double>(tamanoParticion)
                - static_cast<double>(sizeof(SuperBloque)))
               / divisor;

    return static_cast<int>(std::floor(n));
}

// ============================================================
//  ESTRUCTURAS EN MEMORIA RAM
// ============================================================
struct ParticionMontada {
    std::string id;
    std::string path;
    std::string nombre;
    int         start;
    int         size;
    char        type;
    int         correlativo;

    ParticionMontada()
        : start(0), size(0), type(PART_PRIMARIA), correlativo(0) {}
};

extern std::vector<ParticionMontada> particionesMontadas;
extern std::string DISCOS_DIR;

// Mapa: nombre_archivo -> ruta_completa
// Se llena en Reports.cpp cada vez que se genera un reporte
extern std::map<std::string, std::string> reporteRutas;

struct SesionActiva {
    bool        activa      = false;
    std::string usuario;
    std::string idParticion;
    int         uid         = 0;
    int         gid         = 0;
    bool        esRoot      = false;

    void cerrar() {
        activa      = false;
        usuario     = "";
        idParticion = "";
        uid         = 0;
        gid         = 0;
        esRoot      = false;
    }
};

extern SesionActiva sesionActual;