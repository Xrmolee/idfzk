# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "F:/Espressif/frameworks/esp-idf-v5.3/components/bootloader/subproject"
  "G:/idfzk/build/bootloader"
  "G:/idfzk/build/bootloader-prefix"
  "G:/idfzk/build/bootloader-prefix/tmp"
  "G:/idfzk/build/bootloader-prefix/src/bootloader-stamp"
  "G:/idfzk/build/bootloader-prefix/src"
  "G:/idfzk/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "G:/idfzk/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "G:/idfzk/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
