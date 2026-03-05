project(preview)

# Added: Find PkgConfig module to use pkg-config for finding libraries
find_package(PkgConfig REQUIRED)

if(APPLE)
    option(PREVIEW_NATIVE "Use native macOS image decoding and processing" ON)
else()
    set(PREVIEW_NATIVE OFF)
endif()

set(SOURCES
    src/Plugin.cpp
    src/Settings.cpp
    src/Image.cpp
    src/ImageView.cpp
    src/ImageAtFull.cpp
    src/ImageAtQV.cpp
    src/PreviewLog.cpp
    src/decoder/ImageDecoder.cpp
    src/decoder/ImageDecoder_common.cpp
)

# Initialize library lists for both branches
set(PREVIEW_NATIVE_LIBS "")
set(PREVIEW_CROSSPLATFORM_LIBS "")

if(APPLE)
    list(APPEND SOURCES src/decoder/ImageDecoder_mac.cpp)
    find_library(IMAGEIO ImageIO REQUIRED)
    find_library(COREGRAPHICS CoreGraphics REQUIRED)
    find_library(COREFOUNDATION CoreFoundation REQUIRED)
    find_library(ACCELERATE Accelerate REQUIRED)
    set(PREVIEW_NATIVE_LIBS ${IMAGEIO} ${COREGRAPHICS} ${COREFOUNDATION} ${ACCELERATE})
endif()

# Always include cross-platform decoders
list(APPEND SOURCES src/decoder/ImageDecoder_stb.cpp)
# Include the new HEIF decoder source file for compilation
list(APPEND SOURCES src/decoder/ImageDecoder_heif.cpp) 
# Include the WebP decoder source file
list(APPEND SOURCES src/decoder/ImageDecoder_webp.cpp)

# --- Integrate WebP and HEIC support using PkgConfig ---
# Check for libwebp
pkg_check_modules(WEBP REQUIRED libwebp)
# Check for libheif
pkg_check_modules(HEIF REQUIRED libheif)

# Always include include directories BEFORE add_library
include_directories(
    ${WEBP_INCLUDE_DIRS}
    ${HEIF_INCLUDE_DIRS}
    .
    ../far2l/far2sdk
    ../WinPort
)

# Use full paths to libraries if possible
foreach(lib_name ${WEBP_LIBRARIES})
    find_library(LIB_WEBP_${lib_name} NAMES ${lib_name} HINTS ${WEBP_LIBRARY_DIRS})
    list(APPEND PREVIEW_CROSSPLATFORM_LIBS ${LIB_WEBP_${lib_name}})
endforeach()

foreach(lib_name ${HEIF_LIBRARIES})
    find_library(LIB_HEIF_${lib_name} NAMES ${lib_name} HINTS ${HEIF_LIBRARY_DIRS})
    list(APPEND PREVIEW_CROSSPLATFORM_LIBS ${LIB_HEIF_${lib_name}})
endforeach()
# --- End Integration ---

add_library(preview MODULE ${SOURCES})

# Link all libraries found for the active branch
target_link_libraries(preview utils far2l ${PREVIEW_NATIVE_LIBS} ${PREVIEW_CROSSPLATFORM_LIBS})

target_compile_definitions(preview PRIVATE
    -DWINPORT_DIRECT
    -DUNICODE
    -D_UNICODE
)

set_target_properties(preview PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${INSTALL_DIR}/Plugins/preview/plug"
    PREFIX ""
    SUFFIX ".far-plug-wide"
)

# Copy resource files (config and help)
set(CURRENT_TARGET "preview")
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
