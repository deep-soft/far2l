project(img)

set(SOURCES
    src/Plugin.cpp
    src/Settings.cpp
    src/Image.cpp
    src/ImageView.cpp
    src/ImageAtFull.cpp
    src/ImageAtQV.cpp
    src/ImgLog.cpp
    src/decoder/ImageDecoder.cpp
    src/decoder/ImageDecoder_common.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND SOURCES src/decoder/ImageDecoder_linux.cpp)
    message(WARNING "IMG plugin Linux decoder backend is not implemented yet. Building a placeholder plugin with no Linux decoders.")

elseif(APPLE)
    list(APPEND SOURCES src/decoder/ImageDecoder_mac.cpp)
    find_library(IMAGEIO ImageIO REQUIRED)
    find_library(COREGRAPHICS CoreGraphics REQUIRED)
    find_library(COREFOUNDATION CoreFoundation REQUIRED)
    find_library(ACCELERATE Accelerate REQUIRED)
    set(LINK_LIBS ${IMAGEIO} ${COREGRAPHICS} ${COREFOUNDATION} ${ACCELERATE})
endif()

add_library(img MODULE ${SOURCES})

target_include_directories(img PRIVATE
    .
    ../far2l/far2sdk
    ../WinPort
    ${INCLUDE_DIRS}
)

target_link_libraries(img utils far2l ${LINK_LIBS})

target_compile_definitions(img PRIVATE
    -DWINPORT_DIRECT
    -DUNICODE
    -D_UNICODE
    ${EXTRA_DEFINES}
)

set_target_properties(img PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${INSTALL_DIR}/Plugins/img/plug"
    PREFIX ""
    SUFFIX ".far-plug-wide"
)

# Copy resource files (config and help)
set(CURRENT_TARGET "img")
file(GLOB_RECURSE RESOURCE_FILES
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/configs
    "${CMAKE_CURRENT_SOURCE_DIR}/configs/*"
)
setup_target_auxiliary_files(${CURRENT_TARGET}
    RESOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/configs
    "${INSTALL_DIR}/Plugins/${CURRENT_TARGET}/"
)
add_dependencies(${CURRENT_TARGET} copy_aux_files_for_${CURRENT_TARGET})
