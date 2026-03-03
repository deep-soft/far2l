# libusb-based MTP/PTP plugin module.

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

set(SOURCES
    src/FARPlugin.cpp
    src/MTPPlugin.cpp
    src/MTPLog.cpp
    src/usb/UsbEnumerator_mac.cpp
    src/usb/UsbTransport.cpp
    src/ptp/PtpSession.cpp
    src/mtp/MtpSession.cpp
    src/mtp/MtpDeviceModel.cpp
    src/mtp/DirectoryCache.cpp
    src/mtp/QuirkRegistry.cpp
    src/mtp/TransferManager.cpp
    src/backend/MtpBackend.cpp
    src/backend/PtpBackend.cpp
    src/backend/BackendRouter.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(REMOVE_ITEM SOURCES src/usb/UsbEnumerator_mac.cpp)
    list(APPEND SOURCES src/usb/UsbEnumerator_linux.cpp)
endif()

add_library(mtp MODULE ${SOURCES})

target_link_libraries(mtp utils far2l)

target_compile_definitions(mtp PRIVATE
    -DWINPORT_DIRECT
    -DUNICODE
    -D_UNICODE
    -DFAR_DONT_USE_INTERNALS
)

target_include_directories(mtp PRIVATE .)
target_include_directories(mtp PRIVATE ../far2l/far2sdk)
target_include_directories(mtp PRIVATE ../WinPort)
target_include_directories(mtp PRIVATE ${LIBUSB_INCLUDE_DIRS})

target_link_libraries(mtp ${LIBUSB_LIBRARIES})
target_link_directories(mtp PRIVATE ${LIBUSB_LIBRARY_DIRS})

set_target_properties(mtp PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${INSTALL_DIR}/Plugins/mtp/plug"
    PREFIX ""
    SUFFIX ".far-plug-wide"
)

set(CURRENT_TARGET "mtp")
file(GLOB_RECURSE RESOURCE_FILES
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/configs
    "${CMAKE_CURRENT_SOURCE_DIR}/configs/*"
)
