add_executable(sdrjo-cli main.cpp)
target_link_libraries(sdrjo-cli PRIVATE
    sdrjo-core
    adsb-decoder
    morse-decoder
    lrpt-decoder
)
