cmake_minimum_required(VERSION 3.16)
project(SdrJo VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Le librerie statiche vengono linkate anche nei moduli plugin (.so/.dll):
# serve codice position-independent ovunque.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    # M_PI e soci da <cmath>, niente macro min/max da windows.h.
    add_compile_definitions(_USE_MATH_DEFINES NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# Tutti gli eseguibili in build/bin e i moduli plugin in build/bin/modules,
# cosi' l'app trova i moduli senza copiare file a mano.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/modules)
foreach(cfg IN ITEMS RELEASE DEBUG RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR}/bin/modules)
endforeach()

# ---------------------------------------------------------------------------
# Opzioni di build
# ---------------------------------------------------------------------------
option(SDRJO_BUILD_TESTS "Compila i test automatici" ON)
option(SDRJO_BUILD_CLI   "Compila lo strumento a riga di comando" ON)
if(WIN32)
    option(SDRJO_BUILD_GUI "Compila la GUI (Dear ImGui + GLFW)" ON)
else()
    option(SDRJO_BUILD_GUI "Compila la GUI (Dear ImGui + GLFW)" OFF)
endif()

# librtlsdr viene caricata A RUNTIME (rtlsdr.dll su Windows, librtlsdr.so
# su Linux): non serve nulla in fase di compilazione. Basta mettere la DLL
# del driver rtl-sdr-blog accanto all'eseguibile (vedi README).

add_subdirectory(core)
add_subdirectory(modules/adsb)
add_subdirectory(modules/morse)
add_subdirectory(modules/meteor_lrpt)
add_subdirectory(modules/rds)
add_subdirectory(modules/noaa_apt)

if(SDRJO_BUILD_CLI)
    add_subdirectory(app/cli)
endif()

if(SDRJO_BUILD_GUI)
    add_subdirectory(app/gui)
endif()

if(SDRJO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
