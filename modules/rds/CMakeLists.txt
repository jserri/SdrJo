add_library(rds-decoder STATIC
    src/rds_blocks.cpp
    src/rds_demod.cpp
)
target_include_directories(rds-decoder PUBLIC include)
target_link_libraries(rds-decoder PUBLIC sdrjo-core)

add_library(sdrjo_module_rds MODULE src/rds_module.cpp)
target_link_libraries(sdrjo_module_rds PRIVATE rds-decoder)
set_target_properties(sdrjo_module_rds PROPERTIES PREFIX "")

install(TARGETS sdrjo_module_rds LIBRARY DESTINATION modules)
