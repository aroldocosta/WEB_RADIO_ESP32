cmake_minimum_required(VERSION 3.16)

# Inclusão do ESP-ADF caso a variável de ambiente ADF_PATH esteja definida
if(DEFINED ENV{ADF_PATH})
    include($ENV{ADF_PATH}/CMakeLists.txt)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(web_radio_esp32)
