//  Agora RTC/MEDIA SDK
//
//  Created by Pengfei Han in 2020-03.
//  Copyright (c) 2020 Agora.io. All rights reserved.
//

#include "sample_local_user_observer.h"

#include "log.h"

namespace {

std::string normalizeUserId(const char* userId) {
  return (userId && *userId) ? userId : "unknown";
}

class UserEncodedAudioFrameReceiver final : public agora::rtc::IAudioEncodedFrameReceiver {
 public:
  UserEncodedAudioFrameReceiver(const std::string& userId,
                                const EncodedAudioFrameCallback& callback)
      : user_id_(userId), callback_(callback) {}

  bool onEncodedAudioFrameReceived(
      const uint8_t* packet, size_t length,
      const agora::media::base::AudioEncodedFrameInfo& info) override {
    return callback_ ? callback_(user_id_, packet, length, info) : true;
  }

 private:
  std::string user_id_;
  EncodedAudioFrameCallback callback_;
};

}  // namespace

SampleLocalUserObserver::SampleLocalUserObserver(agora::rtc::IRtcConnection* connection)
    : connection_(connection) {
  local_user_ = connection_->getLocalUser();
  local_user_->registerLocalUserObserver(this);
}

SampleLocalUserObserver::SampleLocalUserObserver(agora::rtc::ILocalUser* user)
    : local_user_(user) {
  local_user_->registerLocalUserObserver(this);
}

SampleLocalUserObserver::~SampleLocalUserObserver() {
  local_user_->unregisterLocalUserObserver(this);
  clearRemoteTrackStates();
}

agora::rtc::ILocalUser* SampleLocalUserObserver::GetLocalUser() {
  return local_user_;
}

agora::agora_refptr<agora::rtc::IRemoteAudioTrack>
SampleLocalUserObserver::GetRemoteAudioTrack() {
  std::lock_guard<std::mutex> lock(observer_lock_);
  auto it = remote_audio_tracks_.find(last_audio_uid_);
  if (it != remote_audio_tracks_.end()) return it->second.track;
  return remote_audio_tracks_.empty() ? nullptr : remote_audio_tracks_.begin()->second.track;
}

void SampleLocalUserObserver::PublishAudioTrack(
    agora::agora_refptr<agora::rtc::ILocalAudioTrack> audioTrack) {
  local_user_->publishAudio(audioTrack);
}

void SampleLocalUserObserver::PublishVideoTrack(
    agora::agora_refptr<agora::rtc::ILocalVideoTrack> videoTrack) {
  local_user_->publishVideo(videoTrack);
}

void SampleLocalUserObserver::UnpublishAudioTrack(
    agora::agora_refptr<agora::rtc::ILocalAudioTrack> audioTrack) {
  local_user_->unpublishAudio(audioTrack);
}

void SampleLocalUserObserver::UnpublishVideoTrack(
    agora::agora_refptr<agora::rtc::ILocalVideoTrack> videoTrack) {
  local_user_->unpublishVideo(videoTrack);
}

agora::agora_refptr<agora::rtc::IRemoteAudioTrack>
SampleLocalUserObserver::GetRemoteAudioTrack(const std::string& userId) {
  std::lock_guard<std::mutex> lock(observer_lock_);
  auto it = remote_audio_tracks_.find(userId);
  return it == remote_audio_tracks_.end() ? nullptr : it->second.track;
}

bool SampleLocalUserObserver::GetRemoteAudioTrackStats(
    const std::string& userId, agora::rtc::RemoteAudioTrackStats& stats) {
  std::lock_guard<std::mutex> lock(observer_lock_);
  auto it = remote_audio_tracks_.find(userId);
  if (it == remote_audio_tracks_.end() || !it->second.has_stats) return false;
  stats = it->second.stats;
  return true;
}

agora::agora_refptr<agora::rtc::IRemoteVideoTrack>
SampleLocalUserObserver::GetRemoteVideoTrack(const std::string& userId) {
  std::lock_guard<std::mutex> lock(observer_lock_);
  auto it = remote_video_tracks_.find(userId);
  return it == remote_video_tracks_.end() ? nullptr : it->second.track;
}

bool SampleLocalUserObserver::GetRemoteVideoTrackStats(
    const std::string& userId, agora::rtc::RemoteVideoTrackStats& stats) {
  std::lock_guard<std::mutex> lock(observer_lock_);
  auto it = remote_video_tracks_.find(userId);
  if (it == remote_video_tracks_.end() || !it->second.has_stats) return false;
  stats = it->second.stats;
  return true;
}

agora::agora_refptr<agora::rtc::IRemoteVideoTrack>
SampleLocalUserObserver::GetRemoteVideoTrack() {
  std::lock_guard<std::mutex> lock(observer_lock_);
  auto it = remote_video_tracks_.find(last_video_uid_);
  if (it != remote_video_tracks_.end()) return it->second.track;
  return remote_video_tracks_.empty() ? nullptr : remote_video_tracks_.begin()->second.track;
}

void SampleLocalUserObserver::setAudioFrameObserver(
    agora::media::IAudioFrameObserverBase* observer) {
  std::lock_guard<std::mutex> lock(observer_lock_);
  if (audio_frame_observer_registered_ && audio_frame_observer_ != observer) {
    local_user_->unregisterAudioFrameObserver(audio_frame_observer_);
    audio_frame_observer_registered_ = false;
  }
  audio_frame_observer_ = observer;
  if (audio_frame_observer_ && !remote_audio_tracks_.empty()) {
    registerAudioFrameObserverIfNeededLocked();
  }
}

void SampleLocalUserObserver::unsetAudioFrameObserver() {
  std::lock_guard<std::mutex> lock(observer_lock_);
  if (audio_frame_observer_registered_ && audio_frame_observer_) {
    local_user_->unregisterAudioFrameObserver(audio_frame_observer_);
  }
  audio_frame_observer_registered_ = false;
  audio_frame_observer_ = nullptr;
}

void SampleLocalUserObserver::setVideoFrameObserver(
    agora::rtc::IVideoFrameObserver2* observer) {
  std::lock_guard<std::mutex> lock(observer_lock_);
  if (video_frame_observer_registered_ && video_frame_observer_ != observer) {
    local_user_->unregisterVideoFrameObserver(video_frame_observer_);
    video_frame_observer_registered_ = false;
  }
  video_frame_observer_ = observer;
  if (video_frame_observer_ && !remote_video_tracks_.empty()) {
    registerVideoFrameObserverIfNeededLocked();
  }
}

void SampleLocalUserObserver::unsetVideoFrameObserver() {
  std::lock_guard<std::mutex> lock(observer_lock_);
  if (video_frame_observer_registered_ && video_frame_observer_) {
    local_user_->unregisterVideoFrameObserver(video_frame_observer_);
  }
  video_frame_observer_registered_ = false;
  video_frame_observer_ = nullptr;
}

void SampleLocalUserObserver::setVideoEncodedImageReceiver(
    agora::media::IVideoEncodedFrameObserver* receiver) {
  std::lock_guard<std::mutex> lock(observer_lock_);
  if (video_encoded_receiver_registered_ && video_encoded_receiver_) {
    local_user_->unregisterVideoEncodedFrameObserver(video_encoded_receiver_);
    video_encoded_receiver_registered_ = false;
  }
  video_encoded_receiver_ = receiver;
  if (video_encoded_receiver_ && !remote_video_tracks_.empty()) {
    video_encoded_receiver_registered_ =
        local_user_->registerVideoEncodedFrameObserver(video_encoded_receiver_) == 0;
  }
}

void SampleLocalUserObserver::clearAudioEncodedReceiversLocked() {
  for (auto& item : remote_audio_tracks_) {
    if (!item.second.track) continue;
    if (item.second.encoded_receiver) {
      item.second.track->unregisterAudioEncodedFrameReceiver(
          item.second.encoded_receiver.get());
      item.second.encoded_receiver.reset();
    }
    if (item.second.raw_encoded_receiver) {
      item.second.track->unregisterAudioEncodedFrameReceiver(item.second.raw_encoded_receiver);
      item.second.raw_encoded_receiver = nullptr;
    }
  }
}

void SampleLocalUserObserver::setEncodedAudioFrameObserver(
    agora::rtc::IAudioEncodedFrameReceiver* observer) {
  std::lock_guard<std::mutex> lock(observer_lock_);
  clearAudioEncodedReceiversLocked();
  audio_encoded_receiver_ = observer;
  encoded_audio_frame_callback_ = nullptr;
  if (!audio_encoded_receiver_) return;

  agora::rtc::AudioEncFrameRecvParams params;
  for (auto& item : remote_audio_tracks_) {
    if (item.second.track) {
      if (item.second.track->registerAudioEncodedFrameReceiver(audio_encoded_receiver_, params) ==
          0) {
        item.second.raw_encoded_receiver = audio_encoded_receiver_;
      }
    }
  }
}

void SampleLocalUserObserver::setEncodedAudioFrameCallback(
    const EncodedAudioFrameCallback& callback) {
  std::lock_guard<std::mutex> lock(observer_lock_);
  clearAudioEncodedReceiversLocked();
  audio_encoded_receiver_ = nullptr;
  encoded_audio_frame_callback_ = callback;
  if (!encoded_audio_frame_callback_) return;

  agora::rtc::AudioEncFrameRecvParams params;
  for (auto& item : remote_audio_tracks_) {
    if (!item.second.track) continue;
    if (item.second.encoded_receiver) {
      item.second.track->unregisterAudioEncodedFrameReceiver(
          item.second.encoded_receiver.get());
      item.second.encoded_receiver.reset();
    }
    if (item.second.raw_encoded_receiver) {
      item.second.track->unregisterAudioEncodedFrameReceiver(item.second.raw_encoded_receiver);
      item.second.raw_encoded_receiver = nullptr;
    }
    auto receiver = std::make_shared<UserEncodedAudioFrameReceiver>(
        item.first, encoded_audio_frame_callback_);
    if (item.second.track->registerAudioEncodedFrameReceiver(receiver.get(), params) == 0) {
      item.second.encoded_receiver = receiver;
    }
  }
}

void SampleLocalUserObserver::registerAudioFrameObserverIfNeededLocked() {
  if (!audio_frame_observer_ || audio_frame_observer_registered_) return;
  if (local_user_->registerAudioFrameObserver(audio_frame_observer_) == 0) {
    audio_frame_observer_registered_ = true;
  }
}

void SampleLocalUserObserver::registerVideoFrameObserverIfNeededLocked() {
  if (!video_frame_observer_ || video_frame_observer_registered_) return;
  if (local_user_->registerVideoFrameObserver(video_frame_observer_) == 0) {
    video_frame_observer_registered_ = true;
  }
}

void SampleLocalUserObserver::removeRemoteAudioTrackLocked(const std::string& userId) {
  auto it = remote_audio_tracks_.find(userId);
  if (it != remote_audio_tracks_.end()) {
    if (it->second.track && it->second.encoded_receiver) {
      it->second.track->unregisterAudioEncodedFrameReceiver(it->second.encoded_receiver.get());
    }
    if (it->second.track && it->second.raw_encoded_receiver) {
      it->second.track->unregisterAudioEncodedFrameReceiver(it->second.raw_encoded_receiver);
    }
    remote_audio_tracks_.erase(it);
  }
  if (remote_audio_tracks_.empty()) {
    last_audio_uid_.clear();
  } else if (last_audio_uid_ == userId) {
    last_audio_uid_ = remote_audio_tracks_.begin()->first;
  }
  // The PCM observer belongs to the local user, not to an individual track.
  // Keep it registered across the last track disappearing or being replaced;
  // unregistering and registering again inside the subscription callback can
  // leave before-mixing PCM callbacks stopped after a remote republish.
  // unsetAudioFrameObserver()/clearRemoteTrackStates() handle final teardown.
}

void SampleLocalUserObserver::removeRemoteVideoTrackLocked(const std::string& userId) {
  auto it = remote_video_tracks_.find(userId);
  if (it != remote_video_tracks_.end()) {
    if (video_mixer_ && enable_video_mix_ && it->second.track) {
      video_mixer_->removeVideoTrack(userId.c_str(), it->second.track);
    }
    remote_video_tracks_.erase(it);
  }
  if (remote_video_tracks_.empty()) {
    last_video_uid_.clear();
  } else if (last_video_uid_ == userId) {
    last_video_uid_ = remote_video_tracks_.begin()->first;
  }
  remote_source_map_.erase(userId);
  if (video_mixer_ && enable_video_mix_) {
    x_offset_ = 0;
    y_offset_ = 0;
    for (auto& source : remote_source_map_) {
      if (!source.second.muted) {
        video_mixer_->setStreamLayout(
            source.first.c_str(), calculate_layout(source.second.width, source.second.height));
      }
    }
    video_mixer_->refresh();
  }
  if (remote_video_tracks_.empty()) {
    if (video_frame_observer_registered_ && video_frame_observer_) {
      local_user_->unregisterVideoFrameObserver(video_frame_observer_);
      video_frame_observer_registered_ = false;
    }
    if (video_encoded_receiver_registered_ && video_encoded_receiver_) {
      local_user_->unregisterVideoEncodedFrameObserver(video_encoded_receiver_);
      video_encoded_receiver_registered_ = false;
    }
  }
}

void SampleLocalUserObserver::clearRemoteTrackStates() {
  std::lock_guard<std::mutex> lock(observer_lock_);
  if (audio_frame_observer_registered_ && audio_frame_observer_) {
    local_user_->unregisterAudioFrameObserver(audio_frame_observer_);
  }
  if (video_frame_observer_registered_ && video_frame_observer_) {
    local_user_->unregisterVideoFrameObserver(video_frame_observer_);
  }
  if (video_encoded_receiver_registered_ && video_encoded_receiver_) {
    local_user_->unregisterVideoEncodedFrameObserver(video_encoded_receiver_);
  }
  clearAudioEncodedReceiversLocked();
  if (video_mixer_ && enable_video_mix_) {
    for (auto& item : remote_video_tracks_) {
      if (item.second.track) {
        video_mixer_->removeVideoTrack(item.first.c_str(), item.second.track);
      }
    }
  }
  remote_audio_tracks_.clear();
  remote_video_tracks_.clear();
  last_audio_uid_.clear();
  last_video_uid_.clear();
  remote_source_map_.clear();
  audio_encoded_receiver_ = nullptr;
  encoded_audio_frame_callback_ = nullptr;
  video_encoded_receiver_ = nullptr;
  audio_frame_observer_registered_ = false;
  video_frame_observer_registered_ = false;
  video_encoded_receiver_registered_ = false;
}

void SampleLocalUserObserver::onUserAudioTrackSubscribed(
    agora::user_id_t userId,
    agora::agora_refptr<agora::rtc::IRemoteAudioTrack> audioTrack) {
  AG_LOG(INFO, "onUserAudioTrackSubscribed: userId %s", normalizeUserId(userId).c_str());
  std::lock_guard<std::mutex> lock(observer_lock_);
  const std::string uid = normalizeUserId(userId);
  removeRemoteAudioTrackLocked(uid);

  RemoteAudioTrackState state;
  state.track = audioTrack;
  remote_audio_tracks_[uid] = state;
  last_audio_uid_ = uid;

  if (!audioTrack) return;
  registerAudioFrameObserverIfNeededLocked();
  agora::rtc::AudioEncFrameRecvParams params;
  if (encoded_audio_frame_callback_) {
    auto receiver = std::make_shared<UserEncodedAudioFrameReceiver>(
        uid, encoded_audio_frame_callback_);
    if (audioTrack->registerAudioEncodedFrameReceiver(receiver.get(), params) == 0) {
      remote_audio_tracks_[uid].encoded_receiver = receiver;
    }
  } else if (audio_encoded_receiver_) {
    if (audioTrack->registerAudioEncodedFrameReceiver(audio_encoded_receiver_, params) == 0) {
      remote_audio_tracks_[uid].raw_encoded_receiver = audio_encoded_receiver_;
    }
  }
}

void SampleLocalUserObserver::onUserVideoTrackSubscribed(
    agora::user_id_t userId, const agora::rtc::VideoTrackInfo& trackInfo,
    agora::agora_refptr<agora::rtc::IRemoteVideoTrack> videoTrack) {
  AG_LOG(INFO, "onUserVideoTrackSubscribed: userId %s, codecType %d, encodedFrameOnly %d",
         normalizeUserId(userId).c_str(), trackInfo.codecType, trackInfo.encodedFrameOnly);
  std::lock_guard<std::mutex> lock(observer_lock_);
  const std::string uid = normalizeUserId(userId);
  removeRemoteVideoTrackLocked(uid);
  remote_video_tracks_[uid].track = videoTrack;
  last_video_uid_ = uid;

  if (videoTrack && video_encoded_receiver_ && !video_encoded_receiver_registered_) {
    video_encoded_receiver_registered_ =
        local_user_->registerVideoEncodedFrameObserver(video_encoded_receiver_) == 0;
  }
  if (videoTrack) registerVideoFrameObserverIfNeededLocked();
  if (videoTrack && video_mixer_ && enable_video_mix_) {
    video_mixer_->addVideoTrack(uid.c_str(), videoTrack);
  }
}

void SampleLocalUserObserver::onFirstRemoteVideoDecoded(agora::user_id_t userId, int width,
                                                         int height, int elapsed) {
  std::lock_guard<std::mutex> lock(observer_lock_);
  if (video_mixer_ && enable_video_mix_) {
    const std::string uid = normalizeUserId(userId);
    remote_source_map_[uid] = videoInfo(width, height, false);
    x_offset_ = 0;
    y_offset_ = 0;
    for (auto it = remote_source_map_.begin(); it != remote_source_map_.end(); ++it) {
      video_mixer_->setStreamLayout(
          it->first.c_str(), calculate_layout(it->second.width, it->second.height));
    }
    video_mixer_->refresh();
  }
}

agora::rtc::MixerLayoutConfig SampleLocalUserObserver::calculate_layout(int width, int height) {
  agora::rtc::MixerLayoutConfig mixConfig;
  mixConfig.height = height;
  mixConfig.width = width;
  mixConfig.zOrder = 1;
  mixConfig.x = x_offset_;
  mixConfig.y = y_offset_;
  if (x_offset_ + width < 1920) {
    x_offset_ += width;
  } else {
    x_offset_ = width;
    y_offset_ += height;
  }
  AG_LOG(INFO, "the x_offset id %d y_offset is %d ", x_offset_, y_offset_);
  return mixConfig;
}

void SampleLocalUserObserver::onUserVideoTrackStateChanged(
    agora::user_id_t userId,
    agora::agora_refptr<agora::rtc::IRemoteVideoTrack> videoTrack,
    agora::rtc::REMOTE_VIDEO_STATE state,
    agora::rtc::REMOTE_VIDEO_STATE_REASON reason, int elapsed) {
  std::lock_guard<std::mutex> lock(observer_lock_);
  const std::string uid = normalizeUserId(userId);
  if (state == agora::rtc::REMOTE_VIDEO_STATE_STOPPED &&
      reason == agora::rtc::REMOTE_VIDEO_STATE_REASON_REMOTE_OFFLINE) {
    auto it = remote_video_tracks_.find(uid);
    if (it != remote_video_tracks_.end() && (!videoTrack || it->second.track == videoTrack)) {
      removeRemoteVideoTrackLocked(uid);
    }
    return;
  }
  if (!(video_mixer_ && enable_video_mix_)) return;

  if (state == agora::rtc::REMOTE_VIDEO_STATE_STOPPED) {
    auto it = remote_source_map_.find(uid);
    if (it != remote_source_map_.end()) it->second.muted = true;
  } else if (state == agora::rtc::REMOTE_VIDEO_STATE_STARTING ||
             state == agora::rtc::REMOTE_VIDEO_STATE_DECODING) {
    auto it = remote_source_map_.find(uid);
    if (it != remote_source_map_.end()) it->second.muted = false;
  } else {
    return;
  }

  x_offset_ = 0;
  y_offset_ = 0;
  for (auto it = remote_source_map_.begin(); it != remote_source_map_.end(); ++it) {
    if (!it->second.muted) {
      video_mixer_->setStreamLayout(
          it->first.c_str(), calculate_layout(it->second.width, it->second.height));
    }
  }
  video_mixer_->refresh();
}

void SampleLocalUserObserver::onUserInfoUpdated(agora::user_id_t userId,
                                                 ILocalUserObserver::USER_MEDIA_INFO msg,
                                                 bool val) {
  const std::string uid = normalizeUserId(userId);
  AG_LOG(INFO, "onUserInfoUpdated: userId %s, msg %d, val %d", uid.c_str(), msg, val);
  if (use_string_uid_ && !user_string_.empty() && userId && std::string(userId) == user_string_) {
    local_user_->subscribeAudio(user_string_.c_str());
    agora::rtc::VideoSubscriptionOptions subscriptionOptions;
    subscriptionOptions.encodedFrameOnly = true;
    local_user_->subscribeVideo(user_string_.c_str(), subscriptionOptions);
    AG_LOG(INFO, "subscribe audio & video from %s ", user_string_.c_str());
  }
}

void SampleLocalUserObserver::onUserAudioTrackStateChanged(
    agora::user_id_t userId,
    agora::agora_refptr<agora::rtc::IRemoteAudioTrack> audioTrack,
    agora::rtc::REMOTE_AUDIO_STATE state,
    agora::rtc::REMOTE_AUDIO_STATE_REASON reason, int elapsed) {
  AG_LOG(INFO, "onUserAudioTrackStateChanged: userId %s, state %d, reason %d",
         normalizeUserId(userId).c_str(), state, reason);
  if (state == agora::rtc::REMOTE_AUDIO_STATE_STOPPED &&
      reason == agora::rtc::REMOTE_AUDIO_REASON_REMOTE_OFFLINE) {
    std::lock_guard<std::mutex> lock(observer_lock_);
    const std::string uid = normalizeUserId(userId);
    auto it = remote_audio_tracks_.find(uid);
    if (it != remote_audio_tracks_.end() && (!audioTrack || it->second.track == audioTrack)) {
      removeRemoteAudioTrackLocked(uid);
    }
  }
}

void SampleLocalUserObserver::onRemoteAudioTrackStatistics(
    agora::agora_refptr<agora::rtc::IRemoteAudioTrack> audioTrack,
    const agora::rtc::RemoteAudioTrackStats& stats) {
  std::lock_guard<std::mutex> lock(observer_lock_);
  for (auto& item : remote_audio_tracks_) {
    if (item.second.track == audioTrack) {
      item.second.stats = stats;
      item.second.has_stats = true;
      return;
    }
  }
}

void SampleLocalUserObserver::onRemoteVideoTrackStatistics(
    agora::agora_refptr<agora::rtc::IRemoteVideoTrack> videoTrack,
    const agora::rtc::RemoteVideoTrackStats& stats) {
  std::lock_guard<std::mutex> lock(observer_lock_);
  for (auto& item : remote_video_tracks_) {
    if (item.second.track == videoTrack) {
      item.second.stats = stats;
      item.second.has_stats = true;
      return;
    }
  }
}

void SampleLocalUserObserver::onIntraRequestReceived() {
  AG_LOG(INFO, "onIntraRequestReceived");
}
