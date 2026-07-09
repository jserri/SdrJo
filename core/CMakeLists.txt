add_library(sdrjo-core STATIC
    src/dsp/fft.cpp
    src/dsp/fir_filter.cpp
    src/dsp/demod.cpp
    src/module/module_loader.cpp
    src/util/http_server.cpp
)

if(WIN32)
    target_link_libraries(sdrjo-core PUBLIC ws2_32)
endif()

target_include_directories(sdrjo-core PUBLIC include)

if(SDRJO_HAVE_RTLSDR)
    target_sources(sdrjo-core PRIVATE src/source/rtl_sdr_source.cpp)
    target_include_directories(sdrjo-core PRIVATE ${RTLSDR_INCLUDE_DIR})
    target_link_libraries(sdrjo-core PUBLIC ${RTLSDR_LIBRARY})
    target_compile_definitions(sdrjo-core PUBLIC SDRJO_HAVE_RTLSDR=1)
endif()

# Sorgente da file IQ registrato: sempre disponibile (utile per test/replay)
target_sources(sdrjo-core PRIVATE src/source/file_source.cpp)

if(UNIX)
    target_link_libraries(sdrjo-core PUBLIC dl pthread)
endif()
