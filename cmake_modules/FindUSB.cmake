# - Try to find USB
# Once done this will define
#
#  USB_FOUND - system has USB
#  USB_INCLUDE_DIR - the USB include directory
#  USB_LIBRARIES - Link these to use USB
#  USB_VERSION_STRING - Human readable version number of cfitsio
#  USB_VERSION_MAJOR  - Major version number of cfitsio
#  USB_VERSION_MINOR  - Minor version number of cfitsio

# Copyright (c) 2006, Jasem Mutlaq <mutlaqja@ikarustech.com>
# Based on FindLibfacile by Carsten Niehaus, <cniehaus@gmx.de>
#
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.

if (USB_INCLUDE_DIR AND USB_LIBRARIES)

  # in cache already
  set(USB_FOUND TRUE)
  message(STATUS "Found USB: ${USB_LIBRARIES}")


else (USB_INCLUDE_DIR AND USB_LIBRARIES)

  # JM: Packages from different distributions have different suffixes
  find_path(USB_INCLUDE_DIR libusb.h
    PATH_SUFFIXES libusb-1.0
    ${_obIncDir}
    ${GNUWIN32_DIR}/include
  )

  find_library(USB_LIBRARIES NAMES usb-1.0
    PATHS
    ${_obLinkDir}
    ${GNUWIN32_DIR}/lib
    HINTS ${CMAKE_C_IMPLICIT_LINK_DIRECTORIES}
  )

  if(USB_INCLUDE_DIR AND USB_LIBRARIES)
    set(USB_FOUND TRUE)
  else (USB_INCLUDE_DIR AND USB_LIBRARIES)
    set(USB_FOUND FALSE)
  endif(USB_INCLUDE_DIR AND USB_LIBRARIES)


  if (USB_FOUND)

    if (NOT USB_FIND_QUIETLY)
      message(STATUS "Found USB ${USB_VERSION_STRING}: ${USB_LIBRARIES}")
    endif (NOT USB_FIND_QUIETLY)
  else (USB_FOUND)
    if (USB_FIND_REQUIRED)
      message(STATUS "USB not found.")
    endif (USB_FIND_REQUIRED)
  endif (USB_FOUND)

  mark_as_advanced(USB_INCLUDE_DIR USB_LIBRARIES)

endif (USB_INCLUDE_DIR AND USB_LIBRARIES)
