# Libreria statica con la logica di decodifica (usata da test e CLI)...
add_library(adsb-decoder STATIC
    src/mode_s.cpp
    src/preamble_detector.cpp
    src/aircraft_tracker.cpp
    src/adsb_json.cpp
    src/sbs_output.cpp
    src/adsb_web_page.cpp
)
target_include_directories(adsb-decoder PUBLIC include)
target_link_libraries(adsb-decoder PUBLIC sdrjo-core)

# ...e il modulo plugin caricabile dall'host.
add_library(sdrjo_module_adsb MODULE src/adsb_module.cpp)
target_link_libraries(sdrjo_module_adsb PRIVATE adsb-decoder)
set_target_properties(sdrjo_module_adsb PROPERTIES PREFIX "")

install(TARGETS sdrjo_module_adsb LIBRARY DESTINATION modules)
