set(SDL2_ROOT "${CMAKE_SOURCE_DIR}/third_party/SDL2-2.32.10")

if(WIN32)
    add_executable(zera330c main.c intro.c config.c zera330.rc)
else()
    add_executable(zera330c main.c intro.c config.c)
endif()
target_link_libraries(zera330c PRIVATE neocore)
if(MSVC)
    target_compile_options(zera330c PRIVATE /utf-8)
endif()

target_include_directories(zera330c PRIVATE "${SDL2_ROOT}/include" "${CMAKE_SOURCE_DIR}/third_party/miniz")
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(SDL2_LIBDIR "${SDL2_ROOT}/lib/x64")
else()
    set(SDL2_LIBDIR "${SDL2_ROOT}/lib/x86")
endif()
target_link_directories(zera330c PRIVATE "${SDL2_LIBDIR}")
target_link_libraries(zera330c PRIVATE SDL2 SDL2main)

# SDL2.dll precisa estar ao lado do .exe no Windows.
add_custom_command(TARGET zera330c POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDL2_LIBDIR}/SDL2.dll" "$<TARGET_FILE_DIR:zera330c>")

# Abertura da marca (assets/zera330_intro.z3i) ao lado do .exe.
add_custom_command(TARGET zera330c POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:zera330c>/assets")
