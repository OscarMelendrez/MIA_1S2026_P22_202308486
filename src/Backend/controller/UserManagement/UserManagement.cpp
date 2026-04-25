#include "UserManagement.h"
#include "../../Utilities/utilities.h"
#include "../../model/structures.h"

#include <cstring>
#include <ctime>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace Users {

struct RegistroUsuario {
    int         id;
    char        tipo;
    std::string grupo;
    std::string usuario;
    std::string pass;
};

static ParticionMontada* buscarMontada(const std::string& id) {
    for (auto& pm : particionesMontadas)
        if (pm.id == id) return &pm;
    return nullptr;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Leer users.txt desde EXT2
static std::string leerUsersTxt(std::fstream& file, const ParticionMontada& pm)
{
    SuperBloque sb;
    if (!Utilities::ReadObject(file, sb, pm.start))
        throw std::runtime_error("no se pudo leer SuperBloque");

    Inodo rootInodo;
    Utilities::ReadObject(file, rootInodo, sb.s_inode_start);

    int inodoUsersTxt = -1;
    for (int b = 0; b < 12 && inodoUsersTxt == -1; b++) {
        if (rootInodo.i_block[b] == -1) continue;
        BloqueCarpeta bloque;
        Utilities::ReadObject(file, bloque,
            sb.s_block_start + rootInodo.i_block[b] * BLOCK_SIZE);
        for (int e = 0; e < 4; e++) {
            if (bloque.b_content[e].b_inodo == -1) continue;
            if (std::string(bloque.b_content[e].b_name) == "users.txt") {
                inodoUsersTxt = bloque.b_content[e].b_inodo;
                break;
            }
        }
    }
    if (inodoUsersTxt == -1)
        throw std::runtime_error("users.txt no encontrado. Ejecuta MKFS primero");

    Inodo inodoU;
    Utilities::ReadObject(file, inodoU,
        sb.s_inode_start + inodoUsersTxt * static_cast<int>(sizeof(Inodo)));

    std::string contenido;
    for (int b = 0; b < 12; b++) {
        if (inodoU.i_block[b] == -1) break;
        BloqueArchivo bloque;
        Utilities::ReadObject(file, bloque,
            sb.s_block_start + inodoU.i_block[b] * BLOCK_SIZE);
        int leidos    = b * BLOCK_SIZE;
        int restantes = inodoU.i_s - leidos;
        if (restantes <= 0) break;
        int bytes = (restantes >= BLOCK_SIZE) ? BLOCK_SIZE : restantes;
        contenido.append(bloque.b_content, bytes);
    }
    return contenido;
}

// Escribir users.txt en EXT2
static void escribirUsersTxt(std::fstream& file, const ParticionMontada& pm,
                              const std::string& contenido)
{
    SuperBloque sb;
    Utilities::ReadObject(file, sb, pm.start);

    Inodo rootInodo;
    Utilities::ReadObject(file, rootInodo, sb.s_inode_start);

    int inodoUsersTxt = -1;
    int bloqueRaizIdx = -1;

    for (int b = 0; b < 12 && inodoUsersTxt == -1; b++) {
        if (rootInodo.i_block[b] == -1) continue;
        bloqueRaizIdx = rootInodo.i_block[b];
        BloqueCarpeta bloque;
        Utilities::ReadObject(file, bloque,
            sb.s_block_start + bloqueRaizIdx * BLOCK_SIZE);
        for (int e = 0; e < 4; e++) {
            if (bloque.b_content[e].b_inodo == -1) continue;
            if (std::string(bloque.b_content[e].b_name) == "users.txt") {
                inodoUsersTxt = bloque.b_content[e].b_inodo;
                break;
            }
        }
    }

    if (inodoUsersTxt == -1) {
        char estado; int nuevoInodo = -1, nuevoBloque = -1;
        for (int i = 1; i < sb.s_inodes_count; i++) {
            file.seekg(sb.s_bm_inode_start + i); file.read(&estado, 1);
            if (estado == '0') { nuevoInodo = i; break; }
        }
        for (int i = 1; i < sb.s_blocks_count; i++) {
            file.seekg(sb.s_bm_block_start + i); file.read(&estado, 1);
            if (estado == '0') { nuevoBloque = i; break; }
        }
        if (nuevoInodo == -1 || nuevoBloque == -1)
            throw std::runtime_error("sin espacio para users.txt");

        char uno = '1';
        file.seekp(sb.s_bm_inode_start + nuevoInodo);  file.write(&uno, 1);
        file.seekp(sb.s_bm_block_start + nuevoBloque); file.write(&uno, 1);

        Inodo inodoU;
        inodoU.i_uid      = 1; inodoU.i_gid = 1;
        inodoU.i_s        = static_cast<int>(contenido.size());
        inodoU.i_type     = INODO_ARCHIVO;
        inodoU.i_perm[0]  = '6'; inodoU.i_perm[1] = '6'; inodoU.i_perm[2] = '4';
        inodoU.i_block[0] = nuevoBloque;
        { std::string fa = getFechaActual();
          std::strncpy(inodoU.i_ctime, fa.c_str(), 18); inodoU.i_ctime[18] = '\0';
          std::strncpy(inodoU.i_mtime, fa.c_str(), 18); inodoU.i_mtime[18] = '\0';
          std::strncpy(inodoU.i_atime, fa.c_str(), 18); inodoU.i_atime[18] = '\0'; }
        Utilities::WriteObject(file, inodoU,
            sb.s_inode_start + nuevoInodo * static_cast<int>(sizeof(Inodo)));

        BloqueCarpeta bloqueRaiz;
        Utilities::ReadObject(file, bloqueRaiz,
            sb.s_block_start + bloqueRaizIdx * BLOCK_SIZE);
        for (int e = 0; e < 4; e++) {
            if (bloqueRaiz.b_content[e].b_inodo == -1) {
                std::strncpy(bloqueRaiz.b_content[e].b_name, "users.txt", 11);
                bloqueRaiz.b_content[e].b_inodo = nuevoInodo;
                break;
            }
        }
        Utilities::WriteObject(file, bloqueRaiz,
            sb.s_block_start + bloqueRaizIdx * BLOCK_SIZE);

        inodoUsersTxt = nuevoInodo;
        sb.s_free_inodes_count--;
        sb.s_free_blocks_count--;
        Utilities::WriteObject(file, sb, pm.start);
    }

    Inodo inodoU;
    Utilities::ReadObject(file, inodoU,
        sb.s_inode_start + inodoUsersTxt * static_cast<int>(sizeof(Inodo)));

    int totalBytes = static_cast<int>(contenido.size());
    int bloques    = (totalBytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
    if (bloques == 0) bloques = 1;

    for (int b = 0; b < bloques && b < 12; b++) {
        if (inodoU.i_block[b] == -1) {
            char estado; int nb = -1;
            for (int i = 1; i < sb.s_blocks_count; i++) {
                file.seekg(sb.s_bm_block_start + i); file.read(&estado, 1);
                if (estado == '0') { nb = i; break; }
            }
            if (nb == -1) throw std::runtime_error("sin bloques para users.txt");
            char uno = '1'; file.seekp(sb.s_bm_block_start + nb); file.write(&uno, 1);
            inodoU.i_block[b] = nb;
            sb.s_free_blocks_count--;
            Utilities::WriteObject(file, sb, pm.start);
        }
        BloqueArchivo bloque;
        std::memset(bloque.b_content, 0, BLOCK_SIZE);
        int offset = b * BLOCK_SIZE;
        int bytes  = std::min(BLOCK_SIZE, totalBytes - offset);
        std::memcpy(bloque.b_content, contenido.c_str() + offset, bytes);
        Utilities::WriteObject(file, bloque,
            sb.s_block_start + inodoU.i_block[b] * BLOCK_SIZE);
    }

    inodoU.i_s = totalBytes;
    { std::string fa = getFechaActual();
      std::strncpy(inodoU.i_mtime, fa.c_str(), 18); inodoU.i_mtime[18] = '\0'; }
    Utilities::WriteObject(file, inodoU,
        sb.s_inode_start + inodoUsersTxt * static_cast<int>(sizeof(Inodo)));
    file.flush();
}

static std::vector<RegistroUsuario> parsearUsers(const std::string& contenido) {
    std::vector<RegistroUsuario> registros;
    std::istringstream ss(contenido);
    std::string linea;
    while (std::getline(ss, linea)) {
        linea = trim(linea);
        if (linea.empty()) continue;
        std::istringstream ls(linea);
        std::string campo;
        std::vector<std::string> campos;
        while (std::getline(ls, campo, ',')) campos.push_back(trim(campo));
        if (campos.size() < 3) continue;
        RegistroUsuario r;
        r.id   = std::stoi(campos[0]);
        r.tipo = campos[1][0];
        r.grupo= campos[2];
        if (r.tipo == 'U' && campos.size() >= 5) {
            r.usuario = campos[3]; r.pass = campos[4];
        }
        registros.push_back(r);
    }
    return registros;
}

static std::string serializarUsers(const std::vector<RegistroUsuario>& registros) {
    std::ostringstream ss;
    for (const auto& r : registros) {
        if (r.tipo == 'G')
            ss << r.id << ", G, " << r.grupo << "\n";
        else
            ss << r.id << ", U, " << r.grupo << ", "
               << r.usuario << ", " << r.pass << "\n";
    }
    return ss.str();
}

static int maxId(const std::vector<RegistroUsuario>& registros, char tipo) {
    int max = 0;
    for (const auto& r : registros)
        if (r.tipo == tipo && r.id > max) max = r.id;
    return max;
}


// ============================================================
//  LOGIN
// ============================================================
std::string Login(const std::string& user, const std::string& pass,
                  const std::string& id)
{
    if (sesionActual.activa)
        throw std::runtime_error(
            "LOGIN: ya hay sesion activa de '" + sesionActual.usuario +
            "'. Ejecuta LOGOUT primero");

    ParticionMontada* pm = buscarMontada(id);
    if (!pm)
        throw std::runtime_error("LOGIN: particion '" + id + "' no encontrada");

    auto file = Utilities::OpenFile(pm->path);
    if (!file.is_open())
        throw std::runtime_error("LOGIN: no se pudo abrir: " + pm->path);

    std::string contenido = leerUsersTxt(file, *pm);
    file.close();
    auto registros = parsearUsers(contenido);

    for (const auto& r : registros) {
        if (r.tipo != 'U' || r.id == 0) continue;
        if (r.usuario != user) continue;
        if (r.pass != pass)
            throw std::runtime_error("LOGIN: contrasena incorrecta");

        int gid = 0;
        for (const auto& g : registros)
            if (g.tipo == 'G' && g.id != 0 && g.grupo == r.grupo)
                { gid = g.id; break; }

        sesionActual.activa      = true;
        sesionActual.usuario     = user;
        sesionActual.idParticion = id;
        sesionActual.uid         = r.id;
        sesionActual.gid         = gid;
        sesionActual.esRoot      = (user == "root");

        return "LOGIN: sesion iniciada como '" + user +
               "' en particion " + id +
               " (uid=" + std::to_string(r.id) +
               ", gid=" + std::to_string(gid) + ")" +
               (sesionActual.esRoot ? " [ROOT]" : "");
    }
    throw std::runtime_error("LOGIN: usuario '" + user + "' no encontrado");
}

std::string Logout() {
    if (!sesionActual.activa)
        throw std::runtime_error("LOGOUT: no hay sesion activa");
    std::string u = sesionActual.usuario;
    sesionActual.cerrar();
    return "LOGOUT: sesion de '" + u + "' cerrada";
}

std::string Mkgrp(const std::string& name) {
    if (!sesionActual.activa)  throw std::runtime_error("MKGRP: no hay sesion activa");
    if (!sesionActual.esRoot)  throw std::runtime_error("MKGRP: solo root");
    if (name.empty() || name.size() > 10)
        throw std::runtime_error("MKGRP: nombre invalido (1-10 chars)");

    ParticionMontada* pm = buscarMontada(sesionActual.idParticion);
    if (!pm) throw std::runtime_error("MKGRP: particion no encontrada");
    auto file = Utilities::OpenFile(pm->path);
    if (!file.is_open()) throw std::runtime_error("MKGRP: no se pudo abrir disco");

    auto registros = parsearUsers(leerUsersTxt(file, *pm));
    for (const auto& r : registros)
        if (r.tipo == 'G' && r.id != 0 && r.grupo == name)
            throw std::runtime_error("MKGRP: el grupo '" + name + "' ya existe");

    RegistroUsuario nuevo;
    nuevo.id = maxId(registros, 'G') + 1;
    nuevo.tipo = 'G'; nuevo.grupo = name;
    registros.push_back(nuevo);
    escribirUsersTxt(file, *pm, serializarUsers(registros));
    file.close();
    return "MKGRP: grupo '" + name + "' creado con GID=" + std::to_string(nuevo.id);
}

std::string Rmgrp(const std::string& name) {
    if (!sesionActual.activa)  throw std::runtime_error("RMGRP: no hay sesion activa");
    if (!sesionActual.esRoot)  throw std::runtime_error("RMGRP: solo root");

    ParticionMontada* pm = buscarMontada(sesionActual.idParticion);
    if (!pm) throw std::runtime_error("RMGRP: particion no encontrada");
    auto file = Utilities::OpenFile(pm->path);
    if (!file.is_open()) throw std::runtime_error("RMGRP: no se pudo abrir disco");

    auto registros = parsearUsers(leerUsersTxt(file, *pm));
    bool encontrado = false;
    for (auto& r : registros)
        if (r.tipo == 'G' && r.id != 0 && r.grupo == name)
            { r.id = 0; encontrado = true; break; }
    if (!encontrado)
        throw std::runtime_error("RMGRP: grupo '" + name + "' no encontrado");

    escribirUsersTxt(file, *pm, serializarUsers(registros));
    file.close();
    return "RMGRP: grupo '" + name + "' eliminado";
}

std::string Mkusr(const std::string& user, const std::string& pass,
                  const std::string& grp)
{
    if (!sesionActual.activa)  throw std::runtime_error("MKUSR: no hay sesion activa");
    if (!sesionActual.esRoot)  throw std::runtime_error("MKUSR: solo root");
    if (user.empty() || user.size() > 10)
        throw std::runtime_error("MKUSR: usuario invalido (1-10 chars)");
    if (pass.empty() || pass.size() > 10)
        throw std::runtime_error("MKUSR: contrasena invalida (1-10 chars)");

    ParticionMontada* pm = buscarMontada(sesionActual.idParticion);
    if (!pm) throw std::runtime_error("MKUSR: particion no encontrada");
    auto file = Utilities::OpenFile(pm->path);
    if (!file.is_open()) throw std::runtime_error("MKUSR: no se pudo abrir disco");

    auto registros = parsearUsers(leerUsersTxt(file, *pm));
    for (const auto& r : registros)
        if (r.tipo == 'U' && r.id != 0 && r.usuario == user)
            throw std::runtime_error("MKUSR: usuario '" + user + "' ya existe");

    bool grupoOk = false;
    for (const auto& r : registros)
        if (r.tipo == 'G' && r.id != 0 && r.grupo == grp) { grupoOk = true; break; }
    if (!grupoOk)
        throw std::runtime_error("MKUSR: grupo '" + grp + "' no existe");

    RegistroUsuario nuevo;
    nuevo.id = maxId(registros, 'U') + 1;
    nuevo.tipo = 'U'; nuevo.grupo = grp;
    nuevo.usuario = user; nuevo.pass = pass;
    registros.push_back(nuevo);
    escribirUsersTxt(file, *pm, serializarUsers(registros));
    file.close();
    return "MKUSR: usuario '" + user + "' creado UID=" +
           std::to_string(nuevo.id) + " grupo='" + grp + "'";
}

std::string Rmusr(const std::string& user) {
    if (!sesionActual.activa)  throw std::runtime_error("RMUSR: no hay sesion activa");
    if (!sesionActual.esRoot)  throw std::runtime_error("RMUSR: solo root");

    ParticionMontada* pm = buscarMontada(sesionActual.idParticion);
    if (!pm) throw std::runtime_error("RMUSR: particion no encontrada");
    auto file = Utilities::OpenFile(pm->path);
    if (!file.is_open()) throw std::runtime_error("RMUSR: no se pudo abrir disco");

    auto registros = parsearUsers(leerUsersTxt(file, *pm));
    bool encontrado = false;
    for (auto& r : registros)
        if (r.tipo == 'U' && r.id != 0 && r.usuario == user)
            { r.id = 0; encontrado = true; break; }
    if (!encontrado)
        throw std::runtime_error("RMUSR: usuario '" + user + "' no encontrado");

    escribirUsersTxt(file, *pm, serializarUsers(registros));
    file.close();
    return "RMUSR: usuario '" + user + "' eliminado";
}

std::string Chgrp(const std::string& user, const std::string& grp) {
    if (!sesionActual.activa)  throw std::runtime_error("CHGRP: no hay sesion activa");
    if (!sesionActual.esRoot)  throw std::runtime_error("CHGRP: solo root");

    ParticionMontada* pm = buscarMontada(sesionActual.idParticion);
    if (!pm) throw std::runtime_error("CHGRP: particion no encontrada");
    auto file = Utilities::OpenFile(pm->path);
    if (!file.is_open()) throw std::runtime_error("CHGRP: no se pudo abrir disco");

    auto registros = parsearUsers(leerUsersTxt(file, *pm));
    bool grupoOk = false;
    for (const auto& r : registros)
        if (r.tipo == 'G' && r.id != 0 && r.grupo == grp) { grupoOk = true; break; }
    if (!grupoOk)
        throw std::runtime_error("CHGRP: grupo '" + grp + "' no existe");

    bool encontrado = false;
    for (auto& r : registros)
        if (r.tipo == 'U' && r.id != 0 && r.usuario == user)
            { r.grupo = grp; encontrado = true; break; }
    if (!encontrado)
        throw std::runtime_error("CHGRP: usuario '" + user + "' no encontrado");

    escribirUsersTxt(file, *pm, serializarUsers(registros));
    file.close();
    return "CHGRP: usuario '" + user + "' movido al grupo '" + grp + "'";
}

} // namespace Users