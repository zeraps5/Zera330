# Launcher do zera330 (Windows): Win32 puro, no modelo do launcher do ZeraPS5.
add_executable(Zera330 WIN32 launcher.c launcher.rc)
target_compile_definitions(Zera330 PRIVATE ZERA330_VERSION="${ZERA330_VERSION}" ZERA330_GIT_COMMIT="${ZERA330_GIT_COMMIT}")
# Cabeçalhos do SDL2: o launcher carrega o SDL2.dll do jogo pra ler os controles.
target_include_directories(Zera330 PRIVATE "${CMAKE_SOURCE_DIR}/third_party/SDL2-2.32.10/include")
target_link_libraries(Zera330 PRIVATE comctl32 shell32 ole32 gdiplus uuid)
if(MSVC)
    target_link_options(Zera330 PRIVATE /MANIFEST:NO)  # o manifesto vem do launcher.rc
endif()
if(MSVC)
    target_compile_options(Zera330 PRIVATE /utf-8)
endif()
add_custom_command(TARGET Zera330 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Zera330>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/docs/img/zera330-logo.png" "$<TARGET_FILE_DIR:Zera330>/assets/zera330-logo.png")
