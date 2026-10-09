cmake_minimum_required(VERSION 3.5)
project(MultiopusSendSamples)

# Build sample_send_pcm_multiopus (raw PCM multi-channel sender)
file(GLOB SAMPLE_SEND_PCM_MULTIOPUS_CPP_FILES
     "${PROJECT_SOURCE_DIR}/sample_send_pcm_multiopus.cpp"
     "${PROJECT_SOURCE_DIR}/../common/*.cpp")
add_executable(sample_send_pcm_multiopus ${SAMPLE_SEND_PCM_MULTIOPUS_CPP_FILES})

# Build sample_send_wav_multiopus (multi-channel WAV sender)
file(GLOB SAMPLE_SEND_WAV_MULTIOPUS_CPP_FILES
     "${PROJECT_SOURCE_DIR}/sample_send_wav_multiopus.cpp"
     "${PROJECT_SOURCE_DIR}/../common/*.cpp")
add_executable(sample_send_wav_multiopus ${SAMPLE_SEND_WAV_MULTIOPUS_CPP_FILES})

# Build sample_send_opus8 (pre-encoded 8ch Opus raw stream sender)
file(GLOB SAMPLE_SEND_OPUS8_CPP_FILES
     "${PROJECT_SOURCE_DIR}/sample_send_opus8.cpp"
     "${PROJECT_SOURCE_DIR}/../common/*.cpp")
add_executable(sample_send_opus8 ${SAMPLE_SEND_OPUS8_CPP_FILES})

# Build sample_send_mkv_multiopus (MKV sender: 4xstereo AC-3 -> 8ch audio + mpeg4 -> I420 video)
# 仅该 target 依赖 ffmpeg（libavformat/libavcodec/libavutil/libswresample/libswscale）
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
                  libavformat libavcodec libavutil libswresample libswscale)
file(GLOB SAMPLE_SEND_MKV_MULTIOPUS_CPP_FILES
     "${PROJECT_SOURCE_DIR}/sample_send_mkv_multiopus.cpp"
     "${PROJECT_SOURCE_DIR}/../common/*.cpp")
add_executable(sample_send_mkv_multiopus ${SAMPLE_SEND_MKV_MULTIOPUS_CPP_FILES})
target_link_libraries(sample_send_mkv_multiopus PkgConfig::LIBAV)
