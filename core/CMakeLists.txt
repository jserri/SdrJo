add_library(sdrjo-core STATIC
    src/dsp/fft.cpp
    src/dsp/fir_filter.cpp
    src/dsp/demod.cpp
    src/module/module_loader.cpp
    src/util/http_server.cpp
    src/util/tcp_line_server.cpp
    src/util/bmp_writer.cpp
    src/web/cockpit_server.cpp
    src/web/cockpit_page.cpp
)

if(WIN32)
    target_link_libraries(sdrjo-core PUBLIC ws2_32)
endif()

target_include_directories(sdrjo-core PUBLIC include)

# Sorgente RTL-SDR: la librtlsdr viene caricata a runtime, quindi il
# wrapper si compila sempre, senza dipendenze.
target_sources(sdrjo-core PRIVATE src/source/rtl_sdr_source.cpp)

# Sorgente da file IQ registrato: sempre disponibile (utile per test/replay)
target_sources(sdrjo-core PRIVATE src/source/file_source.cpp)

if(UNIX)
    target_link_libraries(sdrjo-core PUBLIC dl pthread)
endif()
