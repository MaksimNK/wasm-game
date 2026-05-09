cmake_minimum_required(VERSION 3.10)
project(audio_visualizer)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_CXX_STANDARD 11)

find_package(SDL2 REQUIRED)

set(SOURCES
    src/main.cpp
    src/audio.cpp
    src/game.cpp
    deps/kiss_fft.c
)

include_directories(include deps)

add_executable(game ${SOURCES})
target_link_libraries(game SDL2::SDL2 m)

if(EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".js")
    set_target_properties(game PROPERTIES
        LINK_FLAGS "-s WASM=1 -s USE_SDL=2 -s EXPORTED_FUNCTIONS='[\"_main\"]' -s EXPORTED_RUNTIME_METHODS='[\"ccall\", \"cwrap\"]' -s ALLOW_MEMORY_GROWTH=1 --preload-file ${CMAKE_SOURCE_DIR}/audio -lm"
    )
endif()
