cmake_minimum_required(VERSION 2.4)
project(DefaultSamples)

# Common file parsers
file(GLOB FILE_PARSER_CPP_FILES
     "${PROJECT_SOURCE_DIR}/../common/file_parser/helper_h264_parser.cpp")

# Build sample_send_h264_pcm_multiopus
file(GLOB SAMPLE_SEND_H264_PCM_MULTI_OPUS_CPP_FILES
     "${PROJECT_SOURCE_DIR}/sample_send_h264_pcm_multiopus.cpp"
     "${PROJECT_SOURCE_DIR}/../common/*.cpp")
add_executable(sample_send_h264_pcm_multiopus ${SAMPLE_SEND_H264_PCM_MULTI_OPUS_CPP_FILES}
                                               ${FILE_PARSER_CPP_FILES})


# Build sample_send_h264_pcm_multiopus_8ch_wav
file(GLOB SAMPLE_SEND_H264_PCM_MULTI_OPUS_8CH_WAV_CPP_FILES
     "${PROJECT_SOURCE_DIR}/sample_send_h264_pcm_multiopus_8ch_wav.cpp"
     "${PROJECT_SOURCE_DIR}/../common/*.cpp")
add_executable(sample_send_h264_pcm_multiopus_8ch_wav ${SAMPLE_SEND_H264_PCM_MULTI_OPUS_8CH_WAV_CPP_FILES}
                                                       ${FILE_PARSER_CPP_FILES})



# New sample_send_opus8
file(GLOB SAMPLE_SEND_OPUS8_CPP_FILES
     "${PROJECT_SOURCE_DIR}/sample_send_opus8.cpp"
     "${PROJECT_SOURCE_DIR}/../common/*.cpp")

add_executable(sample_send_opus8 ${SAMPLE_SEND_OPUS8_CPP_FILES}
                                    ${FILE_PARSER_CPP_FILES})

# 如果需要的话，可以添加其他目标，比如 sample_receive_h264_pcm_multiopus
# file(GLOB SAMPLE_RECEIVE_H264_PCM_MULTI_OPUS_CPP_FILES
#      "${PROJECT_SOURCE_DIR}/sample_receive_h264_pcm_multiopus.cpp"
#      "${PROJECT_SOURCE_DIR}/../common/*.cpp")
# add_executable(sample_receive_h264_pcm_multiopus ${SAMPLE_RECEIVE_H264_PCM_MULTI_OPUS_CPP_FILES})

