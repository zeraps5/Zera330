cmake_minimum_required(VERSION 3.16)
project(neogeo_emu C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

enable_testing()

# Versão e commit mostrados no launcher.
set(ZERA330_VERSION "0.1.0")
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE ZERA330_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT ZERA330_GIT_COMMIT)
    set(ZERA330_GIT_COMMIT "local")
endif()

add_subdirectory(core)

# Fase 6 do roadmap: frontend SDL2 (adiantado - ver docs/ROADMAP.md).
# SDL2 vendorizado em third_party/SDL2-2.32.10 (baixado de
# github.com/libsdl-org/SDL/releases, licença zlib).
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/SDL2-2.32.10")
    # Emulador (zera330c.exe) e launcher (Zera330.exe) lado a lado em build/bin,
    # como o ps5c.exe e o ZeraPS5.exe.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    add_subdirectory(platform/sdl2)
    if(WIN32)
        add_subdirectory(frontends/launcher)
    endif()
else()
    message(STATUS "third_party/SDL2-2.32.10 nao encontrado - pulando frontend SDL2")
endif()
