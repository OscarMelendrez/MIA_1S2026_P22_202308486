#include "Reports.h"
#include "../../Utilities/utilities.h"
#include "../../model/structures.h"

#include <sstream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <map>

namespace Reports {

// ── Utilidades ───────────────────────────────────────────────

static ParticionMontada* buscarMontada(const std::string& id) {
    for (auto& pm : particionesMontadas)
        if (pm.id == id) return &pm;
    return nullptr;
}

static std::string dotAJpg(const std::string& dotContent,
                             const std::string& outPath)
{
    std::filesystem::path p(outPath);
    if (p.has_parent_path())
        std::filesystem::create_directories(p.parent_path());

    std::string dotFile = outPath + ".dot";
    std::ofstream f(dotFile);
    if (!f.is_open())
        throw std::runtime_error("No se pudo crear .dot: " + dotFile);
    f << dotContent;
    f.close();

    std::string cmd = "dot -Tjpg \"" + dotFile + "\" -o \"" + outPath + "\" 2>/dev/null";
    if (std::system(cmd.c_str()) != 0)
        throw std::runtime_error("Graphviz fallo generando: " + outPath);

    reporteRutas[p.filename().string()] = outPath;
    return "REP: reporte generado en " + outPath;
}

static std::string esc(const std::string& s) {
    std::string r;
    for (char c : s) {
        if      (c == '&') r += "&amp;";
        else if (c == '<') r += "&lt;";
        else if (c == '>') r += "&gt;";
        else if (c == '"') r += "&quot;";
        else               r += c;
    }
    return r;
}

static std::string permStr(const char p[3]) {
    std::string r = "-";
    for (int i = 0; i < 3; i++) {
        int v = p[i] - '0';
        r += (v & 4) ? 'r' : '-';
        r += (v & 2) ? 'w' : '-';
        r += (v & 1) ? 'x' : '-';
    }
    return r;
}

static SuperBloque leerSB(std::fstream& f, int start) {
    SuperBloque sb{};
    f.seekg(start);
    f.read(reinterpret_cast<char*>(&sb), sizeof(SuperBloque));
    return sb;
}

static int resolverRuta(std::fstream& f, const SuperBloque& sb,
                         const std::string& ruta)
{
    if (ruta.empty() || ruta[0] != '/') return -1;
    std::vector<std::string> partes;
    std::istringstream ss(ruta);
    std::string p;
    while (std::getline(ss, p, '/'))
        if (!p.empty()) partes.push_back(p);

    int idx = 0;
    for (const auto& nombre : partes) {
        Inodo inodo{};
        f.seekg(sb.s_inode_start + idx * sizeof(Inodo));
        f.read(reinterpret_cast<char*>(&inodo), sizeof(Inodo));
        if (inodo.i_type != INODO_CARPETA) return -1;
        bool found = false;
        for (int b = 0; b < 12 && !found; b++) {
            if (inodo.i_block[b] == -1) continue;
            BloqueCarpeta bc{};
            f.seekg(sb.s_block_start + inodo.i_block[b] * sizeof(BloqueCarpeta));
            f.read(reinterpret_cast<char*>(&bc), sizeof(BloqueCarpeta));
            for (int e = 0; e < 4 && !found; e++) {
                if (bc.b_content[e].b_inodo != -1 &&
                    std::string(bc.b_content[e].b_name,
                        strnlen(bc.b_content[e].b_name,12)) == nombre) {
                    idx = bc.b_content[e].b_inodo;
                    found = true;
                }
            }
        }
        if (!found) return -1;
    }
    return idx;
}

static std::string leerArchivoEXT2(std::fstream& f, const SuperBloque& sb,
                                     const Inodo& inodo)
{
    std::string contenido;
    for (int b = 0; b < 12; b++) {
        if (inodo.i_block[b] == -1) break;
        BloqueArchivo ba{};
        f.seekg(sb.s_block_start + inodo.i_block[b] * sizeof(BloqueArchivo));
        f.read(reinterpret_cast<char*>(&ba), sizeof(BloqueArchivo));
        int leidos = b * 64;
        int bytes  = std::min(64, inodo.i_s - leidos);
        if (bytes <= 0) break;
        contenido.append(ba.b_content, bytes);
    }
    return contenido;
}

static std::string getUser(std::fstream& f, const SuperBloque& sb, int uid) {
    int idx = resolverRuta(f, sb, "/users.txt");
    if (idx == -1) return std::to_string(uid);
    Inodo in{};
    f.seekg(sb.s_inode_start + idx * sizeof(Inodo));
    f.read(reinterpret_cast<char*>(&in), sizeof(Inodo));
    std::string txt = leerArchivoEXT2(f, sb, in);
    auto trim = [](std::string s) {
        while (!s.empty() && (s[0]==' '||s[0]=='\r'||s[0]=='\n')) s=s.substr(1);
        while (!s.empty() && (s.back()==' '||s.back()=='\r'||s.back()=='\n')) s.pop_back();
        return s;
    };
    std::istringstream ss(txt);
    std::string linea;
    while (std::getline(ss, linea)) {
        std::istringstream ls(linea);
        std::string sid, tipo, grp, usr;
        std::getline(ls, sid, ','); std::getline(ls, tipo, ',');
        std::getline(ls, grp, ','); std::getline(ls, usr, ',');
        tipo = trim(tipo); usr = trim(usr); sid = trim(sid);
        if (tipo == "U") {
            try { if (std::stoi(sid) == uid) return usr; } catch(...) {}
        }
    }
    return std::to_string(uid);
}

static std::string getGroup(std::fstream& f, const SuperBloque& sb, int gid) {
    int idx = resolverRuta(f, sb, "/users.txt");
    if (idx == -1) return std::to_string(gid);
    Inodo in{};
    f.seekg(sb.s_inode_start + idx * sizeof(Inodo));
    f.read(reinterpret_cast<char*>(&in), sizeof(Inodo));
    std::string txt = leerArchivoEXT2(f, sb, in);
    auto trim = [](std::string s) {
        while (!s.empty() && (s[0]==' '||s[0]=='\r'||s[0]=='\n')) s=s.substr(1);
        while (!s.empty() && (s.back()==' '||s.back()=='\r'||s.back()=='\n')) s.pop_back();
        return s;
    };
    std::istringstream ss(txt);
    std::string linea;
    while (std::getline(ss, linea)) {
        std::istringstream ls(linea);
        std::string sid, tipo, grp;
        std::getline(ls, sid, ','); std::getline(ls, tipo, ',');
        std::getline(ls, grp, ',');
        tipo = trim(tipo); grp = trim(grp); sid = trim(sid);
        if (tipo == "G") {
            try { if (std::stoi(sid) == gid) return grp; } catch(...) {}
        }
    }
    return std::to_string(gid);
}

// ============================================================
//  REP MBR
// ============================================================
static std::string repMBR(const std::string& diskPath,
                            const std::string& outPath)
{
    std::ifstream disco(diskPath, std::ios::binary);
    if (!disco.is_open())
        throw std::runtime_error("REP MBR: no se pudo abrir: " + diskPath);
    MBR mbr{};
    disco.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    disco.close();

    std::string fecha(mbr.mbr_fecha_creacion,
                      strnlen(mbr.mbr_fecha_creacion, 19));
    std::ostringstream dot;
    dot << "digraph G {\n  node [shape=none margin=0 fontname=\"Arial\"]\n"
        << "  main [label=<\n"
        << "  <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"6\" BGCOLOR=\"white\">\n"
        << "  <TR><TD COLSPAN=\"2\" BGCOLOR=\"#4B0082\"><FONT COLOR=\"white\"><B>REPORTE DE MBR</B></FONT></TD></TR>\n"
        << "  <TR><TD BGCOLOR=\"#D8BFD8\">mbr_tamano</TD><TD>" << mbr.mbr_tamano << "</TD></TR>\n"
        << "  <TR><TD BGCOLOR=\"#E6D0E6\">mbr_fecha_creacion</TD><TD>" << esc(fecha) << "</TD></TR>\n"
        << "  <TR><TD BGCOLOR=\"#D8BFD8\">mbr_disk_signature</TD><TD>" << mbr.mbr_dsk_signature << "</TD></TR>\n";

    for (int i = 0; i < 4; i++) {
        Partition& p = mbr.mbr_partitions[i];
        if (p.part_start == -1) continue;
        std::string nombre(p.part_name, strnlen(p.part_name, 16));
        bool esExt = (p.part_type == 'E');

        dot << "  <TR><TD COLSPAN=\"2\" BGCOLOR=\"#4B0082\"><FONT COLOR=\"white\"><B>Particion</B></FONT></TD></TR>\n"
            << "  <TR><TD BGCOLOR=\"#D8BFD8\">part_status</TD><TD>" << p.part_status << "</TD></TR>\n"
            << "  <TR><TD BGCOLOR=\"#E6D0E6\">part_type</TD><TD>" << (char)tolower(p.part_type) << "</TD></TR>\n"
            << "  <TR><TD BGCOLOR=\"#D8BFD8\">part_fit</TD><TD>" << (char)tolower(p.part_fit) << "</TD></TR>\n"
            << "  <TR><TD BGCOLOR=\"#E6D0E6\">part_start</TD><TD>" << p.part_start << "</TD></TR>\n"
            << "  <TR><TD BGCOLOR=\"#D8BFD8\">part_size</TD><TD>" << p.part_s << "</TD></TR>\n"
            << "  <TR><TD BGCOLOR=\"#E6D0E6\">part_name</TD><TD>" << esc(nombre) << "</TD></TR>\n";

        if (esExt) {
            std::ifstream d2(diskPath, std::ios::binary);
            int off = p.part_start;
            while (off != -1) {
                EBR ebr{};
                d2.seekg(off);
                d2.read(reinterpret_cast<char*>(&ebr), sizeof(EBR));
                if (ebr.part_s == 0) break;
                std::string en(ebr.part_name, strnlen(ebr.part_name, 16));
                dot << "  <TR><TD COLSPAN=\"2\" BGCOLOR=\"#C05050\"><FONT COLOR=\"white\"><B>Particion Logica</B></FONT></TD></TR>\n"
                    << "  <TR><TD BGCOLOR=\"#F4CCCC\">part_status</TD><TD>" << ebr.part_mount << "</TD></TR>\n"
                    << "  <TR><TD BGCOLOR=\"#FADADD\">part_next</TD><TD>" << ebr.part_next << "</TD></TR>\n"
                    << "  <TR><TD BGCOLOR=\"#F4CCCC\">part_fit</TD><TD>" << (char)tolower(ebr.part_fit) << "</TD></TR>\n"
                    << "  <TR><TD BGCOLOR=\"#FADADD\">part_start</TD><TD>" << ebr.part_start << "</TD></TR>\n"
                    << "  <TR><TD BGCOLOR=\"#F4CCCC\">part_size</TD><TD>" << ebr.part_s << "</TD></TR>\n"
                    << "  <TR><TD BGCOLOR=\"#FADADD\">part_name</TD><TD>" << esc(en) << "</TD></TR>\n";
                if (ebr.part_next == -1) break;
                off = ebr.part_next;
            }
            d2.close();
        }
    }
    dot << "  </TABLE>>]\n}\n";
    return dotAJpg(dot.str(), outPath);
}

// ============================================================
//  REP EBR
// ============================================================
static std::string repEBR(const std::string& diskPath,
                            const std::string& outPath)
{
    std::ifstream disco(diskPath, std::ios::binary);
    if (!disco.is_open())
        throw std::runtime_error("REP EBR: no se pudo abrir: " + diskPath);
    MBR mbr{};
    disco.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    disco.close();

    int extStart = -1;
    for (int i = 0; i < 4; i++)
        if (mbr.mbr_partitions[i].part_type == 'E' &&
            mbr.mbr_partitions[i].part_start != -1)
            { extStart = mbr.mbr_partitions[i].part_start; break; }

    if (extStart == -1)
        throw std::runtime_error("REP EBR: no existe particion extendida en este disco");

    std::ostringstream dot;
    dot << "digraph G {\n  node [shape=none margin=0 fontname=\"Arial\"]\n"
        << "  main [label=<\n"
        << "  <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"6\" BGCOLOR=\"white\">\n"
        << "  <TR><TD COLSPAN=\"2\" BGCOLOR=\"#4B0082\"><FONT COLOR=\"white\"><B>REPORTE DE EBR</B></FONT></TD></TR>\n";

    std::ifstream d2(diskPath, std::ios::binary);
    int off = extStart; int cnt = 0;
    while (off != -1) {
        EBR ebr{};
        d2.seekg(off);
        d2.read(reinterpret_cast<char*>(&ebr), sizeof(EBR));
        if (ebr.part_s == 0 && cnt > 0) break;
        std::string en(ebr.part_name, strnlen(ebr.part_name, 16));
        dot << "  <TR><TD COLSPAN=\"2\" BGCOLOR=\"#4B0082\"><FONT COLOR=\"white\"><B>Particion</B></FONT></TD></TR>\n"
            << "  <TR><TD BGCOLOR=\"#D8BFD8\">part_status</TD><TD>" << ebr.part_mount << "</TD></TR>\n"
            << "  <TR><TD BGCOLOR=\"#E6D0E6\">part_type</TD><TD>l</TD></TR>\n"
            << "  <TR><TD BGCOLOR=\"#D8BFD8\">part_fit</TD><TD>" << (char)tolower(ebr.part_fit) << "</TD></TR>\n"
            << "  <TR><TD BGCOLOR=\"#E6D0E6\">part_start</TD><TD>" << ebr.part_start << "</TD></TR>\n"
            << "  <TR><TD BGCOLOR=\"#D8BFD8\">part_size</TD><TD>" << ebr.part_s << "</TD></TR>\n"
            << "  <TR><TD BGCOLOR=\"#E6D0E6\">part_next</TD><TD>" << ebr.part_next << "</TD></TR>\n"
            << "  <TR><TD BGCOLOR=\"#D8BFD8\">part_name</TD><TD>" << esc(en) << "</TD></TR>\n";
        cnt++;
        if (ebr.part_next == -1) break;
        off = ebr.part_next;
    }
    d2.close();
    if (cnt == 0)
        dot << "  <TR><TD COLSPAN=\"2\">Sin particiones logicas</TD></TR>\n";
    dot << "  </TABLE>>]\n}\n";
    return dotAJpg(dot.str(), outPath);
}

// ============================================================
//  REP DISK
// ============================================================
static std::string repDisk(const std::string& diskPath,
                             const std::string& outPath)
{
    std::ifstream disco(diskPath, std::ios::binary);
    if (!disco.is_open())
        throw std::runtime_error("REP DISK: no se pudo abrir: " + diskPath);
    MBR mbr{};
    disco.read(reinterpret_cast<char*>(&mbr), sizeof(MBR));
    disco.close();

    std::string discNombre = std::filesystem::path(diskPath).filename().string();
    int total = mbr.mbr_tamano;

    struct Seg { int ini, fin; std::string tipo, nombre; };
    std::vector<Seg> flat;
    flat.push_back({0, (int)sizeof(MBR), "MBR", "MBR"});

    for (int i = 0; i < 4; i++) {
        Partition& p = mbr.mbr_partitions[i];
        if (p.part_start == -1) continue;
        std::string n(p.part_name, strnlen(p.part_name, 16));
        if (p.part_type == 'E') {
            std::ifstream d2(diskPath, std::ios::binary);
            int off = p.part_start;
            while (off != -1) {
                EBR ebr{};
                d2.seekg(off);
                d2.read(reinterpret_cast<char*>(&ebr), sizeof(EBR));
                if (ebr.part_s == 0) break;
                std::string en(ebr.part_name, strnlen(ebr.part_name, 16));
                flat.push_back({off, off + (int)sizeof(EBR), "EBR", "EBR"});
                if (ebr.part_start != off)
                    flat.push_back({ebr.part_start, ebr.part_start + ebr.part_s, "Logica", en});
                if (ebr.part_next == -1) break;
                off = ebr.part_next;
            }
            d2.close();
        } else {
            flat.push_back({p.part_start, p.part_start + p.part_s, "Primaria", n});
        }
    }

    std::sort(flat.begin(), flat.end(), [](const Seg& a, const Seg& b){ return a.ini < b.ini; });

    std::vector<Seg> segs;
    int cursor = 0;
    for (auto& s : flat) {
        if (s.ini > cursor)
            segs.push_back({cursor, s.ini, "Libre",
                std::to_string((s.ini-cursor)*100/total) + "% del disco"});
        segs.push_back(s);
        cursor = s.fin;
    }
    if (cursor < total)
        segs.push_back({cursor, total, "Libre",
            std::to_string((total-cursor)*100/total) + "% del disco"});

    std::ostringstream dot;
    dot << "digraph G {\n  node [shape=none margin=0 fontname=\"Arial\"]\n"
        << "  label=\"" << esc(discNombre) << "\" labelloc=t fontname=\"Arial\" fontsize=14\n"
        << "  main [label=<\n"
        << "  <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\">\n  <TR>\n";

    for (auto& s : segs) {
        int pct = std::max(1,(s.fin-s.ini)*100/total);
        int w   = std::max(60, pct*9);
        std::string color = "#F2F3F4";
        if      (s.tipo=="MBR")      color="#AED6F1";
        else if (s.tipo=="Primaria") color="#A9DFBF";
        else if (s.tipo=="EBR")      color="#F9E79F";
        else if (s.tipo=="Logica")   color="#FAD7A0";
        dot << "    <TD WIDTH=\"" << w << "\" BGCOLOR=\"" << color << "\">"
            << "<FONT POINT-SIZE=\"10\"><B>" << s.tipo << "</B><BR/>"
            << esc(s.nombre) << "</FONT></TD>\n";
    }
    dot << "  </TR>\n  </TABLE>>]\n}\n";
    return dotAJpg(dot.str(), outPath);
}

// ============================================================
//  REP SB
// ============================================================
static std::string repSB(const std::string& diskPath,
                           const std::string& id,
                           const std::string& outPath)
{
    ParticionMontada* pm = buscarMontada(id);
    if (!pm) throw std::runtime_error("REP SB: id no encontrado: " + id);
    auto file = Utilities::OpenFile(diskPath);
    if (!file.is_open()) throw std::runtime_error("REP SB: no se pudo abrir disco");
    SuperBloque sb = leerSB(file, pm->start);
    file.close();

    std::string discNombre = std::filesystem::path(diskPath).filename().string();
    std::string smtime(sb.s_mtime, strnlen(sb.s_mtime, 19));
    std::string sumtime(sb.s_umtime, strnlen(sb.s_umtime, 19));

    std::ostringstream dot;
    dot << "digraph G {\n  node [shape=none margin=0 fontname=\"Arial\"]\n"
        << "  main [label=<\n"
        << "  <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"6\" BGCOLOR=\"white\">\n"
        << "  <TR><TD COLSPAN=\"2\" BGCOLOR=\"#27AE60\"><FONT COLOR=\"white\"><B>Reporte de SUPERBLOQUE</B></FONT></TD></TR>\n";

    auto row = [&](const std::string& c, const std::string& v, bool alt) {
        std::string bg = alt ? "#A9DFBF" : "#FDFEFE";
        dot << "  <TR><TD BGCOLOR=\"" << bg << "\">" << esc(c) << "</TD><TD>" << esc(v) << "</TD></TR>\n";
    };
    bool a=false;
    row("sb_nombre_hd",          discNombre,                        a=!a);
    row("sb_inodos_count",       std::to_string(sb.s_inodes_count), a=!a);
    row("sb_bloques_count",      std::to_string(sb.s_blocks_count), a=!a);
    row("sb_free_inodos_count",  std::to_string(sb.s_free_inodes_count), a=!a);
    row("sb_free_bloques_count", std::to_string(sb.s_free_blocks_count), a=!a);
    row("sb_date_creacion",      smtime,  a=!a);
    row("sb_date_ultimo_montaje",sumtime, a=!a);
    row("sb_montajes_count",     std::to_string(sb.s_mnt_count),   a=!a);
    std::ostringstream hex; hex << std::hex << sb.s_magic;
    row("sb_magic",              "0x" + hex.str(),                  a=!a);
    row("sb_inode_size",         std::to_string(sb.s_inode_s),      a=!a);
    row("sb_block_size",         std::to_string(sb.s_block_s),      a=!a);
    row("sb_firts_ino",          std::to_string(sb.s_firts_ino),    a=!a);
    row("sb_first_blo",          std::to_string(sb.s_first_blo),    a=!a);
    row("sb_ap_bitmap_inodos",   std::to_string(sb.s_bm_inode_start), a=!a);
    row("sb_ap_bitmap_bloques",  std::to_string(sb.s_bm_block_start), a=!a);
    row("sb_ap_inodos",          std::to_string(sb.s_inode_start),  a=!a);
    row("sb_ap_bloques",         std::to_string(sb.s_block_start),  a=!a);
    dot << "  </TABLE>>]\n}\n";
    return dotAJpg(dot.str(), outPath);
}

// ============================================================
//  REP BM_INODE / REP BM_BLOCK  → archivo .txt
// ============================================================
static std::string repBitmap(const std::string& diskPath,
                               const std::string& id,
                               const std::string& outPath,
                               bool esInode)
{
    ParticionMontada* pm = buscarMontada(id);
    if (!pm) throw std::runtime_error("REP BITMAP: id no encontrado: " + id);
    auto file = Utilities::OpenFile(diskPath);
    if (!file.is_open()) throw std::runtime_error("REP BITMAP: no se pudo abrir disco");
    SuperBloque sb = leerSB(file, pm->start);
    int count  = esInode ? sb.s_inodes_count  : sb.s_blocks_count;
    int bstart = esInode ? sb.s_bm_inode_start : sb.s_bm_block_start;
    std::vector<char> bm(count);
    file.seekg(bstart);
    file.read(bm.data(), count);
    file.close();

    std::filesystem::create_directories(std::filesystem::path(outPath).parent_path());
    std::ofstream fout(outPath);
    if (!fout.is_open()) throw std::runtime_error("REP BITMAP: no se pudo crear: " + outPath);
    const int COLS = 20;
    for (int i = 0; i < count; i++) {
        if (i % COLS == 0) { if (i>0) fout << "\n"; fout << (i/COLS+1) << "\t"; }
        fout << bm[i] << " ";
    }
    fout << "\n";
    fout.close();
    reporteRutas[std::filesystem::path(outPath).filename().string()] = outPath;
    return "REP: reporte generado en " + outPath;
}

// ============================================================
//  REP TREE
// ============================================================
static void generarTree(std::fstream& f, const SuperBloque& sb,
                         int inodoIdx, const std::string& nombreRuta,
                         std::ostringstream& dot, std::vector<int>& visitados)
{
    if (std::find(visitados.begin(),visitados.end(),inodoIdx)!=visitados.end()) return;
    visitados.push_back(inodoIdx);

    Inodo inodo{};
    f.seekg(sb.s_inode_start + inodoIdx * sizeof(Inodo));
    f.read(reinterpret_cast<char*>(&inodo), sizeof(Inodo));

    std::string nId  = "i" + std::to_string(inodoIdx);
    std::string lId  = "l" + std::to_string(inodoIdx);
    std::string perm = std::string(1,inodo.i_perm[0]) +
                       std::string(1,inodo.i_perm[1]) +
                       std::string(1,inodo.i_perm[2]);
    std::string color = (inodo.i_type==INODO_CARPETA) ? "#AED6F1" : "#FDEBD0";

    dot << "  " << lId << " [label=\"" << esc(nombreRuta)
        << "\" shape=plaintext fontname=\"Arial\"]\n"
        << "  " << nId << " [label=<\n"
        << "  <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"3\" BGCOLOR=\"" << color << "\">\n"
        << "  <TR><TD COLSPAN=\"2\"><B>inodo " << inodoIdx << "</B></TD></TR>\n"
        << "  <TR><TD>i_TYPE</TD><TD>" << inodo.i_type << "</TD></TR>\n";
    for (int b=0;b<3;b++)
        dot << "  <TR><TD>ap" << b << "</TD><TD>" << inodo.i_block[b] << "</TD></TR>\n";
    dot << "  <TR><TD>i_perm</TD><TD>" << perm << "</TD></TR>\n"
        << "  </TABLE>> shape=none margin=0]\n"
        << "  " << lId << " -> " << nId << " [style=dashed]\n";

    for (int b=0;b<12;b++) {
        if (inodo.i_block[b]==-1) continue;
        int bi = inodo.i_block[b];
        std::string bId = "b"+std::to_string(bi);

        if (inodo.i_type==INODO_CARPETA) {
            BloqueCarpeta bc{};
            f.seekg(sb.s_block_start + bi * sizeof(BloqueCarpeta));
            f.read(reinterpret_cast<char*>(&bc), sizeof(BloqueCarpeta));
            dot << "  " << bId << " [label=<\n"
                << "  <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"3\" BGCOLOR=\"#AED6F1\">\n"
                << "  <TR><TD COLSPAN=\"2\"><B>b. carpetas " << bi << "</B></TD></TR>\n";
            for (int e=0;e<4;e++) {
                std::string bn(bc.b_content[e].b_name, strnlen(bc.b_content[e].b_name,12));
                dot << "  <TR><TD>" << esc(bn.empty()?"-":bn)
                    << "</TD><TD>" << bc.b_content[e].b_inodo << "</TD></TR>\n";
            }
            dot << "  </TABLE>> shape=none margin=0]\n"
                << "  " << nId << " -> " << bId << "\n";
            for (int e=0;e<4;e++) {
                if (bc.b_content[e].b_inodo==-1) continue;
                std::string bn(bc.b_content[e].b_name, strnlen(bc.b_content[e].b_name,12));
                if (bn=="."||bn=="..") continue;
                std::string cL="l"+std::to_string(bc.b_content[e].b_inodo);
                dot << "  " << bId << " -> " << cL << "\n";
                generarTree(f, sb, bc.b_content[e].b_inodo,
                    nombreRuta=="/" ? "/"+bn : nombreRuta+"/"+bn, dot, visitados);
            }
        } else {
            BloqueArchivo ba{};
            f.seekg(sb.s_block_start + bi * sizeof(BloqueArchivo));
            f.read(reinterpret_cast<char*>(&ba), sizeof(BloqueArchivo));
            int bytes = std::min(64, inodo.i_s - b*64);
            if (bytes<0) bytes=0;
            std::string cont(ba.b_content, bytes);
            dot << "  " << bId << " [label=<\n"
                << "  <TABLE BORDER=\"1\" CELLBORDER=\"0\" CELLSPACING=\"0\" CELLPADDING=\"3\" BGCOLOR=\"#FDEBD0\">\n"
                << "  <TR><TD><B>b. archivos " << bi << "</B></TD></TR>\n"
                << "  <TR><TD>" << esc(cont) << "</TD></TR>\n"
                << "  </TABLE>> shape=none margin=0]\n"
                << "  " << nId << " -> " << bId << "\n";
        }
    }
    if (inodo.i_block[12]!=-1) {
        BloqueApuntadores bp{};
        f.seekg(sb.s_block_start + inodo.i_block[12] * sizeof(BloqueApuntadores));
        f.read(reinterpret_cast<char*>(&bp), sizeof(BloqueApuntadores));
        std::string bpId = "bp"+std::to_string(inodo.i_block[12]);
        dot << "  " << bpId << " [label=<\n"
            << "  <TABLE BORDER=\"1\" CELLBORDER=\"0\" CELLSPACING=\"0\" CELLPADDING=\"3\" BGCOLOR=\"#F1948A\">\n"
            << "  <TR><TD><B>b. apuntadores " << inodo.i_block[12] << "</B></TD></TR>\n  <TR><TD>";
        for (int p=0;p<16;p++) { dot<<bp.b_pointers[p]; if(p<15) dot<<", "; }
        dot << "</TD></TR>\n  </TABLE>> shape=none margin=0]\n"
            << "  " << nId << " -> " << bpId << "\n";
        for (int p=0;p<16;p++) {
            if (bp.b_pointers[p]==-1) continue;
            BloqueArchivo ba{};
            f.seekg(sb.s_block_start+bp.b_pointers[p]*sizeof(BloqueArchivo));
            f.read(reinterpret_cast<char*>(&ba), sizeof(BloqueArchivo));
            std::string cont(ba.b_content,strnlen(ba.b_content,64));
            std::string bn="b"+std::to_string(bp.b_pointers[p]);
            dot << "  " << bn << " [label=<\n"
                << "  <TABLE BORDER=\"1\" CELLBORDER=\"0\" CELLSPACING=\"0\" CELLPADDING=\"3\" BGCOLOR=\"#FDEBD0\">\n"
                << "  <TR><TD><B>b. archivos " << bp.b_pointers[p] << "</B></TD></TR>\n"
                << "  <TR><TD>" << esc(cont) << "</TD></TR>\n"
                << "  </TABLE>> shape=none margin=0]\n"
                << "  " << bpId << " -> " << bn << "\n";
        }
    }
}

static std::string repTree(const std::string& diskPath,
                             const std::string& id,
                             const std::string& outPath)
{
    ParticionMontada* pm = buscarMontada(id);
    if (!pm) throw std::runtime_error("REP TREE: id no encontrado: " + id);
    auto file = Utilities::OpenFile(diskPath);
    if (!file.is_open()) throw std::runtime_error("REP TREE: no se pudo abrir disco");
    SuperBloque sb = leerSB(file, pm->start);
    std::ostringstream dot;
    dot << "digraph G {\n  rankdir=LR\n  node [fontname=\"Arial\" fontsize=10]\n"
        << "  raiz [label=\"inodo de carpeta\\nraiz\" shape=plaintext]\n"
        << "  raiz -> l0\n";
    std::vector<int> visitados;
    generarTree(file, sb, 0, "/", dot, visitados);
    dot << "}\n";
    file.close();
    return dotAJpg(dot.str(), outPath);
}

// ============================================================
//  REP INODE
// ============================================================
static std::string repInode(const std::string& diskPath,
                              const std::string& id,
                              const std::string& rutaExt2,
                              const std::string& outPath)
{
    ParticionMontada* pm = buscarMontada(id);
    if (!pm) throw std::runtime_error("REP INODE: id no encontrado: " + id);
    auto file = Utilities::OpenFile(diskPath);
    if (!file.is_open()) throw std::runtime_error("REP INODE: no se pudo abrir disco");
    SuperBloque sb = leerSB(file, pm->start);
    int idx = resolverRuta(file, sb, rutaExt2);

    // Si no encuentra en EXT2, intentar desde carpeta física
    if (idx == -1) {
        std::string discNombre = std::filesystem::path(diskPath).stem().string();
        std::string rutaFisica = DISCOS_DIR + "/" + discNombre + rutaExt2;
        if (std::filesystem::exists(rutaFisica)) {
            // Buscar en EXT2 usando solo el nombre del archivo
            std::string nombre = std::filesystem::path(rutaFisica).filename().string();
            // Reconstruir ruta relativa desde raíz
            std::string rutaRel = rutaExt2;
            idx = resolverRuta(file, sb, rutaRel);
        }
        if (idx == -1) {
            file.close();
            throw std::runtime_error("REP INODE: ruta no encontrada: " + rutaExt2);
        }
    }

    std::ostringstream dot;
    dot << "digraph G {\n  node [shape=none margin=0 fontname=\"Arial\" fontsize=10]\n  rankdir=LR\n";

    std::vector<int> vis; std::vector<int> cola={idx}; std::string prev="";
    while (!cola.empty()) {
        int i=cola.front(); cola.erase(cola.begin());
        if (std::find(vis.begin(),vis.end(),i)!=vis.end()) continue;
        vis.push_back(i);
        Inodo inodo{};
        file.seekg(sb.s_inode_start+i*sizeof(Inodo));
        file.read(reinterpret_cast<char*>(&inodo),sizeof(Inodo));
        std::string at(inodo.i_atime,strnlen(inodo.i_atime,19));
        std::string perm=std::string(1,inodo.i_perm[0])+std::string(1,inodo.i_perm[1])+std::string(1,inodo.i_perm[2]);
        std::string nId="inodo"+std::to_string(i);
        dot << "  " << nId << " [label=<\n"
            << "  <TABLE BORDER=\"1\" CELLBORDER=\"0\" CELLSPACING=\"0\" CELLPADDING=\"4\" BGCOLOR=\"white\">\n"
            << "  <TR><TD COLSPAN=\"2\" ALIGN=\"CENTER\" BGCOLOR=\"#AED6F1\"><B>Inodo " << i << "</B></TD></TR>\n"
            << "  <TR><TD ALIGN=\"LEFT\">i_uid</TD><TD ALIGN=\"LEFT\">" << inodo.i_uid << "</TD></TR>\n"
            << "  <TR><TD ALIGN=\"LEFT\">i_size</TD><TD ALIGN=\"LEFT\">" << inodo.i_s << "</TD></TR>\n"
            << "  <TR><TD ALIGN=\"LEFT\">i_atime</TD><TD ALIGN=\"LEFT\">" << esc(at) << "</TD></TR>\n"
            << "  <TR><TD>.</TD><TD></TD></TR>\n";
        for (int b=0;b<15;b++)
            dot << "  <TR><TD ALIGN=\"LEFT\">i_block_"<<(b+1)<<"</TD><TD ALIGN=\"LEFT\">"<<inodo.i_block[b]<<"</TD></TR>\n";
        dot << "  <TR><TD>.</TD><TD></TD></TR>\n"
            << "  <TR><TD ALIGN=\"LEFT\">i_perm</TD><TD ALIGN=\"LEFT\">" << perm << "</TD></TR>\n"
            << "  <TR><TD>.</TD><TD></TD></TR>\n"
            << "  </TABLE>>]\n";
        if (!prev.empty()) dot << "  " << prev << " -> " << nId << "\n";
        prev=nId;
    }
    dot << "}\n";
    file.close();
    return dotAJpg(dot.str(), outPath);
}

// ============================================================
//  REP BLOCK
// ============================================================
static std::string repBlock(const std::string& diskPath,
                              const std::string& id,
                              const std::string& rutaExt2,
                              const std::string& outPath)
{
    ParticionMontada* pm = buscarMontada(id);
    if (!pm) throw std::runtime_error("REP BLOCK: id no encontrado: " + id);
    auto file = Utilities::OpenFile(diskPath);
    if (!file.is_open()) throw std::runtime_error("REP BLOCK: no se pudo abrir disco");
    SuperBloque sb = leerSB(file, pm->start);
    int idx = resolverRuta(file, sb, rutaExt2);

    // Fallback: carpeta física del disco
    if (idx == -1) {
        std::string discNombre = std::filesystem::path(diskPath).stem().string();
        std::string rutaFisica = DISCOS_DIR + "/" + discNombre + rutaExt2;
        if (std::filesystem::exists(rutaFisica))
            idx = resolverRuta(file, sb, rutaExt2);
        if (idx == -1) {
            file.close();
            throw std::runtime_error("REP BLOCK: ruta no encontrada: " + rutaExt2);
        }
    }

    Inodo inodo{};
    file.seekg(sb.s_inode_start+idx*sizeof(Inodo));
    file.read(reinterpret_cast<char*>(&inodo),sizeof(Inodo));

    std::ostringstream dot;
    dot << "digraph G {\n  node [shape=none margin=0 fontname=\"Arial\" fontsize=10]\n  rankdir=LR\n";
    std::string prev="";

    for (int b=0;b<12;b++) {
        if (inodo.i_block[b]==-1) continue;
        int bi=inodo.i_block[b];
        std::string nId="blk"+std::to_string(bi);
        if (inodo.i_type==INODO_CARPETA) {
            BloqueCarpeta bc{};
            file.seekg(sb.s_block_start+bi*sizeof(BloqueCarpeta));
            file.read(reinterpret_cast<char*>(&bc),sizeof(BloqueCarpeta));
            dot << "  " << nId << " [label=<\n"
                << "  <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"4\" BGCOLOR=\"white\">\n"
                << "  <TR><TD COLSPAN=\"2\" BGCOLOR=\"#AED6F1\"><B>Bloque Carpeta " << bi << "</B></TD></TR>\n"
                << "  <TR><TD><B>b_name</B></TD><TD><B>b_inodo</B></TD></TR>\n";
            for (int e=0;e<4;e++) {
                std::string bn(bc.b_content[e].b_name,strnlen(bc.b_content[e].b_name,12));
                dot << "  <TR><TD>" << esc(bn) << "</TD><TD>" << bc.b_content[e].b_inodo << "</TD></TR>\n";
            }
            dot << "  </TABLE>>]\n";
        } else {
            BloqueArchivo ba{};
            file.seekg(sb.s_block_start+bi*sizeof(BloqueArchivo));
            file.read(reinterpret_cast<char*>(&ba),sizeof(BloqueArchivo));
            int bytes=std::min(64,inodo.i_s-b*64); if(bytes<0)bytes=0;
            std::string cont(ba.b_content,bytes);
            dot << "  " << nId << " [label=<\n"
                << "  <TABLE BORDER=\"1\" CELLBORDER=\"0\" CELLSPACING=\"0\" CELLPADDING=\"4\" BGCOLOR=\"white\">\n"
                << "  <TR><TD BGCOLOR=\"#A9DFBF\"><B>Bloque Archivo " << bi << "</B></TD></TR>\n"
                << "  <TR><TD>" << esc(cont) << "</TD></TR>\n"
                << "  </TABLE>>]\n";
        }
        if (!prev.empty()) dot << "  " << prev << " -> " << nId << "\n";
        prev=nId;
    }
    if (inodo.i_block[12]!=-1) {
        BloqueApuntadores bp{};
        file.seekg(sb.s_block_start+inodo.i_block[12]*sizeof(BloqueApuntadores));
        file.read(reinterpret_cast<char*>(&bp),sizeof(BloqueApuntadores));
        std::string bpId="bap"+std::to_string(inodo.i_block[12]);
        dot << "  " << bpId << " [label=<\n"
            << "  <TABLE BORDER=\"1\" CELLBORDER=\"0\" CELLSPACING=\"0\" CELLPADDING=\"4\" BGCOLOR=\"white\">\n"
            << "  <TR><TD BGCOLOR=\"#F1948A\"><B>Bloque Apuntadores " << inodo.i_block[12] << "</B></TD></TR>\n"
            << "  <TR><TD>";
        for (int p=0;p<16;p++) { dot<<bp.b_pointers[p]; if(p<15)dot<<", "; }
        dot << "</TD></TR>\n  </TABLE>>]\n";
        if (!prev.empty()) dot << "  " << prev << " -> " << bpId << "\n";
        std::string pb=bpId;
        for (int p=0;p<16;p++) {
            if (bp.b_pointers[p]==-1) continue;
            BloqueArchivo ba{};
            file.seekg(sb.s_block_start+bp.b_pointers[p]*sizeof(BloqueArchivo));
            file.read(reinterpret_cast<char*>(&ba),sizeof(BloqueArchivo));
            std::string cont(ba.b_content,strnlen(ba.b_content,64));
            std::string bn="blk"+std::to_string(bp.b_pointers[p]);
            dot << "  " << bn << " [label=<\n"
                << "  <TABLE BORDER=\"1\" CELLBORDER=\"0\" CELLSPACING=\"0\" CELLPADDING=\"4\" BGCOLOR=\"white\">\n"
                << "  <TR><TD BGCOLOR=\"#A9DFBF\"><B>Bloque Archivo " << bp.b_pointers[p] << "</B></TD></TR>\n"
                << "  <TR><TD>" << esc(cont) << "</TD></TR>\n"
                << "  </TABLE>>]\n"
                << "  " << pb << " -> " << bn << "\n";
            pb=bn;
        }
    }
    dot << "}\n";
    file.close();
    return dotAJpg(dot.str(), outPath);
}

// ============================================================
//  REP FILE  → .txt con el contenido del archivo
// ============================================================
static std::string repFile(const std::string& diskPath,
                             const std::string& id,
                             const std::string& rutaExt2,
                             const std::string& outPath)
{
    ParticionMontada* pm = buscarMontada(id);
    if (!pm) throw std::runtime_error("REP FILE: id no encontrado: " + id);
    auto file = Utilities::OpenFile(diskPath);
    if (!file.is_open()) throw std::runtime_error("REP FILE: no se pudo abrir disco");
    SuperBloque sb = leerSB(file, pm->start);

    std::string contenido;

    // Intento 1: buscar en EXT2
    int idx = resolverRuta(file, sb, rutaExt2);
    if (idx != -1) {
        Inodo inodo{};
        file.seekg(sb.s_inode_start + idx * sizeof(Inodo));
        file.read(reinterpret_cast<char*>(&inodo), sizeof(Inodo));
        if (inodo.i_type != INODO_ARCHIVO) {
            file.close();
            throw std::runtime_error("REP FILE: no es un archivo: " + rutaExt2);
        }
        contenido = leerArchivoEXT2(file, sb, inodo);
    } else {
        // Intento 2: buscar en carpeta fisica del disco
        // DISCOS_DIR/<nombre_disco>/<ruta>
        std::string discNombre = std::filesystem::path(diskPath).stem().string();
        std::string rutaFisica = DISCOS_DIR + "/" + discNombre + rutaExt2;

        if (!std::filesystem::exists(rutaFisica)) {
            file.close();
            throw std::runtime_error("REP FILE: ruta no encontrada: " + rutaExt2 +
                "\n  (buscado en EXT2 y en: " + rutaFisica + ")");
        }
        std::ifstream fis(rutaFisica);
        if (!fis.is_open()) {
            file.close();
            throw std::runtime_error("REP FILE: no se pudo abrir: " + rutaFisica);
        }
        contenido = std::string((std::istreambuf_iterator<char>(fis)),
                                  std::istreambuf_iterator<char>());
        fis.close();
    }
    file.close();

    std::filesystem::create_directories(std::filesystem::path(outPath).parent_path());
    std::ofstream fout(outPath);
    if (!fout.is_open()) throw std::runtime_error("REP FILE: no se pudo crear: " + outPath);
    fout << contenido;
    fout.close();
    reporteRutas[std::filesystem::path(outPath).filename().string()] = outPath;
    return "REP: reporte generado en " + outPath;
}

// ============================================================
//  REP LS  → tabla Graphviz con el listado del directorio
// ============================================================
static std::string repLS(const std::string& diskPath,
                           const std::string& id,
                           const std::string& rutaExt2,
                           const std::string& outPath)
{
    ParticionMontada* pm = buscarMontada(id);
    if (!pm) throw std::runtime_error("REP LS: id no encontrado: " + id);
    auto file = Utilities::OpenFile(diskPath);
    if (!file.is_open()) throw std::runtime_error("REP LS: no se pudo abrir disco");
    SuperBloque sb = leerSB(file, pm->start);

    int idx = resolverRuta(file, sb, rutaExt2);

    // Si no existe en EXT2, buscar en carpeta física del disco
    if (idx == -1) {
        file.close();
        std::string discNombre = std::filesystem::path(diskPath).stem().string();
        std::string rutaFisica = DISCOS_DIR + "/" + discNombre + rutaExt2;
        if (!std::filesystem::exists(rutaFisica) ||
            !std::filesystem::is_directory(rutaFisica))
            throw std::runtime_error("REP LS: ruta no encontrada: " + rutaExt2 +
                "\n  (buscado en EXT2 y en: " + rutaFisica + ")");

        // Generar tabla con entradas del directorio físico
        std::ostringstream dot;
        dot << "digraph G {\n  node [shape=none margin=0 fontname=\"Arial\" fontsize=10]\n"
            << "  main [label=<\n"
            << "  <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"6\" BGCOLOR=\"white\">\n"
            << "  <TR>"
            << "<TD BGCOLOR=\"#2C3E50\"><FONT COLOR=\"white\"><B>Permisos</B></FONT></TD>"
            << "<TD BGCOLOR=\"#2C3E50\"><FONT COLOR=\"white\"><B>Owner</B></FONT></TD>"
            << "<TD BGCOLOR=\"#2C3E50\"><FONT COLOR=\"white\"><B>Grupo</B></FONT></TD>"
            << "<TD BGCOLOR=\"#2C3E50\"><FONT COLOR=\"white\"><B>Size (en Bytes)</B></FONT></TD>"
            << "<TD BGCOLOR=\"#2C3E50\"><FONT COLOR=\"white\"><B>Fecha</B></FONT></TD>"
            << "<TD BGCOLOR=\"#2C3E50\"><FONT COLOR=\"white\"><B>Hora</B></FONT></TD>"
            << "<TD BGCOLOR=\"#2C3E50\"><FONT COLOR=\"white\"><B>Tipo</B></FONT></TD>"
            << "<TD BGCOLOR=\"#2C3E50\"><FONT COLOR=\"white\"><B>Name</B></FONT></TD>"
            << "</TR>\n";

        bool alt = false;
        for (const auto& entry : std::filesystem::directory_iterator(rutaFisica)) {
            std::string nombre = entry.path().filename().string();
            std::string tipo   = entry.is_directory() ? "Carpeta" : "Archivo";
            std::string size   = entry.is_regular_file() ?
                std::to_string(entry.file_size()) : "0";
            std::string bg = alt ? "#D6EAF8" : "#FDFEFE";
            dot << "  <TR BGCOLOR=\"" << bg << "\">"
                << "<TD>-rw-rw-r--</TD>"
                << "<TD>-</TD><TD>-</TD>"
                << "<TD>" << size  << "</TD>"
                << "<TD>-</TD><TD>-</TD>"
                << "<TD>" << tipo  << "</TD>"
                << "<TD>" << esc(nombre) << "</TD>"
                << "</TR>\n";
            alt = !alt;
        }
        dot << "  </TABLE>>]\n}\n";
        return dotAJpg(dot.str(), outPath);
    }

    Inodo dir{};
    file.seekg(sb.s_inode_start + idx * sizeof(Inodo));
    file.read(reinterpret_cast<char*>(&dir), sizeof(Inodo));
    if (dir.i_type != INODO_CARPETA) {
        file.close();
        throw std::runtime_error("REP LS: no es directorio: " + rutaExt2);
    }

    std::ostringstream dot;
    dot << "digraph G {\n  node [shape=none margin=0 fontname=\"Arial\" fontsize=10]\n"
        << "  main [label=<\n"
        << "  <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"6\" BGCOLOR=\"white\">\n"
        << "  <TR>"
        << "<TD BGCOLOR=\"#2C3E50\"><FONT COLOR=\"white\"><B>Permisos</B></FONT></TD>"
        << "<TD BGCOLOR=\"#2C3E50\"><FONT COLOR=\"white\"><B>Owner</B></FONT></TD>"
        << "<TD BGCOLOR=\"#2C3E50\"><FONT COLOR=\"white\"><B>Grupo</B></FONT></TD>"
        << "<TD BGCOLOR=\"#2C3E50\"><FONT COLOR=\"white\"><B>Size (en Bytes)</B></FONT></TD>"
        << "<TD BGCOLOR=\"#2C3E50\"><FONT COLOR=\"white\"><B>Fecha</B></FONT></TD>"
        << "<TD BGCOLOR=\"#2C3E50\"><FONT COLOR=\"white\"><B>Hora</B></FONT></TD>"
        << "<TD BGCOLOR=\"#2C3E50\"><FONT COLOR=\"white\"><B>Tipo</B></FONT></TD>"
        << "<TD BGCOLOR=\"#2C3E50\"><FONT COLOR=\"white\"><B>Name</B></FONT></TD>"
        << "</TR>\n";

    bool alt=false;
    for (int b=0;b<12;b++) {
        if (dir.i_block[b]==-1) continue;
        BloqueCarpeta bc{};
        file.seekg(sb.s_block_start+dir.i_block[b]*sizeof(BloqueCarpeta));
        file.read(reinterpret_cast<char*>(&bc),sizeof(BloqueCarpeta));
        for (int e=0;e<4;e++) {
            if (bc.b_content[e].b_inodo==-1) continue;
            std::string bn(bc.b_content[e].b_name,strnlen(bc.b_content[e].b_name,12));
            if (bn=="."||bn=="..") continue;
            Inodo hijo{};
            file.seekg(sb.s_inode_start+bc.b_content[e].b_inodo*sizeof(Inodo));
            file.read(reinterpret_cast<char*>(&hijo),sizeof(Inodo));
            std::string owner=getUser(file,sb,hijo.i_uid);
            std::string grupo=getGroup(file,sb,hijo.i_gid);
            std::string perms=permStr(hijo.i_perm);
            std::string tipo=(hijo.i_type==INODO_ARCHIVO)?"Archivo":"Carpeta";
            std::string at(hijo.i_atime,strnlen(hijo.i_atime,19));
            std::string fecha=at.size()>=19?at.substr(0,10):at;
            std::string hora=at.size()>=19?at.substr(11,5):"";
            std::string bg=alt?"#D6EAF8":"#FDFEFE";
            dot << "  <TR BGCOLOR=\"" << bg << "\">"
                << "<TD>" << esc(perms) << "</TD>"
                << "<TD>" << esc(owner) << "</TD>"
                << "<TD>" << esc(grupo) << "</TD>"
                << "<TD>" << hijo.i_s   << "</TD>"
                << "<TD>" << esc(fecha) << "</TD>"
                << "<TD>" << esc(hora)  << "</TD>"
                << "<TD>" << esc(tipo)  << "</TD>"
                << "<TD>" << esc(bn)    << "</TD>"
                << "</TR>\n";
            alt=!alt;
        }
    }
    dot << "  </TABLE>>]\n}\n";
    file.close();
    return dotAJpg(dot.str(), outPath);
}

// ============================================================
//  PUNTO DE ENTRADA PRINCIPAL
// ============================================================
std::string Rep(const std::string& name,
                const std::string& outputPath,
                const std::string& id,
                const std::string& pathFileLs)
{
    // Validar -name
    static const std::vector<std::string> nombres = {
        "mbr","ebr","disk","inode","block",
        "bm_inode","bm_block","tree","sb","file","ls"
    };
    if (std::find(nombres.begin(),nombres.end(),name)==nombres.end())
        throw std::runtime_error(
            "REP: -name='" + name + "' no valido. "
            "Valores: mbr, ebr, disk, inode, block, bm_inode, bm_block, tree, sb, file, ls");

    // Validar -path y crear carpeta si no existe
    if (outputPath.empty())
        throw std::runtime_error("REP: falta -path");
    std::filesystem::path op(outputPath);
    if (op.has_parent_path())
        std::filesystem::create_directories(op.parent_path());

    // Validar -id
    if (id.empty())
        throw std::runtime_error("REP: falta -id");
    ParticionMontada* pm = buscarMontada(id);
    if (!pm)
        throw std::runtime_error("REP: particion '" + id + "' no esta montada");

    std::string diskPath = pm->path;

    // -path_file_ls: obligatorio para file y ls
    std::string rutaExt2 = pathFileLs;
    if ((name=="file"||name=="ls") && rutaExt2.empty())
        throw std::runtime_error("REP " + name + ": falta -path_file_ls");

    // Para inode y block: default "/"
    if ((name=="inode"||name=="block") && rutaExt2.empty())
        rutaExt2 = "/";

    if      (name=="mbr")      return repMBR(diskPath, outputPath);
    else if (name=="ebr")      return repEBR(diskPath, outputPath);
    else if (name=="disk")     return repDisk(diskPath, outputPath);
    else if (name=="sb")       return repSB(diskPath, id, outputPath);
    else if (name=="bm_inode") return repBitmap(diskPath, id, outputPath, true);
    else if (name=="bm_block") return repBitmap(diskPath, id, outputPath, false);
    else if (name=="tree")     return repTree(diskPath, id, outputPath);
    else if (name=="inode")    return repInode(diskPath, id, rutaExt2, outputPath);
    else if (name=="block")    return repBlock(diskPath, id, rutaExt2, outputPath);
    else if (name=="file")     return repFile(diskPath, id, rutaExt2, outputPath);
    else                       return repLS(diskPath, id, rutaExt2, outputPath);
}

// ============================================================
//  JOURNALING - Generar imagen de transacciones
// ============================================================
std::string GenerarImagenJournaling(const std::vector<RegistroJournal>& registros,
                                     const std::string& id)
{
    // Ruta correcta: /home/oscaredd/Calificacion_MIA/Reportes/journaling_[id].jpg
    std::string outPath = "/home/oscaredd/Calificacion_MIA/Reportes/journaling_" + id + ".jpg";
    
    // Crear directorio si no existe
    std::filesystem::create_directories(std::filesystem::path(outPath).parent_path());

    // Construir tabla en Graphviz DOT format - formato exacto del ejemplo
    std::ostringstream dot;
    dot << "digraph G {\n";
    dot << "  node [shape=none margin=0 fontname=\"Arial\"]\n";
    dot << "  main [label=<\n";
    dot << "  <TABLE BORDER=\"1\" CELLBORDER=\"1\" CELLSPACING=\"0\" CELLPADDING=\"5\" BGCOLOR=\"white\">\n";
    
    // Header principal con título
    dot << "  <TR><TD COLSPAN=\"4\" BGCOLOR=\"#003399\"><FONT COLOR=\"white\"><B>JOURNALING - Particion: " 
        << esc(id) << "</B></FONT></TD></TR>\n";
    
    // Fila de encabezados de columnas
    dot << "  <TR BGCOLOR=\"#3366CC\">\n";
    dot << "    <TD ALIGN=\"CENTER\"><FONT COLOR=\"white\"><B>Operacion</B></FONT></TD>\n";
    dot << "    <TD ALIGN=\"CENTER\"><FONT COLOR=\"white\"><B>Path</B></FONT></TD>\n";
    dot << "    <TD ALIGN=\"CENTER\"><FONT COLOR=\"white\"><B>Contenido</B></FONT></TD>\n";
    dot << "    <TD ALIGN=\"CENTER\"><FONT COLOR=\"white\"><B>Fecha</B></FONT></TD>\n";
    dot << "  </TR>\n";

    if (registros.empty()) {
        // Sin registros
        dot << "  <TR><TD COLSPAN=\"4\" ALIGN=\"CENTER\" BGCOLOR=\"white\"><I>(sin registros)</I></TD></TR>\n";
    } else {
        // Mostrar cada registro con fondo blanco
        for (const auto& reg : registros) {
            dot << "  <TR BGCOLOR=\"white\">\n";
            dot << "    <TD>" << esc(reg.operacion) << "</TD>\n";
            dot << "    <TD>" << esc(reg.path) << "</TD>\n";
            dot << "    <TD>" << esc(reg.contenido) << "</TD>\n";
            dot << "    <TD>" << esc(reg.fecha) << "</TD>\n";
            dot << "  </TR>\n";
        }
    }

    dot << "  </TABLE>>]\n";
    dot << "}\n";

    // Generar imagen usando dotAJpg (que devuelve la ruta)
    return dotAJpg(dot.str(), outPath);
}

} // namespace Reports