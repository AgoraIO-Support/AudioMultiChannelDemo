cmake_minimum_required(VERSION 3.5)
project(AgoraAudioMultiChannelDemo)

# SDK 路径：通过 -DAGORA_SDK_DIR=<path>/agora_rtc_sdk 指定，
# 例如 -DAGORA_SDK_DIR=../../LinuxSDKFolder/X86_64/agora_rtc_sdk
if(NOT AGORA_SDK_DIR)
  set(AGORA_SDK_DIR "${CMAKE_SOURCE_DIR}/../../LinuxSDKFolder/X86_64/agora_rtc_sdk")
endif()
get_filename_component(AGORA_SDK_DIR "${AGORA_SDK_DIR}" ABSOLUTE
                       BASE_DIR "${CMAKE_SOURCE_DIR}")
set(AGORA_SDK_LIB_DIR "${AGORA_SDK_DIR}/agora_sdk")
set(AGORA_SDK_HEADER_DIR "${AGORA_SDK_DIR}/agora_sdk/include")

if(NOT EXISTS "${AGORA_SDK_LIB_DIR}/libagora_rtc_sdk.so")
  message(FATAL_ERROR
          "libagora_rtc_sdk.so not found in ${AGORA_SDK_LIB_DIR}. "
          "Please set -DAGORA_SDK_DIR=<path>/agora_rtc_sdk correctly "
          "(LinuxSDKFolder/X86_64 or LinuxSDKFolder/Aarch64).")
endif()

message(STATUS "AGORA_SDK_DIR: ${AGORA_SDK_DIR}")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer -Wno-unused-command-line-argument")

# include header path
include_directories("${CMAKE_SOURCE_DIR}" "${AGORA_SDK_HEADER_DIR}")

# Link SDK libs（与 SDK example/scripts/env.cmake 做法一致：
# 链接 agora_rtc_sdk + 系统库，libagora-fdkaac.so / libaosl.so 通过 rpath 在运行时加载）
link_directories(${AGORA_SDK_LIB_DIR})
link_libraries(agora_rtc_sdk pthread dl rt m)

set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} -Wl,-rpath=.:${AGORA_SDK_LIB_DIR}")

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/out/)

add_subdirectory(multiopus)
add_subdirectory(receive)
add_subdirectory(tools)
