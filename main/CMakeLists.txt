set(app_sources
    "main.cpp"
    "app/Application.cpp"
    "runtime/RuntimeKernel.cpp"
    "communication/JsonProtocol.cpp"
    "communication/MessageGateway.cpp"
    "config/ConfigEngine.cpp"
)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "."
    REQUIRES arduinojson esp_timer
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_20)
