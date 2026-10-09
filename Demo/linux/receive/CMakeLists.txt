cmake_minimum_required(VERSION 3.5)
project(MultiChannelReceiveSamples)

# Build sample_receive_pcm_multiopus (decoded PCM per-user callback)
file(GLOB SAMPLE_RECEIVE_PCM_MULTIOPUS_CPP_FILES
     "${PROJECT_SOURCE_DIR}/sample_receive_pcm_multiopus.cpp"
     "${PROJECT_SOURCE_DIR}/../common/*.cpp")
add_executable(sample_receive_pcm_multiopus ${SAMPLE_RECEIVE_PCM_MULTIOPUS_CPP_FILES})

# Build sample_receive_encoded_audio (encoded audio frame callback)
file(GLOB SAMPLE_RECEIVE_ENCODED_AUDIO_CPP_FILES
     "${PROJECT_SOURCE_DIR}/sample_receive_encoded_audio.cpp"
     "${PROJECT_SOURCE_DIR}/../common/*.cpp")
add_executable(sample_receive_encoded_audio ${SAMPLE_RECEIVE_ENCODED_AUDIO_CPP_FILES})

# Build sample_receive_mixed_audio (mixed audio output, always 2 channels)
file(GLOB SAMPLE_RECEIVE_MIXED_AUDIO_CPP_FILES
     "${PROJECT_SOURCE_DIR}/sample_receive_mixed_audio.cpp"
     "${PROJECT_SOURCE_DIR}/../common/*.cpp")
add_executable(sample_receive_mixed_audio ${SAMPLE_RECEIVE_MIXED_AUDIO_CPP_FILES})

# Build sample_receive_av_multiopus (multi-channel audio PCM + I420 video receive & dump)
# 接收侧由 SDK 解码，无需 ffmpeg
file(GLOB SAMPLE_RECEIVE_AV_MULTIOPUS_CPP_FILES
     "${PROJECT_SOURCE_DIR}/sample_receive_av_multiopus.cpp"
     "${PROJECT_SOURCE_DIR}/../common/*.cpp")
add_executable(sample_receive_av_multiopus ${SAMPLE_RECEIVE_AV_MULTIOPUS_CPP_FILES})
