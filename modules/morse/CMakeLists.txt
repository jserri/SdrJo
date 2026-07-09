add_library(morse-decoder STATIC
    src/cw_decoder.cpp
    src/morse_table.cpp
)
target_include_directories(morse-decoder PUBLIC include)
target_link_libraries(morse-decoder PUBLIC sdrjo-core)

add_library(sdrjo_module_morse MODULE src/morse_module.cpp)
target_link_libraries(sdrjo_module_morse PRIVATE morse-decoder)
set_target_properties(sdrjo_module_morse PROPERTIES PREFIX "")

install(TARGETS sdrjo_module_morse LIBRARY DESTINATION modules)
