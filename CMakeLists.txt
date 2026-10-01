cmake_minimum_required(VERSION 4.1)
project(Laba2)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(FLTK_BUILD_TEST OFF CACHE BOOL "Build FLTK test programs" FORCE)
set(FLTK_BUILD_EXAMPLES OFF CACHE BOOL "Build FLTK examples" FORCE)
set(FLTK_BUILD_FLUID OFF CACHE BOOL "Build FLUID" FORCE)
set(FLTK_BUILD_FLTK_OPTIONS OFF CACHE BOOL "Build fltk-options" FORCE)
set(FLTK_BUILD_FORMS OFF CACHE BOOL "Build XForms compatibility" FORCE)
set(FLTK_BUILD_GL OFF CACHE BOOL "Build OpenGL support" FORCE)
set(FLTK_BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared FLTK libraries" FORCE)
set(FLTK_BUILD_HTML_DOCS OFF CACHE BOOL "Build HTML documentation" FORCE)
set(FLTK_BUILD_PDF_DOCS OFF CACHE BOOL "Build PDF documentation" FORCE)
set(FLTK_BUILD_FLUID_DOCS OFF CACHE BOOL "Build FLUID documentation" FORCE)

if(APPLE)
    set(FLTK_BACKEND_X11 OFF CACHE BOOL "Use X11 backend" FORCE)
endif()

if(WIN32)
    set(FLTK_MSVC_RUNTIME_DLL OFF CACHE BOOL "Static MSVC runtime" FORCE)
endif()

add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fltk-1.4.5"
        "${CMAKE_CURRENT_BINARY_DIR}/third_party/fltk-1.4.5"
        EXCLUDE_FROM_ALL
)

add_library(laba2_core STATIC
        src/model/ImageFormat.cpp
        src/model/ImageRecord.cpp
        src/model/ScanState.cpp
        src/core/FileHead.cpp
        src/core/FormatDetector.cpp
        src/core/MetaExtractor.cpp
        src/core/parsers/BmpParser.cpp
        src/core/parsers/PngParser.cpp
        src/core/parsers/JpegParser.cpp
        src/core/parsers/GifParser.cpp
        src/core/parsers/TiffParser.cpp
        src/core/parsers/PcxParser.cpp
        src/core/parsers/TiffTags.cpp
        src/core/codec/Crc32.cpp
        src/core/codec/Inflate.cpp
        src/core/codec/PngDecoder.cpp
        src/core/codec/Lzw.cpp
        src/core/codec/GifDecoder.cpp
        src/core/codec/RleCodec.cpp
        src/core/codec/BmpDecoder.cpp
        src/core/codec/PcxDecoder.cpp
        src/core/codec/TiffDecoder.cpp
        src/core/codec/JpegDecoder.cpp
        src/core/codec/PixelAnalyzer.cpp
        src/core/DecoderRegistry.cpp
        src/io/DirectoryScanner.cpp
        src/io/ThreadPool.cpp
        src/controller/ScanController.cpp
        src/controller/CsvReport.cpp
)

target_include_directories(laba2_core PUBLIC src)

if(MSVC)
    target_compile_options(laba2_core PUBLIC /utf-8 /W3 /permissive-)
else()
    target_compile_options(laba2_core PRIVATE -Wall -Wextra)
endif()

if(MSVC)
    target_compile_definitions(laba2_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_executable(Laba2 WIN32 MACOSX_BUNDLE
        src/main.cpp
        src/view/MainWindow.cpp
        src/view/ResultsTable.cpp
        src/view/PreviewCanvas.cpp
        src/view/PreviewWindow.cpp
        src/view/DetailPanel.cpp
        src/view/StatusBar.cpp
        src/view/FieldHelp.cpp
        src/view/PathUtil.cpp
)

target_link_libraries(Laba2 PRIVATE laba2_core fltk::fltk)

add_executable(Laba2Scan
        src/main_console.cpp
)

target_link_libraries(Laba2Scan PRIVATE laba2_core)

enable_testing()

add_test(NAME lab2_check_folder
        COMMAND Laba2Scan "${CMAKE_CURRENT_SOURCE_DIR}/task/Для проверки Lab2"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

add_test(NAME lab2_check_folder_csv
        COMMAND Laba2Scan --csv "${CMAKE_CURRENT_SOURCE_DIR}/task/Для проверки Lab2"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_test(NAME lab2_generated_corpus
            COMMAND Laba2Scan --strict "${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME lab2_generated_corpus_deep
            COMMAND Laba2Scan --deep --strict "${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/corrupt")
    add_test(NAME lab2_corrupt_samples
            COMMAND Laba2Scan --deep "${CMAKE_CURRENT_SOURCE_DIR}/tests/corrupt"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_test(NAME lab2_corrupt_samples_strict
            COMMAND Laba2Scan --deep --strict "${CMAKE_CURRENT_SOURCE_DIR}/tests/corrupt"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(lab2_corrupt_samples_strict PROPERTIES WILL_FAIL TRUE)

    add_test(NAME lab2_corrupt_samples_summary
            COMMAND Laba2Scan --deep --csv "${CMAKE_CURRENT_SOURCE_DIR}/tests/corrupt"
                    "${CMAKE_CURRENT_BINARY_DIR}/corrupt_report.csv"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(lab2_corrupt_samples_summary PROPERTIES
            PASS_REGULAR_EXPRESSION "не изображений: 1")
endif()