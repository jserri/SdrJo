add_library(sensors433-decoder STATIC
    src/ook_detector.cpp
    src/nexus_decoder.cpp
)
target_include_directories(sensors433-decoder PUBLIC include)
target_link_libraries(sensors433-decoder PUBLIC sdrjo-core)

add_library(sdrjo_module_sensors433 MODULE src/sensors_module.cpp)
target_link_libraries(sdrjo_module_sensors433 PRIVATE sensors433-decoder)
set_target_properties(sdrjo_module_sensors433 PROPERTIES PREFIX "")

install(TARGETS sdrjo_module_sensors433 LIBRARY DESTINATION modules)
