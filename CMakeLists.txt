cmake_minimum_required(VERSION 3.10)

project(HarmoniLAN)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Habilitar el compilador de meta-objetos (MOC), UI y recursos de Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
pkg_check_modules(OPUS REQUIRED opus)

# Buscar Qt (intenta Qt6 primero, luego Qt5)
find_package(QT NAMES Qt6 Qt5 COMPONENTS Core Gui Widgets Network REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Gui Widgets Network REQUIRED)

include_directories(
    include
    ${PORTAUDIO_INCLUDE_DIRS}
    ${OPUS_INCLUDE_DIRS}
)

add_executable(sender
    src/sender.cpp
)

add_executable(receiver
    src/receiver.cpp
)

add_executable(discovery
    src/discovery.cpp
)

add_executable(discover_client
    src/discover_client.cpp
)

# === AÑADIMOS EL EJECUTABLE DE LA INTERFAZ (GUI) ===
add_executable(HarmoniLAN_GUI
    gui/HarmoniLAN_GUI/main.cpp
    gui/HarmoniLAN_GUI/mainwindow.cpp
    gui/HarmoniLAN_GUI/mainwindow.h
    gui/HarmoniLAN_GUI/mainwindow.ui
)

target_link_libraries(sender
    ${PORTAUDIO_LIBRARIES}
    ${OPUS_LIBRARIES}
)

target_link_libraries(receiver
    ${PORTAUDIO_LIBRARIES}
    ${OPUS_LIBRARIES}
    Threads::Threads
)

# Enlazamos las librerías necesarias a la GUI
target_link_libraries(HarmoniLAN_GUI
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    ${PORTAUDIO_LIBRARIES}
    ${OPUS_LIBRARIES}
    Threads::Threads
)

#add_executable(harmonilan
#    src/main.cpp
#)

#target_link_libraries(harmonilan
#    ${PORTAUDIO_LIBRARIES}
#    ${OPUS_LIBRARIES}
#    Threads::Threads
#)