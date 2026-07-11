add_library(sdrjo-core STATIC
    src/dsp/fft.cpp
    src/dsp/fir_filter.cpp
    src/dsp/demod.cpp
    src/dsp/resampler.cpp
    src/dsp/correction.cpp
    src/dsp/wfm_stereo.cpp
    src/dsp/ssb.cpp
    src/dsp/vfo.cpp
    src/dsp/rtty.cpp
    src/dsp/psk31.cpp
    src/audio/audio_output.cpp
    src/util/iq_recorder.cpp
    src/util/band_plan.cpp
    src/util/frequency_store.cpp
    src/module/module_loader.cpp
    src/util/http_server.cpp
    src/util/geolocate.cpp
    src/util/wav_writer.cpp
    src/util/adpcm.cpp
    src/web/ws_audio_server.cpp
    src/util/tcp_line_server.cpp
    src/sat/tle.cpp
    src/sat/orbit.cpp
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

# ---------------------------------------------------------------------------
# miniaudio: scaricata alla configurazione (header singolo). Se il download
# fallisce (build offline) l'uscita audio usa il backend nullo.
# ---------------------------------------------------------------------------
option(SDRJO_ENABLE_AUDIO "Uscita audio con miniaudio" ON)
if(SDRJO_ENABLE_AUDIO)
    set(MINIAUDIO_H ${CMAKE_BINARY_DIR}/third_party/miniaudio/miniaudio.h)
    if(NOT EXISTS ${MINIAUDIO_H})
        file(DOWNLOAD
            https://raw.githubusercontent.com/mackron/miniaudio/0.11.21/miniaudio.h
            ${MINIAUDIO_H} STATUS MA_STATUS TIMEOUT 60)
        list(GET MA_STATUS 0 MA_CODE)
        if(NOT MA_CODE EQUAL 0)
            file(REMOVE ${MINIAUDIO_H})
        endif()
    endif()
    if(EXISTS ${MINIAUDIO_H})
        target_include_directories(sdrjo-core PRIVATE
            ${CMAKE_BINARY_DIR}/third_party/miniaudio)
        target_compile_definitions(sdrjo-core PRIVATE SDRJO_HAVE_MINIAUDIO=1)
        message(STATUS "miniaudio disponibile: uscita audio attiva")
    else()
        message(STATUS "miniaudio non scaricabile: uscita audio disabilitata (backend nullo)")
    endif()
endif()
