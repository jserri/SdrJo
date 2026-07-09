add_library(apt-decoder STATIC
    src/apt_decoder.cpp
)
target_include_directories(apt-decoder PUBLIC include)
target_link_libraries(apt-decoder PUBLIC sdrjo-core)

add_library(sdrjo_module_noaa_apt MODULE src/apt_module.cpp)
target_link_libraries(sdrjo_module_noaa_apt PRIVATE apt-decoder)
set_target_properties(sdrjo_module_noaa_apt PROPERTIES PREFIX "")
