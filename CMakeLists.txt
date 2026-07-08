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

# librtlsdr: facoltativa: senza, l'app funziona solo da file IQ registrati.
# Per la RTL-SDR Blog V4 serve il driver del fork rtl-sdr-blog (>= 2023).
find_library(RTLSDR_LIBRARY rtlsdr)
find_path(RTLSDR_INCLUDE_DIR rtl-sdr.h)
if(RTLSDR_LIBRARY AND RTLSDR_INCLUDE_DIR)
    set(SDRJO_HAVE_RTLSDR ON)
    message(STATUS "librtlsdr trovata: ${RTLSDR_LIBRARY}")
else()
    set(SDRJO_HAVE_RTLSDR OFF)
    message(STATUS "librtlsdr NON trovata: la sorgente hardware RTL-SDR sara' disabilitata")
endif()

add_subdirectory(core)
add_subdirectory(modules/adsb)
add_subdirectory(modules/morse)
add_subdirectory(modules/meteor_lrpt)

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
