cmake_minimum_required(VERSION 3.4.1)

add_library(mp3fy SHARED lib.cpp)

find_library(log-lib log)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(mp3fy
        ${log-lib}
        ${CMAKE_CURRENT_SOURCE_DIR}/../jni/${ANDROID_ABI}/libavformat.so
        ${CMAKE_CURRENT_SOURCE_DIR}/../jni/${ANDROID_ABI}/libavcodec.so
        ${CMAKE_CURRENT_SOURCE_DIR}/../jni/${ANDROID_ABI}/libavutil.so)