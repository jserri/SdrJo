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
