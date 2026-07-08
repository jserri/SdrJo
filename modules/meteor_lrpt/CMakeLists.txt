add_library(lrpt-decoder STATIC
    src/viterbi27.cpp
    src/ccsds.cpp
    src/qpsk_demodulator.cpp
)
target_include_directories(lrpt-decoder PUBLIC include)
target_link_libraries(lrpt-decoder PUBLIC sdrjo-core)

add_library(sdrjo_module_meteor_lrpt MODULE src/lrpt_module.cpp)
target_link_libraries(sdrjo_module_meteor_lrpt PRIVATE lrpt-decoder)
set_target_properties(sdrjo_module_meteor_lrpt PROPERTIES PREFIX "")
