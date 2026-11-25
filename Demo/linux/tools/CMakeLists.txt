cmake_minimum_required(VERSION 3.5)
project(MultiChannelAnalysisTools)

# pcm_channel_analyzer has no Agora runtime dependency. It is built with the
# demo so captured receiver PCM can be checked using the same source tree.
add_executable(pcm_channel_analyzer
               pcm_channel_analyzer.cpp
               "${CMAKE_CURRENT_SOURCE_DIR}/../common/multichannel_input_validation.cpp"
               "${CMAKE_CURRENT_SOURCE_DIR}/../common/pcm_channel_analyzer.cpp")

file(GLOB AUDIO_TEARDOWN_RACE_CPP_FILES
     "${CMAKE_CURRENT_SOURCE_DIR}/../tests/audio_teardown_race.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/../common/*.cpp")
add_executable(audio_teardown_race ${AUDIO_TEARDOWN_RACE_CPP_FILES})
