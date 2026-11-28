cmake_minimum_required(VERSION 3.13)
cmake_policy(VERSION 3.13)

pkg_check_modules(McpBridgeGio REQUIRED IMPORTED_TARGET "gio-2.0 >= 2.32.0")

add_executable(morexournal-mcp main.cpp)
target_compile_features(morexournal-mcp PRIVATE cxx_std_20)
target_link_libraries(morexournal-mcp PRIVATE xoj::defaults nlohmann::json PkgConfig::McpBridgeGio)
set_target_properties(morexournal-mcp PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        )

if (APPLE)
    target_link_options(morexournal-mcp PRIVATE "-Wl,-headerpad_max_install_names")
endif()

install(TARGETS morexournal-mcp
        RUNTIME DESTINATION bin
        COMPONENT morexournal
        )
