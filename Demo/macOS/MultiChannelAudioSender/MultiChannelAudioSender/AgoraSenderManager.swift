import Foundation
import AgoraRtcKit

/// 发送侧管理：观众/主播加入、多声道私参、自定义音轨 + 10ms pacer 推送 WAV PCM。
///
/// 与 Linux `sample_send_pcm_multiopus` 同口径：
/// - 私参 che.audio.custom_payload_type / custom_channel_num / custom_sample_rate 在 join 之前设置；
/// - payload type 映射：≤4ch → 123，≤6ch → 124，否则 → 125；
/// - 自定义音轨用 mixable（不经过 3A）；
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
    private let sendLock = NSLock()
    private var sendGeneration = 0   // pacer 代际：每次 start/stop 递增；pacer 仅在自己代际内运行

    private func bumpSendGeneration() -> Int {
        sendLock.lock()
        defer { sendLock.unlock() }
        sendGeneration += 1
        return sendGeneration
    }

    private func isCurrentGeneration(_ gen: Int) -> Bool {
        sendLock.lock()
        defer { sendLock.unlock() }
        return sendGeneration == gen
    }

    override init() {
        super.init()
    }

    deinit {
        stopPublishing()
        agoraKit?.leaveChannel(nil)
        AgoraRtcEngineKit.destroy()
    }

    /// payload type 与声道数映射（spec 场景4.1）：≤4 → 123，5/6 → 124，7/8 → 125
    ///（实现为整档比较：1~2 声道同样落入 123 + custom_channel_num=1/2，该组合未纳入版本实测矩阵）
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

        // 多声道私参：join 之前设置；任一失败则中止 join（继续 join 会按错误协商）
        let payload = AgoraSenderManager.payloadType(forChannels: wav.channels)
        let paramRets = [
            kit.setParameters("{\"che.audio.custom_payload_type\":\(payload)}"),
            kit.setParameters("{\"che.audio.custom_channel_num\":\(wav.channels)}"),
            kit.setParameters("{\"che.audio.custom_sample_rate\":\(wav.sampleRate)}")
        ]
        if let failed = paramRets.first(where: { $0 != 0 }) {
            log(L10n.t("log.privateParams", in: L10n.current(), payload, wav.channels, wav.sampleRate)
                + L10n.t("log.setParamsRet", in: L10n.current(), Int(failed)))
            return
        }
        log(L10n.t("log.privateParams", in: L10n.current(), payload, wav.channels, wav.sampleRate))

        // 自定义音轨：MIXABLE（与官方 API-Examples CustomAudioSource 一致）。
        // enableAudioProcessing 保持默认 false，不经过 3A。
        if trackId >= 0 {
            // 防御：上一次 join 创建的音轨未销毁时先销毁，避免孤儿音轨累积
            kit.destroyCustomAudioTrack(trackId)
            trackId = -1
        }
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
        if trackId >= 0 {
            agoraKit?.destroyCustomAudioTrack(trackId)
            trackId = -1
        }
    }

    // MARK: - 发布（10ms pacer 推送 WAV PCM）

    func startPublishing(wav: WavFile) {
        guard let kit = agoraKit, isJoined, !isPublishing, trackId >= 0 else { return }
        let samplesPerFrame = wav.sampleRate / 100          // 10ms
        let bytesPerFrame = samplesPerFrame * wav.channels * 2
        let pcm = wav.pcmData
        // 不足一帧的畸形/过短文件：直接拒绝，避免 pacer 越出 Data 末尾读取
        guard pcm.count >= bytesPerFrame else {
            log(L10n.t("log.wavTooShort", in: L10n.current(), pcm.count, bytesPerFrame))
            return
        }
        let tid = trackId
        let channels = wav.channels
        let sampleRate = wav.sampleRate

        // 代际递增：任何旧 pacer 立即失效退出（快速 Stop→Publish 也不会出现双 pacer）
        let gen = bumpSendGeneration()
        if Thread.isMainThread { sentFrames = 0 } else { DispatchQueue.main.async { self.sentFrames = 0 } }
        log(L10n.t("log.publishStart", in: L10n.current(), channels, sampleRate))

        let thread = Thread { [weak self] in
            var offset = 0
            var frames = 0
            var nextPush = Date()
            // 推流失败日志节流：失败罕见，但每 10ms 一条会迅速冲掉 1000 行 UI 日志
            var pushErrorCount = 0
            var lastPushErrorLog = Date.distantPast
            while let self = self, self.isCurrentGeneration(gen) {
                if offset + bytesPerFrame > pcm.count {
                    offset = 0   // 循环发送
                }
                // samples 语义（官方 API-Examples CustomAudioSource）：全部声道的总样本数
                // = 每声道样本数 × 声道数，与 Linux sendAudioPcmData 的每声道语义不同。
                //
                // 发送侧声道数钳制的版本实测（2026-10-12，本 app + Web 端逐声道音量峰值）：
                // 4.5.2 输入管线最高按 2 声道读取（协商正常，但仅前 2 声道有内容，CH2+ 静音）；
                // 4.5.3 起已修复（4.5.3/4.6.0/4.6.2/4.7.0 实测 8 声道全通）。详见 README 版本实测矩阵。
                let ret = pcm.withUnsafeBytes { raw -> Int32 in
                    kit.pushExternalAudioFrameRawData(
                        UnsafeMutableRawPointer(mutating: raw.baseAddress!.advanced(by: offset)),
                        samples: samplesPerFrame * channels, sampleRate: sampleRate,
                        channels: channels, trackId: tid, timestamp: 0)
                }
                if ret != 0 {
                    pushErrorCount += 1
                    let now = Date()
                    if pushErrorCount == 1 || now.timeIntervalSince(lastPushErrorLog) >= 1.0 {
                        lastPushErrorLog = now
                        let key = pushErrorCount == 1 ? "log.pushError" : "log.pushErrorThrottled"
                        self.log(L10n.t(key, in: L10n.current(), Int(ret), pushErrorCount))
                    }
                } else if pushErrorCount > 0 {
                    self.log(L10n.t("log.pushRecovered", in: L10n.current(), pushErrorCount))
                    pushErrorCount = 0
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
        // 主线程同步翻转（连点 Stop→Publish 时不会因异步延迟吃掉点击）
        if Thread.isMainThread { isPublishing = true } else { DispatchQueue.main.async { self.isPublishing = true } }
    }

    func stopPublishing() {
        bumpSendGeneration()   // 当前 pacer 退出
        sendThread = nil
        if isPublishing {
            log(L10n.t("log.publishStop", in: L10n.current()))
        }
        if Thread.isMainThread { isPublishing = false } else { DispatchQueue.main.async { self.isPublishing = false } }
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
