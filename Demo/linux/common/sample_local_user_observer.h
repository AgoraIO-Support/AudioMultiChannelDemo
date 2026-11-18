//  Agora RTC/MEDIA SDK
//
//  Created by Pengfei Han in 2020-03.
//  Copyright (c) 2020 Agora.io. All rights reserved.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "AgoraBase.h"
#include "NGIAgoraLocalUser.h"
#include "NGIAgoraAudioTrack.h"

#include "NGIAgoraMediaNodeFactory.h"
#include "NGIAgoraMediaNode.h"
#include "NGIAgoraVideoTrack.h"
#include "NGIAgoraRtcConnection.h"
#include "NGIAgoraVideoMixerSource.h"


struct videoInfo{
  int width;
  int height;
  bool muted;
  videoInfo():width(0),height(0),muted(true) {};
  videoInfo(int w ,int h ,bool mute):width(w),height(h),muted(mute) {};
};

// The SDK encoded-frame callback does not include the remote UID. The common
// observer creates one receiver adapter per subscribed audio track and adds
// the UID at the application boundary before forwarding the callback.
using EncodedAudioFrameCallback = std::function<bool(
    const std::string&, const uint8_t*, size_t,
    const agora::media::base::AudioEncodedFrameInfo&)>;

class SampleLocalUserObserver : public agora::rtc::ILocalUserObserver {
 public:
  SampleLocalUserObserver(agora::rtc::IRtcConnection* connection);
  SampleLocalUserObserver(agora::rtc::ILocalUser* user);
  virtual ~SampleLocalUserObserver();

 public:
  agora::rtc::ILocalUser* GetLocalUser();
  void PublishAudioTrack(agora::agora_refptr<agora::rtc::ILocalAudioTrack> audioTrack);
  void PublishVideoTrack(agora::agora_refptr<agora::rtc::ILocalVideoTrack> videoTrack);
  void UnpublishAudioTrack(agora::agora_refptr<agora::rtc::ILocalAudioTrack> audioTrack);
  void UnpublishVideoTrack(agora::agora_refptr<agora::rtc::ILocalVideoTrack> videoTrack);
  // Compatibility accessor for callers that only handle one remote user. New
  // code should use the UID overload so that it cannot accidentally select a
  // different user after another subscription arrives.
  agora::agora_refptr<agora::rtc::IRemoteAudioTrack> GetRemoteAudioTrack();
  agora::agora_refptr<agora::rtc::IRemoteAudioTrack> GetRemoteAudioTrack(
      const std::string& userId);
  bool GetRemoteAudioTrackStats(const std::string& userId,
                                agora::rtc::RemoteAudioTrackStats& stats);
  // Compatibility accessor; prefer the UID overload for multi-user receive.
  agora::agora_refptr<agora::rtc::IRemoteVideoTrack> GetRemoteVideoTrack();
  agora::agora_refptr<agora::rtc::IRemoteVideoTrack> GetRemoteVideoTrack(
      const std::string& userId);
  bool GetRemoteVideoTrackStats(const std::string& userId,
                                agora::rtc::RemoteVideoTrackStats& stats);

  void setVideoEncodedImageReceiver(agora::media::IVideoEncodedFrameObserver* receiver);

  void setEncodedAudioFrameObserver(agora::rtc::IAudioEncodedFrameReceiver* observer);

  void setEncodedAudioFrameCallback(const EncodedAudioFrameCallback& callback);

  void setAudioFrameObserver(agora::media::IAudioFrameObserverBase* observer);
  
  void setSubscribeUserString(std::string username){
      user_string_ = username;
  }
  void unsetAudioFrameObserver();

  void setVideoFrameObserver(agora::rtc::IVideoFrameObserver2* observer);

  void setUseStringUid(bool used){
     use_string_uid_ = used;
  }

  void unsetVideoFrameObserver();

  void setEnableVideoMix(bool enable){
       enable_video_mix_ = enable;
  }

void onStreamMessage(agora::user_id_t userId, int streamId, const char* data, size_t length) {
        printf("the message is %s \n",data);
    }

  void setVideoMixer( agora::agora_refptr<agora::rtc::IVideoMixerSource> video_mixer){
       video_mixer_ = video_mixer;
  }

 public:
  // inherit from agora::rtc::ILocalUserObserver
  void onAudioTrackPublishSuccess(
      agora::agora_refptr<agora::rtc::ILocalAudioTrack> audioTrack) override {}

  void onUserAudioTrackSubscribed(
      agora::user_id_t userId,
      agora::agora_refptr<agora::rtc::IRemoteAudioTrack> audioTrack) override;

  void onAudioTrackPublicationFailure(agora::agora_refptr<agora::rtc::ILocalAudioTrack> audioTrack,
                                      agora::ERROR_CODE_TYPE error) override {}

  void onUserAudioTrackStateChanged(agora::user_id_t userId,
                                    agora::agora_refptr<agora::rtc::IRemoteAudioTrack> audioTrack,
                                    agora::rtc::REMOTE_AUDIO_STATE state,
                                    agora::rtc::REMOTE_AUDIO_STATE_REASON reason,
                                    int elapsed) override;

  void onVideoTrackPublishSuccess(agora::agora_refptr<agora::rtc::ILocalVideoTrack> videoTrack) override {}

  void onVideoTrackPublicationFailure(agora::agora_refptr<agora::rtc::ILocalVideoTrack> videoTrack,
                                      agora::ERROR_CODE_TYPE error) override {}

  void onUserVideoTrackSubscribed(
      agora::user_id_t userId,const agora::rtc::VideoTrackInfo& trackInfo,
      agora::agora_refptr<agora::rtc::IRemoteVideoTrack> videoTrack) override;

  void onUserVideoTrackStateChanged(agora::user_id_t userId,
                                    agora::agora_refptr<agora::rtc::IRemoteVideoTrack> videoTrack,
                                    agora::rtc::REMOTE_VIDEO_STATE state,
                                    agora::rtc::REMOTE_VIDEO_STATE_REASON reason,
                                    int elapsed) ;

  void onRemoteVideoTrackStatistics(agora::agora_refptr<agora::rtc::IRemoteVideoTrack> videoTrack,
                                    const agora::rtc::RemoteVideoTrackStats& stats) override;

  void onLocalVideoTrackStateChanged(agora::agora_refptr<agora::rtc::ILocalVideoTrack> videoTrack,
                                     agora::rtc::LOCAL_VIDEO_STREAM_STATE state,
                                     agora::rtc::LOCAL_VIDEO_STREAM_REASON reason) override {}

  void onLocalVideoTrackStatistics(agora::agora_refptr<agora::rtc::ILocalVideoTrack> videoTrack,
                                   const agora::rtc::LocalVideoTrackStats& stats) override {}

  void onAudioVolumeIndication(const agora::rtc::AudioVolumeInformation* speakers,
                               unsigned int speakerNumber, int totalVolume) override {}

  void onLocalAudioTrackStatistics(const agora::rtc::LocalAudioStats& stats) override {}

  void onRemoteAudioTrackStatistics(agora::agora_refptr<agora::rtc::IRemoteAudioTrack> audioTrack,
                                    const agora::rtc::RemoteAudioTrackStats& stats) override;
                                  
  //void onRemoteVideoStreamInfoUpdated(const agora::rtc::RemoteVideoStreamInfo& info) override {}
  

  void onUserInfoUpdated(agora::user_id_t userId, USER_MEDIA_INFO msg, bool val) override;

  void onIntraRequestReceived() override;

  void onAudioSubscribeStateChanged(const char* channel, agora::user_id_t uid,
                                    agora::rtc::STREAM_SUBSCRIBE_STATE oldState,
                                    agora::rtc::STREAM_SUBSCRIBE_STATE newState,
                                    int elapseSinceLastState) override {}

  void onVideoSubscribeStateChanged(const char* channel, agora::user_id_t uid,
                                    agora::rtc::STREAM_SUBSCRIBE_STATE oldState,
                                    agora::rtc::STREAM_SUBSCRIBE_STATE newState,
                                    int elapseSinceLastState) override {}

  void onAudioPublishStateChanged(const char* channel, agora::rtc::STREAM_PUBLISH_STATE oldState,
                                  agora::rtc::STREAM_PUBLISH_STATE newState,
                                  int elapseSinceLastState) override {}

  void onVideoPublishStateChanged(const char* channel, agora::rtc::STREAM_PUBLISH_STATE oldState,
                                  agora::rtc::STREAM_PUBLISH_STATE newState,
                                  int elapseSinceLastState) override {}
  void onFirstRemoteVideoFrameRendered(agora::user_id_t userId, int width,
                                               int height, int elapsed){}
  void onFirstRemoteVideoFrame(agora::user_id_t userId, int width, int height, int elapsed){}
  void onFirstRemoteAudioFrame(agora::user_id_t userId, int elapsed) {}
  void onFirstRemoteAudioDecoded(agora::user_id_t userId, int elapsed) {}
  void onFirstRemoteVideoDecoded(agora::user_id_t userId, int width, int height, int elapsed) override;

  // virtual void onFirstRemoteAudioDecoded(agora::user_id_t userId, int elapsed){}
  void onAudioTrackPublishStart(agora::agora_refptr<agora::rtc::ILocalAudioTrack> audioTrack) override {}

  void onAudioTrackUnpublished(agora::agora_refptr<agora::rtc::ILocalAudioTrack> audioTrack) override {}

  void onVideoTrackPublishStart(agora::agora_refptr<agora::rtc::ILocalVideoTrack> videoTrack) override {}

  void onVideoTrackUnpublished(agora::agora_refptr<agora::rtc::ILocalVideoTrack> videoTrack)override {}

  void onVideoSizeChanged(agora::user_id_t userId, int width, int height, int rotation) {}
  void onActiveSpeaker(agora::user_id_t userId){}
 private:

  struct RemoteAudioTrackState {
    agora::agora_refptr<agora::rtc::IRemoteAudioTrack> track;
    std::shared_ptr<agora::rtc::IAudioEncodedFrameReceiver> encoded_receiver;
    agora::rtc::IAudioEncodedFrameReceiver* raw_encoded_receiver{nullptr};
    agora::rtc::RemoteAudioTrackStats stats;
    bool has_stats{false};
  };

  struct RemoteVideoTrackState {
    agora::agora_refptr<agora::rtc::IRemoteVideoTrack> track;
    agora::rtc::RemoteVideoTrackStats stats;
    bool has_stats{false};
  };

  void registerAudioFrameObserverIfNeededLocked();
  void registerVideoFrameObserverIfNeededLocked();
  void clearAudioEncodedReceiversLocked();
  void removeRemoteAudioTrackLocked(const std::string& userId);
  void removeRemoteVideoTrackLocked(const std::string& userId);
  void clearRemoteTrackStates();

  agora::rtc::MixerLayoutConfig calculate_layout(int width,int height);

  agora::rtc::IRtcConnection* connection_{nullptr};
  agora::rtc::ILocalUser* local_user_{nullptr};

  std::map<std::string, RemoteAudioTrackState> remote_audio_tracks_;
  std::map<std::string, RemoteVideoTrackState> remote_video_tracks_;
  std::string last_audio_uid_;
  std::string last_video_uid_;

  agora::agora_refptr<agora::rtc::IVideoMixerSource> video_mixer_{nullptr};

  agora::media::IVideoEncodedFrameObserver* video_encoded_receiver_{nullptr};
  agora::rtc::IAudioEncodedFrameReceiver* audio_encoded_receiver_{nullptr};
  EncodedAudioFrameCallback encoded_audio_frame_callback_;

  agora::media::IAudioFrameObserverBase* audio_frame_observer_{nullptr};
  agora::rtc::IVideoFrameObserver2* video_frame_observer_{nullptr};
  std::map<std::string ,videoInfo> remote_source_map_;


  std::mutex observer_lock_;
  bool use_string_uid_{false};
  std::string  user_string_; 

  bool audio_frame_observer_registered_{false};
  bool video_frame_observer_registered_{false};
  bool video_encoded_receiver_registered_{false};

  bool enable_video_mix_{false};
  int x_offset_{0};
  int y_offset_{0};
};
