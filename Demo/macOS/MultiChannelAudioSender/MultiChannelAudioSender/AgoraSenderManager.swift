import Foundation
import AgoraRtcKit

/// 发送侧管理：观众/主播加入、多声道私参、自定义音轨 + 10ms pacer 推送 WAV PCM。
///
/// 与 Linux `sample_send_pcm_multiopus` 同口径：
/// - 私参 che.audio.custom_payload_type / custom_channel_num / custom_sample_rate 在 join 之前设置；
/// - payload type 映射：≤4ch → 123，≤6ch → 124，否则 → 125；
/// - 自定义音轨用 AUDIO_TRACK_DIRECT（不经过 3A）；
/// - 推送节奏 10 ms/帧，samplesPerChannel = 48000/100 = 480。
class AgoraSenderManager: NSObject, ObservableObject {
    @Published var isJoined = false
    @Published var isPublishing = false
    @Published var localUid: UInt = 0
    @Published var logs: [String] = []
    @Published var statusText = L10n.t("status.notJoined", in: L10n.current())
    @Published var sentFrames: Int = 0

    private var agoraKit: AgoraRtcEngineKit?
    private var currentAppId = ""
    private var trackId: NSInteger = -1

    private var sendThread: Thread?
    private var sendStopFlag = false

    override init() {
        super.init()
    }

    deinit {
        stopPublishing()
        agoraKit?.leaveChannel(nil)
        AgoraRtcEngineKit.destroy()
    }

    /// payload type 与声道数映射（spec 场景4.1）：123 → 3/4，124 → 5/6，125 → 7/8
    static func payloadType(forChannels channels: Int) -> Int {
        if channels <= 4 { return 123 }
        if channels <= 6 { return 124 }
        return 125
    }

    // MARK: - 日志

    func log(_ message: String) {
        let formatter = DateFormatter()
        formatter.dateFormat = "HH:mm:ss.SSS"
        let line = "[\(formatter.string(from: Date()))] \(message)"
        // stdout 镜像（无缓冲直写，重定向到文件也能实时落盘），便于命令行自动化采集日志
        FileHandle.standardOutput.write(Data((line + "\n").utf8))
        DispatchQueue.main.async {
            self.logs.append(line)
            if self.logs.count > 1000 {
                self.logs.removeFirst(self.logs.count - 1000)
            }
        }
    }

    // MARK: - 引擎与频道

    private func ensureEngine(appId: String) {
        if agoraKit != nil && currentAppId == appId {
            return
        }
        if agoraKit != nil {
            AgoraRtcEngineKit.destroy()
            agoraKit = nil
        }
        let config = AgoraRtcEngineConfig()
        config.appId = appId
        config.channelProfile = .liveBroadcasting
        agoraKit = AgoraRtcEngineKit.sharedEngine(with: config, delegate: self)
        currentAppId = appId
        log(L10n.t("log.engineCreated", in: L10n.current()))
    }

    /// wav 的多声道参数必须在 join 之前落到引擎上。
    func joinChannel(appId: String, token: String, channelId: String, uid: UInt, wav: WavFile) {
        guard !appId.isEmpty, !channelId.isEmpty else {
            log(L10n.t("log.emptyParams", in: L10n.current()))
            return
        }
        ensureEngine(appId: appId)
        guard let kit = agoraKit else {
            log(L10n.t("log.engineCreateFailed", in: L10n.current()))
            return
        }

        // 多声道私参：join 之前设置
        let payload = AgoraSenderManager.payloadType(forChannels: wav.channels)
        kit.setParameters("{\"che.audio.custom_payload_type\":\(payload)}")
        kit.setParameters("{\"che.audio.custom_channel_num\":\(wav.channels)}")
        kit.setParameters("{\"che.audio.custom_sample_rate\":\(wav.sampleRate)}")
        log(L10n.t("log.privateParams", in: L10n.current(), payload, wav.channels, wav.sampleRate))

        // 自定义音轨：MIXABLE（与官方 API-Examples CustomAudioSource 一致）。
        // enableAudioProcessing 保持默认 false，不经过 3A。
        let trackConfig = AgoraAudioTrackConfig()
        trackConfig.enableLocalPlayback = false
        trackId = Int(kit.createCustomAudioTrack(.mixable, config: trackConfig))
        log(L10n.t("log.customTrack", in: L10n.current(), trackId))

        kit.disableVideo()
        kit.enableAudio()
        kit.setClientRole(.broadcaster)

        let option = AgoraRtcChannelMediaOptions()
        option.clientRoleType = .broadcaster
        option.publishMicrophoneTrack = false
        option.publishCameraTrack = false
        option.autoSubscribeAudio = false
        option.publishCustomAudioTrack = true
        option.publishCustomAudioTrackId = trackId

        let joinToken = token.isEmpty ? nil : token
        let result = kit.joinChannel(byToken: joinToken, channelId: channelId, uid: uid, mediaOptions: option, joinSuccess: nil)
        if result == 0 {
            log(L10n.t("log.joinCallOk", in: L10n.current(), channelId, Int(uid)))
        } else {
            log(L10n.t("log.joinCallFail", in: L10n.current(), result))
        }
    }

    func leaveChannel() {
        stopPublishing()
        agoraKit?.leaveChannel(nil)
    }

    // MARK: - 发布（10ms pacer 推送 WAV PCM）

    func startPublishing(wav: WavFile) {
        guard let kit = agoraKit, isJoined, !isPublishing, trackId >= 0 else { return }
        let samplesPerFrame = wav.sampleRate / 100          // 10ms
        let bytesPerFrame = samplesPerFrame * wav.channels * 2
        let pcm = wav.pcmData
        let tid = trackId
        let channels = wav.channels
        let sampleRate = wav.sampleRate

        sendStopFlag = false
        DispatchQueue.main.async { self.sentFrames = 0 }
        log(L10n.t("log.publishStart", in: L10n.current(), channels, sampleRate))

        let thread = Thread { [weak self] in
            var offset = 0
            var frames = 0
            var nextPush = Date()
            while let self = self, !self.sendStopFlag {
                if offset + bytesPerFrame > pcm.count {
                    offset = 0   // 循环发送
                }
                // samples 语义（官方 API-Examples CustomAudioSource）：全部声道的总样本数
                // = 每声道样本数 × 声道数，与 Linux sendAudioPcmData 的每声道语义不同。
                //
                // 发送侧声道数钳制的版本实测（2026-10-12，本 app + Web 端逐声道音量峰值）：
                // 4.5.2 输入管线最高按 2 声道读取（协商正常，但仅前 2 声道有内容，CH2+ 静音）；
                // 4.6.0 / 4.6.2 / 4.7.0 已修复，8 声道内容全部送达。详见 README 版本实测矩阵。
                let ret = pcm.withUnsafeBytes { raw -> Int32 in
                    kit.pushExternalAudioFrameRawData(
                        UnsafeMutableRawPointer(mutating: raw.baseAddress!.advanced(by: offset)),
                        samples: samplesPerFrame * channels, sampleRate: sampleRate,
                        channels: channels, trackId: tid, timestamp: 0)
                }
                if ret != 0 {
                    self.log(L10n.t("log.pushError", in: L10n.current(), Int(ret)))
                }
                frames += 1
                offset += bytesPerFrame
                if frames % 100 == 0 {
                    DispatchQueue.main.async { self.sentFrames = frames }
                }
                nextPush = nextPush.addingTimeInterval(0.01)
                let sleep = nextPush.timeIntervalSinceNow
                if sleep > 0 {
                    Thread.sleep(forTimeInterval: sleep)
                } else if sleep < -0.1 {
                    nextPush = Date()   // 系统调度严重滞后时重置节奏
                }
            }
            DispatchQueue.main.async { self?.sentFrames = frames }
        }
        thread.name = "MultiopusSendPacer"
        thread.qualityOfService = QualityOfService.userInteractive
        sendThread = thread
        thread.start()
        DispatchQueue.main.async { self.isPublishing = true }
    }

    func stopPublishing() {
        sendStopFlag = true
        sendThread = nil
        if isPublishing {
            log(L10n.t("log.publishStop", in: L10n.current()))
        }
        DispatchQueue.main.async { self.isPublishing = false }
    }

    // MARK: - dump 私参

    func setFrameDump(enabled: Bool) {
        let lang = L10n.current()
        guard let kit = agoraKit else {
            log(L10n.t("log.dumpDeferred", in: lang))
            return
        }
        let ret = kit.setParameters("{\"che.audio.frame_dump\":\(enabled ? "true" : "false")}")
        var line = L10n.t("log.frameDump", in: lang, L10n.t(enabled ? "log.on" : "log.off", in: lang))
        if ret != 0 {
            line += L10n.t("log.setParamsRet", in: lang, Int(ret))
        }
        log(line)
    }
}

extension AgoraSenderManager: AgoraRtcEngineDelegate {
    func rtcEngine(_ engine: AgoraRtcEngineKit, didJoinChannel channel: String, withUid uid: UInt, elapsed: Int) {
        let lang = L10n.current()
        log(L10n.t("log.joinSuccess", in: lang, channel, Int(uid), elapsed))
        DispatchQueue.main.async {
            self.isJoined = true
            self.localUid = uid
            self.statusText = L10n.t("status.joined", in: lang)
        }
    }

    func rtcEngine(_ engine: AgoraRtcEngineKit, didLeaveChannelWith stats: AgoraChannelStats) {
        let lang = L10n.current()
        log(L10n.t("log.left", in: lang))
        DispatchQueue.main.async {
            self.isJoined = false
            self.localUid = 0
            self.statusText = L10n.t("status.notJoined", in: lang)
        }
    }

    func rtcEngine(_ engine: AgoraRtcEngineKit, connectionChangedTo state: AgoraConnectionState, reason: AgoraConnectionChangedReason) {
        let lang = L10n.current()
        let stateDesc: String
        switch state {
        case .disconnected: stateDesc = L10n.t("conn.disconnected", in: lang)
        case .connecting: stateDesc = L10n.t("conn.connecting", in: lang)
        case .connected: stateDesc = L10n.t("conn.connected", in: lang)
        case .reconnecting: stateDesc = L10n.t("conn.reconnecting", in: lang)
        case .failed: stateDesc = L10n.t("conn.failed", in: lang)
        @unknown default: stateDesc = L10n.t("conn.unknown", in: lang, Int(state.rawValue))
        }
        log(L10n.t("log.connState", in: lang, stateDesc, reason.rawValue))
        DispatchQueue.main.async {
            self.statusText = stateDesc
        }
    }

    func rtcEngine(_ engine: AgoraRtcEngineKit, didOccurError errorCode: AgoraErrorCode) {
        log(L10n.t("log.sdkError", in: L10n.current(), errorCode.rawValue))
    }
}
