#pragma once
#include <string>

namespace Users {

// Inicia sesión en el sistema.
// user : nombre del usuario (case-sensitive)
// pass : contraseña (case-sensitive)
// id   : ID de la partición montada donde iniciar sesión
std::string Login(const std::string& user,
                  const std::string& pass,
                  const std::string& id);

// Cierra la sesión activa.
std::string Logout();

// Crea un grupo en users.txt. Solo root puede ejecutarlo.
// name : nombre del grupo (max 10 chars, case-sensitive)
std::string Mkgrp(const std::string& name);

// Elimina un grupo de users.txt. Solo root puede ejecutarlo.
// name : nombre del grupo a eliminar
std::string Rmgrp(const std::string& name);

// Crea un usuario en users.txt. Solo root puede ejecutarlo.
// user : nombre del usuario (max 10 chars)
// pass : contraseña (max 10 chars)
// grp  : grupo al que pertenece (debe existir)
std::string Mkusr(const std::string& user,
                  const std::string& pass,
                  const std::string& grp);

// Elimina un usuario de users.txt. Solo root puede ejecutarlo.
// user : nombre del usuario a eliminar
std::string Rmusr(const std::string& user);

// Cambia el grupo de un usuario. Solo root puede ejecutarlo.
// user : nombre del usuario
// grp  : nuevo grupo
std::string Chgrp(const std::string& user,
                  const std::string& grp);

}