add_executable(test_mode_s test_mode_s.cpp)
target_link_libraries(test_mode_s PRIVATE adsb-decoder)
add_test(NAME mode_s COMMAND test_mode_s)

add_executable(test_morse test_morse.cpp)
target_link_libraries(test_morse PRIVATE morse-decoder)
add_test(NAME morse COMMAND test_morse)

add_executable(test_lrpt test_lrpt.cpp)
target_link_libraries(test_lrpt PRIVATE lrpt-decoder)
add_test(NAME lrpt COMMAND test_lrpt)

add_executable(test_dsp test_dsp.cpp)
target_link_libraries(test_dsp PRIVATE sdrjo-core)
add_test(NAME dsp COMMAND test_dsp)

add_executable(test_adsb_rf test_adsb_rf.cpp)
target_link_libraries(test_adsb_rf PRIVATE adsb-decoder)
add_test(NAME adsb_rf COMMAND test_adsb_rf)

add_executable(test_geo test_geo.cpp)
target_link_libraries(test_geo PRIVATE sdrjo-core)
add_test(NAME geo COMMAND test_geo)

add_executable(test_adsb_server test_adsb_server.cpp)
target_link_libraries(test_adsb_server PRIVATE adsb-decoder)
add_test(NAME adsb_server COMMAND test_adsb_server)

add_executable(test_sbs test_sbs.cpp)
target_link_libraries(test_sbs PRIVATE adsb-decoder)
add_test(NAME sbs COMMAND test_sbs)

add_executable(test_rds test_rds.cpp)
target_link_libraries(test_rds PRIVATE rds-decoder)
add_test(NAME rds COMMAND test_rds)

add_executable(test_apt test_apt.cpp)
target_link_libraries(test_apt PRIVATE apt-decoder)
add_test(NAME apt COMMAND test_apt)

add_executable(test_cockpit test_cockpit.cpp)
target_link_libraries(test_cockpit PRIVATE sdrjo-core)
add_test(NAME cockpit COMMAND test_cockpit)

add_executable(test_sources test_sources.cpp)
target_link_libraries(test_sources PRIVATE sdrjo-core)
add_test(NAME sources COMMAND test_sources)

add_executable(test_dsp_advanced test_dsp_advanced.cpp)
target_link_libraries(test_dsp_advanced PRIVATE sdrjo-core)
add_test(NAME dsp_advanced COMMAND test_dsp_advanced)

add_executable(test_recorder_audio test_recorder_audio.cpp)
target_link_libraries(test_recorder_audio PRIVATE sdrjo-core)
add_test(NAME recorder_audio COMMAND test_recorder_audio)

add_executable(test_bands test_bands.cpp)
target_link_libraries(test_bands PRIVATE sdrjo-core)
add_test(NAME bands COMMAND test_bands)

add_executable(test_freq_store test_freq_store.cpp)
target_link_libraries(test_freq_store PRIVATE sdrjo-core)
add_test(NAME freq_store COMMAND test_freq_store)

add_executable(test_geolocate test_geolocate.cpp)
target_link_libraries(test_geolocate PRIVATE sdrjo-core)
add_test(NAME geolocate COMMAND test_geolocate)

add_executable(test_filters_nb test_filters_nb.cpp)
target_link_libraries(test_filters_nb PRIVATE sdrjo-core)
add_test(NAME filters_nb COMMAND test_filters_nb)

add_executable(test_wav test_wav.cpp)
target_link_libraries(test_wav PRIVATE sdrjo-core)
add_test(NAME wav COMMAND test_wav)

add_executable(test_sat test_sat.cpp)
target_link_libraries(test_sat PRIVATE sdrjo-core)
add_test(NAME sat COMMAND test_sat)

add_executable(test_sensors433 test_sensors433.cpp)
target_link_libraries(test_sensors433 PRIVATE sensors433-decoder)
add_test(NAME sensors433 COMMAND test_sensors433)

add_executable(test_fft_window test_fft_window.cpp)
target_link_libraries(test_fft_window PRIVATE sdrjo-core)
add_test(NAME fft_window COMMAND test_fft_window)

add_executable(test_ws_audio test_ws_audio.cpp)
target_link_libraries(test_ws_audio PRIVATE sdrjo-core)
add_test(NAME ws_audio COMMAND test_ws_audio)

add_executable(test_rtty test_rtty.cpp)
target_link_libraries(test_rtty PRIVATE sdrjo-core)
add_test(NAME rtty COMMAND test_rtty)
