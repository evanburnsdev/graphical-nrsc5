cmake_minimum_required(VERSION 3.1.0)

project(graphical-nrsc5 VERSION 0.1 LANGUAGES CXX)

add_definitions (-D_GNU_SOURCE)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 11)

add_custom_target(xml_copy ALL)
add_custom_command(
    TARGET xml_copy PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/src/gtk.xml
	    ${CMAKE_CURRENT_BINARY_DIR}/gtk.xml)

find_package(Qt5 COMPONENTS Core)
find_package(PkgConfig REQUIRED)
PKG_CHECK_MODULES(GTKMM REQUIRED gtkmm-3.0)

INCLUDE_DIRECTORIES(
    ${GTKMM_INCLUDE_DIRS}
    ./src/classes
    )

file(GLOB HEADERS "src/classes/*.h")
file(GLOB SOURCES "src/classes/*.cpp")

#add_custom_target(src SOURCES ${SOURCES})
#add_custom_target(headers SOURCES ${HEADERS})

ADD_DEFINITIONS(${GTKMM_CFLAGS_OTHER})

add_library(local SHARED ${HEADERS} ${SOURCES})

set( project_sources
    src/main.cpp
    )

add_executable(${PROJECT_NAME}
    ${project_sources}
    )

target_link_libraries(${PROJECT_NAME}
    ${GTKMM_LIBRARIES}
    pthread
    fftw3f
    rtlsdr
    m
    ao
    nrsc5
    local
    )
