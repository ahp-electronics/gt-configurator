cmake_minimum_required(VERSION 2.8...3.20)

project(gt-configurator LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GT_CONFIGURATOR_VERSION_MAJOR 1)
set(GT_CONFIGURATOR_VERSION_MINOR 1)
set(GT_CONFIGURATOR_VERSION_RELEASE 1)
set(GT_CONFIGURATOR_VERSION "${GT_CONFIGURATOR_VERSION_MAJOR}.${GT_CONFIGURATOR_VERSION_MINOR}.${GT_CONFIGURATOR_VERSION_RELEASE}")

# QtCreator supports the following variables for Android, which are identical to qmake Android variables.
# Check http://doc.qt.io/qt-5/deployment-android.html for more information.
# They need to be set before the find_package(Qt5 ...) call.

#if(ANDROID)
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
#    if (ANDROID_ABI STREQUAL "armeabi-v7a")
#        set(ANDROID_EXTRA_LIBS
#            ${CMAKE_CURRENT_SOURCE_DIR}/path/to/libcrypto.so
#            ${CMAKE_CURRENT_SOURCE_DIR}/path/to/libssl.so)
#    endif()
#endif()
LIST(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")

find_package(Qt5 COMPONENTS Widgets SerialPort Network REQUIRED)
find_package(AHPGT REQUIRED)
find_package(DFU REQUIRED)
find_package(Threads REQUIRED)
find_package(M REQUIRED)

include_directories(${CMAKE_INCLUDE_PATH})
include_directories(${AHPGT_INCLUDE_DIR})
include_directories(${DFU_INCLUDE_DIR})

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/icon.ico ${CMAKE_CURRENT_BINARY_DIR}/icon.ico COPYONLY )
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h )
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/gt-configurator.iss.cmake ${CMAKE_CURRENT_BINARY_DIR}/gt-configurator.iss )

if(ANDROID)
    add_library(gt-configurator SHARED ${DFU_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/mainwindow.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/mainwindow.h
        ${CMAKE_CURRENT_SOURCE_DIR}/threads.h
        ${CMAKE_CURRENT_SOURCE_DIR}/mainwindow.ui
    )
    else(ANDROID)
    add_executable(gt-configurator ${DFU_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/mainwindow.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/mainwindow.h
        ${CMAKE_CURRENT_SOURCE_DIR}/mainwindow.ui
        ${CMAKE_CURRENT_SOURCE_DIR}/threads.h
        ${CMAKE_CURRENT_SOURCE_DIR}/resource.qrc
        ${CMAKE_CURRENT_SOURCE_DIR}/app.rc
    )
    if(WIN32)
        target_link_libraries(gt-configurator PRIVATE comctl32)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,--subsystem,windows")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,--subsystem,windows")
        set(CMAKE_LD_FLAGS "${CMAKE_LD_FLAGS} -Wl,--subsystem,windows -static-libstdc++ -static-libgcc")
        set_target_properties(gt-configurator PROPERTIES WIN32_EXECUTABLE TRUE)
    endif(WIN32)
endif(ANDROID)

target_link_libraries(gt-configurator PRIVATE ${DFU_LIBRARIES} ${AHPGT_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${M_LIB} Qt5::Widgets Qt5::SerialPort Qt5::Network)
install(TARGETS gt-configurator RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
install(FILES gt-configurator.png DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons)
install(FILES gt-configurator.desktop DESTINATION ${CMAKE_INSTALL_PREFIX}/share/applications)
