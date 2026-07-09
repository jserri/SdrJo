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
