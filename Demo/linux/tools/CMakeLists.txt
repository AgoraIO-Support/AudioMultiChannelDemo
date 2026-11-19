cmake_minimum_required(VERSION 3.5)
project(MultiChannelAnalysisTools)

# This target has no Agora runtime dependency. It is built with the demo so a
# captured receiver PCM file can be checked using the same source tree as the
# sender and receiver samples.
add_executable(pcm_channel_analyzer
               pcm_channel_analyzer.cpp
               "${CMAKE_CURRENT_SOURCE_DIR}/../common/multichannel_input_validation.cpp"
               "${CMAKE_CURRENT_SOURCE_DIR}/../common/pcm_channel_analyzer.cpp")
