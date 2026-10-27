cmake_minimum_required(VERSION 3.21)
project(Prism VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(PRISM_ASAN "Build with AddressSanitizer" OFF)
if(PRISM_ASAN)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/fsanitize=address>)
    add_link_options(/fsanitize=address)
else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(NOT FFMPEG_DIR)
    set(FFMPEG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/_vendor/ffmpeg/ffmpeg-master-latest-win64-gpl-shared")
endif()
set(LIBASS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/_vendor/libass")

enable_language(ASM_NASM)
set(CMAKE_ASM_NASM_OBJECT_FORMAT win64)

set(PRISM_SOURCES
    src/entry.cpp
    src/common.cpp
    src/gfx.cpp
    src/paint.cpp
    src/asm_c.cpp
    src/asm/kernels.asm
    src/audio.cpp
    src/engine.cpp
    src/ass_render.cpp
    src/ui.cpp
    src/draw.cpp
    src/thumbs.cpp
)

add_executable(Prism WIN32 ${PRISM_SOURCES} resources/prism.rc)
if(PRISM_ASAN)
    target_compile_options(Prism PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:/Zi>)
    add_custom_command(TARGET Prism POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "D:/DevTools/VS2022/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll"
            "$<TARGET_FILE_DIR:Prism>")
endif()

target_include_directories(Prism PRIVATE src "${FFMPEG_DIR}/include" "${LIBASS_DIR}/include")
target_compile_options(Prism PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:/utf-8 /permissive- /W3 /MP /O2>)
target_compile_definitions(Prism PRIVATE UNICODE _UNICODE)
target_link_directories(Prism PRIVATE "${FFMPEG_DIR}/lib")
target_link_libraries(Prism PRIVATE
    d2d1
    psapi dwrite d3d11 dxgi d3dcompiler windowscodecs ole32 oleaut32 shlwapi shell32 dwmapi comctl32 uxtheme
    avformat avcodec avutil swresample swscale
)
target_link_options(Prism PRIVATE /SUBSYSTEM:WINDOWS /MANIFEST:NO /MAP)

set(FFMPEG_DLLS avcodec-63.dll avformat-63.dll avutil-61.dll swresample-7.dll swscale-10.dll)
foreach(dll ${FFMPEG_DLLS})
    add_custom_command(TARGET Prism POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_DIR}/bin/${dll}" "$<TARGET_FILE_DIR:Prism>/${dll}"
        COMMENT "Copy ${dll}")
endforeach()

# libass is loaded dynamically (LoadLibrary + GetProcAddress), so no import lib.
add_custom_command(TARGET Prism POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIBASS_DIR}/libass-9.dll" "$<TARGET_FILE_DIR:Prism>/libass-9.dll"
    COMMENT "Copy libass-9.dll")

if(NOT PRISM_ASAN)
    add_executable(prism_bench tools/bench.cpp src/asm_c.cpp src/asm/kernels.asm)
    target_include_directories(prism_bench PRIVATE src)
    target_compile_options(prism_bench PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:/utf-8 /O2 /W3>)
    set_target_properties(prism_bench PROPERTIES OUTPUT_NAME "prism_bench")
endif()
