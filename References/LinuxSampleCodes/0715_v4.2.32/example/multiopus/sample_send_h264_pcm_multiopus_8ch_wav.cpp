//  Agora RTC/MEDIA SDK
//
//  Created by Jay Zhang in 2020-04.
//  Copyright (c) 2020 Agora.io. All rights reserved.
//
 
 
//video
//   ******************         --------         *************         ------------       *********        **********         ************       ------------        ***********       --------
//  {SDK::capture video}  ==>  |raw data|  ==>  {SDK::encode B}  ==>  |encoded data| ==> {SDK::send}  ==> {AGORA::VOS}  ==>  {SDK::receive} ==> |encoded data|  ==> {SDK::decode} ==> |raw data|
//   ******************         --------         *************         ------------       *********        **********         ************       ------------        ***********       --------
//                                                                                  sample send h264(this sample)                              sample receive h264                
 
//This sample will show how to use the SDK to send the encoded_Video to the Agora_channel
//As a user,you should papera the encoded_Video and create the Agora_service,Agora_connection, Agora_EncodedImage_sender and Agora_local_video_track
//You should parse the encoded_Video and send to sdk one frame by one frame.(by use the AGORA_API: Sender->sendEncodedVideoImage())
//The class HelperH264FileParser is a common_helper class to parse the h264 video
//And all Agora necessary Classes builded in main() function
//Last, the sendVideoThread call Sender->sendEncodedVideoImage() to send the encoded videoFrame
 
//The sdk also provide lots of call_back functions to help user get the network states , local states and  peer states
//The callback functions can be found in **observer , you should register the observer first.
 
// the SDKapi call flows:
// service = createAndInitAgoraService()
//     connection = service->createRtcConnection()
//         connection->registerObserver()
//             connection->connect()
//         factory = service->createMediaNodeFactory()
//             VideoSender = factory->createVideoEncodedImageSender();
//                 VideoTrack = service->createCustomVideoTrack()
//      connection->getLocalUser()->publishAudio(VideoTrack);
//                 Sender->sendEncodedVideoImage() 
 
 
 
//audio
//   ******************         --------         *************         ------------       *********        **********         ************       ------------        ***********       --------
//  {SDK::capture audio}  ==>  |raw data|  ==>  {SDK::encode B}  ==>  |encoded data| ==> {SDK::send}  ==> {AGORA::VOS}  ==>  {SDK::receive} ==> |encoded data|  ==> {SDK::decode} ==> |raw data|
//   ******************         --------         *************         ------------       *********        **********         ************       ------------        ***********       --------
//                        sample send pcm(this sample)                sample send opus                                                                                            sample receive pcm
 
//This sample will show how to use the SDK to send the raw AudioData(pcm) to the Agora_channel
//As a user,you should papera the audio and create the Agora_service,Agora_connection, Agora_audio_pcm_sender and Agora_local_audio_track
//You should parse the audio and send to sdk one frame by one frame.(by use the AGORA_API: Sender->sendAudioPcmData())
//And all Agora necessary Classes builded in main() function
//Last, the sendAudioThread call Sender->sendAudioPcmData() to send the audioFrame
 
//The sdk also provide lots of call_back functions to help user get the network states , local states and  peer states
//The callback functions can be found in **observer , you should register the observer first.
 
 
// the SDKapi call flows:
// service = createAndInitAgoraService()
//     connection = service->createRtcConnection()
//         connection->registerObserver()
//         connection->connect()
//         factory = service->createMediaNodeFactory()
//             AudioSender = factory->createAudioPcmDataSender();
//                 AudioTrack = service->createCustomAudioTrack();
//      connection->getLocalUser()->publishAudio(audioTrack);
//                 Sender->sendAudioPcmData() 
 
//The destruct order of all the class can be find in the main function end.
 
// Wish you have a great experience with Agora_SDK!
 
 
#include <csignal>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>

#include <iostream>
#include <cstdint>
#include <cstdio>
#include <cerrno>

#include <cmath>

 
#include "IAgoraService.h"
#include "NGIAgoraRtcConnection.h"
#include "common/file_parser/helper_h264_parser.h"
#include "common/helper.h"
#include "common/log.h"
#include "common/opt_parser.h"
#include "common/sample_common.h"
#include "common/sample_connection_observer.h"
#include "common/sample_local_user_observer.h"
 
#include "NGIAgoraAudioTrack.h"
#include "NGIAgoraLocalUser.h"
#include "NGIAgoraMediaNodeFactory.h"
#include "NGIAgoraMediaNode.h"
#include "NGIAgoraVideoTrack.h"
 
#define DEFAULT_CONNECT_TIMEOUT_MS (3000)
#define DEFAULT_SAMPLE_RATE (16000)
#define DEFAULT_NUM_OF_CHANNELS (1)
#define DEFAULT_FRAME_RATE (30)
#define DEFAULT_AUDIO_FILE "test_data/send_audio_16k_1ch.pcm"
#define DEFAULT_VIDEO_FILE "test_data/send_video.h264"
 
struct SampleOptions {
  std::string appId;
  std::string channelId;
  std::string userId;
  std::string audioFile = DEFAULT_AUDIO_FILE;
  std::string videoFile = DEFAULT_VIDEO_FILE;
  std::string localIP;
  int customChannelNum = 0;  // 自定义频道数量
  int customPayloadType = 0;  // 自定义负载类型
 
  struct {
    int sampleRate = DEFAULT_SAMPLE_RATE;
    int numOfChannels = DEFAULT_NUM_OF_CHANNELS;
  } audio;
  struct {
    int frameRate = DEFAULT_FRAME_RATE;
    bool showBandwidthEstimation = false;
  } video;
};


// WAV 文件头结构
struct WavHeader {
    char riff[4];                // "RIFF"
    uint32_t overall_size;       // 文件总大小
    char wave[4];                // "WAVE"
    char fmt_chunk_marker[4];    // "fmt "
    uint32_t length_of_fmt;      // fmt 的长度（通常为 16）
    uint16_t format_type;        // 格式类型（通常为 1，表示 PCM）
    uint16_t channels;           // 声道数（例如 8）
    uint32_t sample_rate;        // 采样率（例如 48000）
    uint32_t byterate;           // 每秒字节数
    uint16_t block_align;        // 每个采样的字节数
    uint16_t bits_per_sample;    // 每个样本的位数（例如 24）
    char data_chunk_header[4];   // "data"
    uint32_t data_size;          // 音频数据的大小
};

 
// 更新 sendOnePcmFrame 函数
static void sendOnePcmFrame(const SampleOptions& options, 
                            agora::agora_refptr<agora::rtc::IAudioPcmDataSender> audioFrameSender) {
    static FILE* file = nullptr;
    const char* fileName = options.audioFile.c_str();
    static WavHeader header;
    static bool headerRead = false;

    // 读取 WAV 文件头 (只读取一次)
    if (!file) {
        if (!(file = fopen(fileName, "rb"))) {
            AG_LOG(ERROR, "Failed to open audio file %s", fileName);
            return;
        }

        if (fread(&header, sizeof(WavHeader), 1, file) != 1) {
            AG_LOG(ERROR, "Failed to read WAV header: %s", std::strerror(errno));
            fclose(file);
            file = nullptr;
            return;
        }

        headerRead = true;

        AG_LOG(INFO, "Opened WAV file: %s", fileName);
        AG_LOG(INFO, "Sample Rate: %d, Channels: %d, Bits Per Sample: %d", 
                header.sample_rate, header.channels, header.bits_per_sample);
        
        // 验证支持的格式
        if (header.bits_per_sample != 16 && header.bits_per_sample != 24) {
            AG_LOG(ERROR, "Unsupported bit depth: %d. Only 16-bit and 24-bit are supported.", 
                   header.bits_per_sample);
            fclose(file);
            file = nullptr;
            return;
        }
    }

    // 24-bit to 16-bit 转换函数
    auto convert24to16 = [](const uint8_t* sample24) -> int16_t {
        // 读取24位小端序样本
        int32_t sample32 = sample24[0] | (sample24[1] << 8) | (sample24[2] << 16);
        
        // 符号扩展负值
        if (sample32 & 0x800000) {
            sample32 |= 0xFF000000;
        }
        
        // 转换到16位范围 (右移8位)
        return static_cast<int16_t>(sample32 >> 8);
    };

    // 计算原始文件每帧的字节大小
    int originalSampleSize = header.bits_per_sample / 8; // 24位=3字节, 16位=2字节
    int samplesPerFrame = header.sample_rate / 100; // 每10ms的样本数
    int originalFrameBytes = originalSampleSize * header.channels * samplesPerFrame;
    
    // 输出总是16位
    int outputFrameBytes = 2 * header.channels * samplesPerFrame; // 16位 = 2字节
    
    // 分配输入和输出缓冲区
    // 使用 uint8_t* 类型的 outputBuf
    uint8_t* inputBuf = new uint8_t[originalFrameBytes]; 
    uint8_t* outputBuf = new uint8_t[outputFrameBytes]; // **变更的代码**

    // 读取原始音频数据
    if (fread(inputBuf, 1, originalFrameBytes, file) != originalFrameBytes) {
        if (feof(file)) {
            fclose(file);
            file = nullptr;
            headerRead = false;
            AG_LOG(INFO, "End of audio file");
        } else {
            AG_LOG(ERROR, "Error reading audio data: %s", std::strerror(errno));
        }
        delete[] inputBuf;
        delete[] outputBuf;
        return;
    }

    // 根据位深度处理音频数据
    if (header.bits_per_sample == 24) {
        // 24位转16位
        int totalSamples = header.channels * samplesPerFrame;
        for (int i = 0; i < totalSamples; i++) {
            int16_t sample16 = convert24to16(&inputBuf[i * 3]);
            
            // 将 16 位样本写入 uint8_t* outputBuf
            outputBuf[i * 2] = static_cast<uint8_t>(sample16 & 0xFF);         // **变更的代码**
            outputBuf[i * 2 + 1] = static_cast<uint8_t>((sample16 >> 8) & 0xFF); // **变更的代码**
        }
    } else if (header.bits_per_sample == 16) {
        // 16位直接复制
        memcpy(outputBuf, inputBuf, outputFrameBytes);
    }

    // 发送16位音频数据到Agora
    if (audioFrameSender->sendAudioPcmData(
            outputBuf, // 转换为uint8_t*
            0, 0, 
            samplesPerFrame,
            agora::rtc::TWO_BYTES_PER_SAMPLE, // 总是16位
            header.channels,
            header.sample_rate) < 0) {
        AG_LOG(ERROR, "Failed to send audio frame!");
    }

    // 释放内存
    delete[] inputBuf;
    delete[] outputBuf;
}


 
static void sendOneH264Frame(
    int frameRate, std::unique_ptr<HelperH264Frame> h264Frame,
    agora::agora_refptr<agora::rtc::IVideoEncodedImageSender> videoH264FrameSender) {
  agora::rtc::EncodedVideoFrameInfo videoEncodedFrameInfo;
  videoEncodedFrameInfo.rotation = agora::rtc::VIDEO_ORIENTATION_0;
  videoEncodedFrameInfo.codecType = agora::rtc::VIDEO_CODEC_H264;
  videoEncodedFrameInfo.framesPerSecond = frameRate;
  videoEncodedFrameInfo.frameType =
      (h264Frame.get()->isKeyFrame ? agora::rtc::VIDEO_FRAME_TYPE::VIDEO_FRAME_TYPE_KEY_FRAME
                                   : agora::rtc::VIDEO_FRAME_TYPE::VIDEO_FRAME_TYPE_DELTA_FRAME);
 
  /*   AG_LOG(DEBUG, "sendEncodedVideoImage, buffer %p, len %d, frameType %d",
           reinterpret_cast<uint8_t*>(h264Frame.get()->buffer.get()), h264Frame.get()->bufferLen,
           videoEncodedFrameInfo.frameType); */
 
  videoH264FrameSender->sendEncodedVideoImage(
      reinterpret_cast<uint8_t*>(h264Frame.get()->buffer.get()), h264Frame.get()->bufferLen,
      videoEncodedFrameInfo);
}
 
static void SampleSendAudioTask(
    SampleOptions& options,
    agora::agora_refptr<agora::rtc::IAudioPcmDataSender> audioFrameSender, bool& exitFlag) {
  // Currently only 10 ms PCM frame is supported. So PCM frames are sent at 10 ms interval
  PacerInfo pacer = {0, 10,0, std::chrono::steady_clock::now()};
 
  while (!exitFlag) {
    sendOnePcmFrame(options, audioFrameSender);
    waitBeforeNextSend(pacer);  // sleep for a while before sending next frame
  }
}
 
static void SampleSendVideoH264Task(
    const SampleOptions& options,
    agora::agora_refptr<agora::rtc::IVideoEncodedImageSender> videoH264FrameSender,
    bool& exitFlag) {
  std::unique_ptr<HelperH264FileParser> h264FileParser(
      new HelperH264FileParser(options.videoFile.c_str()));
  h264FileParser->initialize();
 
  // Calculate send interval based on frame rate. H264 frames are sent at this interval
  PacerInfo pacer = {0, 1000 / options.video.frameRate, 0, std::chrono::steady_clock::now()};
 
  while (!exitFlag) {
    if (auto h264Frame = h264FileParser->getH264Frame()) {
      sendOneH264Frame(options.video.frameRate, std::move(h264Frame), videoH264FrameSender);
      waitBeforeNextSend(pacer);  // sleep for a while before sending next frame
    }
  };
}
 
static bool exitFlag = false;
static void SignalHandler(int sigNo) { exitFlag = true; }
 
int main(int argc, char* argv[]) {
  SampleOptions options;
  opt_parser optParser;
 
  optParser.add_long_opt("token", &options.appId, "The token for authentication / must");
  optParser.add_long_opt("channelId", &options.channelId, "Channel Id / must");
  optParser.add_long_opt("userId", &options.userId, "User Id / default is 0");
  optParser.add_long_opt("audioFile", &options.audioFile,
                         "The audio file in raw PCM format to be sent");
  optParser.add_long_opt("videoFile", &options.videoFile,
                         "The video file in YUV420 format to be sent");
  optParser.add_long_opt("sampleRate", &options.audio.sampleRate,
                         "Sample rate for the PCM file to be sent");
  optParser.add_long_opt("numOfChannels", &options.audio.numOfChannels,
                         "Number of channels for the PCM file to be sent");
  optParser.add_long_opt("fps", &options.video.frameRate,
                         "Target frame rate for sending the video stream");
  optParser.add_long_opt("bwe", &options.video.showBandwidthEstimation,
                         "show or hide bandwidth estimation info");
  optParser.add_long_opt("localIP", &options.localIP,
                         "Local IP");
 
  optParser.add_long_opt("custom_channel_num", &options.customChannelNum,
                         "Custom channel number");
  optParser.add_long_opt("custom_payload_type", &options.customPayloadType,
                         "Custom payload type");
 
  if ((argc <= 1) || !optParser.parse_opts(argc, argv)) {
    std::ostringstream strStream;
    optParser.print_usage(argv[0], strStream);
    std::cout << strStream.str() << std::endl;
    return -1;
  }
 
  if (options.appId.empty()) {
    AG_LOG(ERROR, "Must provide appId!");
    return -1;
  }
 
  if (options.channelId.empty()) {
    AG_LOG(ERROR, "Must provide channelId!");
    return -1;
  }

  if(options.audioFile.empty()){
    AG_LOG(ERROR, "Must provide audio file path!");
    return -1;
  }else{
    std::string inputAudioFilePath = "input audio file location: " + options.audioFile;
    AG_LOG(INFO, "%s", inputAudioFilePath.c_str());

  }


 
  std::signal(SIGQUIT, SignalHandler);
  std::signal(SIGABRT, SignalHandler);
  std::signal(SIGINT, SignalHandler);
 
  // Create Agora service
  auto service = createAndInitAgoraService(false, true, true);
  if (!service) {
    AG_LOG(ERROR, "Failed to creating Agora service!");
  }
 
  // Create Agora connection
  agora::rtc::RtcConnectionConfiguration ccfg;
  ccfg.autoSubscribeAudio = false;
  ccfg.autoSubscribeVideo = false;
  ccfg.clientRoleType = agora::rtc::CLIENT_ROLE_BROADCASTER;
  agora::agora_refptr<agora::rtc::IRtcConnection> connection = service->createRtcConnection(ccfg);
  if (!connection) {
    AG_LOG(ERROR, "Failed to creating Agora connection!");
    return -1;
  }
 
  if (!options.localIP.empty()) {
    if (setLocalIP(connection, options.localIP)){
      AG_LOG(ERROR, "set local IP to %s error!", options.localIP.c_str());
      return -1;
    }
  }
 
  auto p = connection->getAgoraParameter();
  p->setInt("che.audio.custom_payload_type", options.customPayloadType);  // 使用自定义负载类型
  p->setInt("che.audio.custom_channel_num", options.customChannelNum);  // 使用自定义频道号
 
 
  // Register connection observer to monitor connection event
  auto connObserver = std::make_shared<SampleConnectionObserver>();
  connection->registerObserver(connObserver.get());
 
  // Register network observer to monitor bandwidth estimation result
  if (options.video.showBandwidthEstimation) {
    connection->registerNetworkObserver(connObserver.get());
  }
 
  // Create local user observer to monitor intra frame request
  auto localUserObserver = std::make_shared<SampleLocalUserObserver>(connection->getLocalUser());
 
  // Connect to Agora channel
  if (connection->connect(options.appId.c_str(), options.channelId.c_str(),
                          options.userId.c_str())) {
    AG_LOG(ERROR, "Failed to connect to Agora channel!");
    return -1;
  }
 
  // Create media node factory
  agora::agora_refptr<agora::rtc::IMediaNodeFactory> factory = service->createMediaNodeFactory();
  if (!factory) {
    AG_LOG(ERROR, "Failed to create media node factory!");
  }
 
  // Create audio data sender
  agora::agora_refptr<agora::rtc::IAudioPcmDataSender> audioFrameSender =
      factory->createAudioPcmDataSender();
  if (!audioFrameSender) {
    AG_LOG(ERROR, "Failed to create audio data sender!");
    return -1;
  }
 
  // Create audio track
  agora::agora_refptr<agora::rtc::ILocalAudioTrack> customAudioTrack =
      service->createCustomAudioTrack(audioFrameSender);
  if (!customAudioTrack) {
    AG_LOG(ERROR, "Failed to create audio track!");
    return -1;
  }
 
  // Create video frame sender
  agora::agora_refptr<agora::rtc::IVideoEncodedImageSender> videoFrameSender =
      factory->createVideoEncodedImageSender();
  if (!videoFrameSender) {
    AG_LOG(ERROR, "Failed to create video frame sender!");
    return -1;
  }
 
  agora::rtc::SenderOptions option;
  option.ccMode = agora::rtc::TCcMode::CC_ENABLED;
  // Create video track
  agora::agora_refptr<agora::rtc::ILocalVideoTrack> customVideoTrack =
      service->createCustomVideoTrack(videoFrameSender, option);
  if (!customVideoTrack) {
    AG_LOG(ERROR, "Failed to create video track!");
    return -1;
  }
 
  // Publish audio & video track
  connection->getLocalUser()->publishAudio(customAudioTrack);
  connection->getLocalUser()->publishVideo(customVideoTrack);
 
  // Wait until connected before sending media stream
  connObserver->waitUntilConnected(DEFAULT_CONNECT_TIMEOUT_MS);
 
  if (!options.localIP.empty()) {
    std::string ip;
    getLocalIP(connection, ip);
    AG_LOG(INFO, "Local IP:%s", ip.c_str());
  }
 
  // Start sending media data
  AG_LOG(INFO, "Start sending audio & video data ...");
  std::thread sendAudioThread(SampleSendAudioTask, std::ref(options), audioFrameSender, std::ref(exitFlag));
  std::thread sendVideoThread(SampleSendVideoH264Task, std::ref(options), videoFrameSender,
                              std::ref(exitFlag));
 
  sendAudioThread.join();
  sendVideoThread.join();
 
  // Unpublish audio & video track
  connection->getLocalUser()->unpublishAudio(customAudioTrack);
  connection->getLocalUser()->unpublishVideo(customVideoTrack);
 
  // Unregister connection observer
  connection->unregisterObserver(connObserver.get());
 
  // Unregister network observer
  connection->unregisterNetworkObserver(connObserver.get());
 
  // Disconnect from Agora channel
  if (connection->disconnect()) {
    AG_LOG(ERROR, "Failed to disconnect from Agora channel!");
    return -1;
  }
  AG_LOG(INFO, "Disconnected from Agora channel successfully");
 
  // Destroy Agora connection and related resources
  connObserver.reset();
  localUserObserver.reset();
  audioFrameSender = nullptr;
  videoFrameSender = nullptr;
  customAudioTrack = nullptr;
  customVideoTrack = nullptr;
  factory = nullptr;
  connection = nullptr;
 
  // Destroy Agora Service
  service->release();
  service = nullptr;
 
  return 0;
}