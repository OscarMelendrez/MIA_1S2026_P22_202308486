cmake_minimum_required(VERSION 3.10)
project(Backend)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Archivos fuente (rutas exactas según tu proyecto) ────────
add_executable(Backend
    main.cpp
    controller/handler.cpp
    Analyzer/Analyzer.cpp
    controller/DiskManagement/DiskManagement.cpp
    controller/FileSystem/FileSystem.cpp
    controller/UserManagement/UserManagement.cpp
    controller/FileManagement/FileManagement.cpp
    controller/Reports/Reports.cpp
    Utilities/utilities.cpp
)

# ── Directorios de includes ───────────────────────────────────
target_include_directories(Backend PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/external
    ${PROJECT_SOURCE_DIR}/model
    ${PROJECT_SOURCE_DIR}/Analyzer
    ${PROJECT_SOURCE_DIR}/controller
    ${PROJECT_SOURCE_DIR}/controller/DiskManagement
    ${PROJECT_SOURCE_DIR}/controller/FileSystem
    ${PROJECT_SOURCE_DIR}/controller/UserManagement
    ${PROJECT_SOURCE_DIR}/controller/FileManagement
    ${PROJECT_SOURCE_DIR}/controller/Reports
    ${PROJECT_SOURCE_DIR}/Utilities
)

# ── Librerías ─────────────────────────────────────────────────
target_link_libraries(Backend PRIVATE pthread)

# ── Fix para crow_all.h (header-only con definiciones duplicadas) ──
# Crow define funciones en el .h que aparecen en múltiples .cpp.
# -Wl,--allow-multiple-definition le dice al linker que lo permita.
target_link_options(Backend PRIVATE -Wl,--allow-multiple-definition)