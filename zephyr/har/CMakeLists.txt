cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(har)

# Locate eml_net.h directly in the container venv or local paths
find_path(EMLEARN_INCLUDE_DIR eml_net.h
    PATHS
        /opt/venv/lib/python3.12/site-packages/emlearn
        /opt/venv/lib/python3.11/site-packages/emlearn
        /usr/local/lib/python3.12/dist-packages/emlearn
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    REQUIRED
)

target_include_directories(app PRIVATE
    ${EMLEARN_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_sources(app PRIVATE src/main.cpp)
