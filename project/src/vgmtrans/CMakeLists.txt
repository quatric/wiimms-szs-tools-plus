cmake_minimum_required(VERSION 3.5)
project(VGMTrans)

if(APPLE)
    execute_process(COMMAND uname -m OUTPUT_VARIABLE HOST_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(CMAKE_LIBRARY_ARCHITECTURE "${HOST_ARCH}-apple-darwin11")
    set(CMAKE_OSX_ARCHITECTURES "${HOST_ARCH}")
    set(GUI_TYPE MACOSX_BUNDLE)
endif(APPLE)

if(WIN32)
    set(CMAKE_LIBRARY_ARCHITECTURE "x86_64-w64-mingw32")
    set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
    set(GUI_TYPE WIN32)
endif(WIN32)

set ( CMAKE_CXX_FLAGS "-std=c++11 -w -g3 -Wall -O0" )

set(CMAKE_PREFIX_PATH
        ${CMAKE_PREFIX_PATH}
        "${CMAKE_SOURCE_DIR}/contrib/${CMAKE_LIBRARY_ARCHITECTURE}")
message(STATUS "PREFIX PATH: " ${CMAKE_PREFIX_PATH})

set(CMAKE_MODULE_PATH
        ${CMAKE_MODULE_PATH}
        ${CMAKE_SOURCE_DIR}/cmake)

file(GLOB_RECURSE MAIN_FILES "${PROJECT_SOURCE_DIR}/src/main/*.cpp" "${PROJECT_SOURCE_DIR}/src/main/*.h")
file(GLOB UI_FILES "${PROJECT_SOURCE_DIR}/src/ui/qt/*.cpp" "${PROJECT_SOURCE_DIR}/src/ui/qt/*.h")
file(GLOB_RECURSE LIB_FILES
        "${PROJECT_SOURCE_DIR}/lib/tinyxml/*.cpp"
        "${PROJECT_SOURCE_DIR}/lib/minizip/*.c" "${PROJECT_SOURCE_DIR}/lib/minizip/*.h"
        )

set(INCLUDE_DIRS
        ${PROJECT_SOURCE_DIR}/src/main
        ${PROJECT_SOURCE_DIR}/src/main/formats
        ${PROJECT_SOURCE_DIR}/src/main/loaders
        ${PROJECT_SOURCE_DIR}/src/ui/qt
        ${PROJECT_SOURCE_DIR}/contrib/${CMAKE_LIBRARY_ARCHITECTURE}/include
        ${PROJECT_SOURCE_DIR}/contrib/${CMAKE_LIBRARY_ARCHITECTURE}/include/glib-2.0
        ${PROJECT_SOURCE_DIR}/contrib/${CMAKE_LIBRARY_ARCHITECTURE}/include/fluidsynth
        ${PROJECT_SOURCE_DIR}/lib/zlib/include
        ${PROJECT_SOURCE_DIR}/lib/tinyxml
        ${PROJECT_SOURCE_DIR}/lib/fluidsynth/mem_sfloader

        )

set(RESOURCE_PATH
        "${PROJECT_SOURCE_DIR}/src/ui/qt/resources/resources.qrc"
        "${PROJECT_SOURCE_DIR}/src/ui/qt/resources/qdarkstyle/style.qrc")

source_group("ui" REGULAR_EXPRESSION .*src/ui/qt/.*)
source_group("main\\formats" REGULAR_EXPRESSION .*src/main/formats/.*)
source_group("main\\loaders" REGULAR_EXPRESSION .*src/main/loaders/.*)
source_group("main" REGULAR_EXPRESSION .*src/main/.*)
source_group("lib\\fluidsynth" REGULAR_EXPRESSION .*lib/fluidsynth/.*)
source_group("lib\\tinyxml" REGULAR_EXPRESSION .*lib/tinyxml/.*)
source_group("lib\\minizip" REGULAR_EXPRESSION .*lib/minizip/.*)

include_directories(${INCLUDE_DIRS})


# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)
# Removed Qt5 dependencies and GUI executable


find_package (ZLIB REQUIRED)
if (ZLIB_FOUND)
    include_directories(${ZLIB_INCLUDE_DIRS})
endif (ZLIB_FOUND)



find_package(GLIB REQUIRED)



file(GLOB CLI_FILES "${PROJECT_SOURCE_DIR}/src/ui/cli/*.cpp" "${PROJECT_SOURCE_DIR}/src/ui/cli/*.h")
add_executable(vgmtrans_cli ${MAIN_FILES} ${CLI_FILES} ${LIB_FILES})
target_link_libraries(vgmtrans_cli ${ZLIB_LIBRARIES} ${GLIB_LIBRARIES})
if (WIN32 OR APPLE)
    target_link_libraries(vgmtrans_cli ${QTPCRE_LIBRARY} ${INTL_LIBRARY} ${ICONV_LIBRARIES})
endif (WIN32 OR APPLE)

# Static core library (no main()) for direct, in-process linking into the
# wbrsar tool -- callers use vgmtrans_bridge.h, no subprocess is spawned.
list(FILTER CLI_FILES EXCLUDE REGEX ".*/src/ui/cli/main\\.cpp$")
add_library(vgmtrans_core STATIC ${MAIN_FILES} ${CLI_FILES} ${LIB_FILES})
target_link_libraries(vgmtrans_core ${ZLIB_LIBRARIES} ${GLIB_LIBRARIES})
if (WIN32 OR APPLE)
    target_link_libraries(vgmtrans_core ${QTPCRE_LIBRARY} ${INTL_LIBRARY} ${ICONV_LIBRARIES})
endif (WIN32 OR APPLE)


add_definitions(-DIOAPI_NO_64)
add_definitions(-DTIXML_USE_STL)

if (WIN32)
    add_definitions(-DFLUIDSYNTH_NOT_A_DLL -DG_OS_WIN32)
endif (WIN32)

if (APPLE)
    find_library(IOKIT_LIBRARY IOKit)
    find_library(APPKIT_LIBRARY AppKit)
    find_library(CARBON_LIBRARY Carbon)
    find_library(COREMIDI_LIBRARY CoreMIDI)
    find_library(COREAUDIO_LIBRARY CoreAudio)
    find_library(AUDIOUNIT_LIBRARY AudioUnit)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    find_library(CORESERVICES_LIBRARY CoreServices)

    target_link_libraries(vgmtrans_cli
            ${APPKIT_LIBRARY}
            ${CARBON_LIBRARY}
            ${COREMIDI_LIBRARY}
            ${COREAUDIO_LIBRARY}
            ${AUDIOUNIT_LIBRARY}
            ${COREFOUNDATION_LIBRARY}
            ${CORESERVICES_LIBRARY}
            ${IOKIT_LIBRARY}
            )
endif (APPLE)
