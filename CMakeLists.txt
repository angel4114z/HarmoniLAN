cmake_minimum_required(VERSION 3.10)

project(HarmoniLAN)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)

pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
pkg_check_modules(OPUS REQUIRED opus)

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

target_link_libraries(sender
    ${PORTAUDIO_LIBRARIES}
)

target_link_libraries(receiver
    ${PORTAUDIO_LIBRARIES}
)

#add_executable(harmonilan
#    src/main.cpp
#)

#target_link_libraries(harmonilan
#    ${PORTAUDIO_LIBRARIES}
#    ${OPUS_LIBRARIES}
#)