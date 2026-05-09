cmake_minimum_required(VERSION 3.10)
project(sdl2_square)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_CXX_STANDARD 11)

# Find SDL2
find_package(SDL2 REQUIRED)

add_executable(game src/main.cpp)

target_link_libraries(game SDL2::SDL2)

# Emscripten build
if(EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".js")
    set_target_properties(game PROPERTIES
        LINK_FLAGS "-s WASM=1 -s USE_SDL=2 -s EXPORTED_FUNCTIONS='[\"_main\"]' -s EXPORTED_RUNTIME_METHODS='[\"ccall\", \"cwrap\"]' -s ALLOW_MEMORY_GROWTH=1 --shell-file ${CMAKE_SOURCE_DIR}/shell.html"
    )
endif()
