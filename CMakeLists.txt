cmake_minimum_required(VERSION 3.5)

project(gt-configurator LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

find_package(Qt5 COMPONENTS Widgets SerialPort REQUIRED)
find_package(AHPGT REQUIRED)
find_package(Threads REQUIRED)

include_directories(${AHPGT_INCLUDE_DIR})

if(ANDROID)
  add_library(gt-configurator SHARED
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
  )
else(ANDROID)
  add_executable(gt-configurator
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    resource.qrc
  )
  if(WIN32)
    add_definitions(-D _win32_IE=0x0500 -D WINVER=0x0500)
    set_target_properties(gt-configurator PROPERTIES WIN32_EXECUTABLE TRUE)
  endif(WIN32)
endif(ANDROID)

target_link_libraries(gt-configurator PRIVATE ${AHPGT_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} Qt5::Widgets Qt5::SerialPort)
install(TARGETS gt-configurator RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
install(FILES gt-configurator.png DESTINATION /usr/share/icons)
install(FILES gt-configurator.desktop DESTINATION /usr/share/applications)
