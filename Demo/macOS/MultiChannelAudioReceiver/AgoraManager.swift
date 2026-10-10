import Foundation
import AppKit
import AgoraRtcKit

struct RemoteUser: Identifiable, Equatable {
    let uid: UInt
    var id: UInt { uid }
    var receivedBitrateKbps: Int = 0
    var hasVideo = false
}

class AgoraManager: NSObject, ObservableObject {
    @Published var isJoined = false
    @Published var localUid: UInt = 0
    @Published var remoteUsers: [RemoteUser] = []
    @Published var logs: [String] = []
    @Published var statusText = L10n.t("status.notJoined", in: L10n.current())

    private var agoraKit: AgoraRtcEngineKit?
    private var currentAppId = ""

    override init() {
        super.init()
    }

    deinit {
        agoraKit?.leaveChannel(nil)
        AgoraRtcEngineKit.destroy()
    }

    // MARK: - 日志

    func log(_ message: String) {
        let formatter = DateFormatter()
        formatter.dateFormat = "HH:mm:ss.SSS"
        let line = "[\(formatter.string(from: Date()))] \(message)"
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

    func joinChannel(appId: String, token: String, channelId: String, uid: UInt) {
        guard !appId.isEmpty, !channelId.isEmpty else {
            log(L10n.t("log.emptyParams", in: L10n.current()))
            return
        }
        ensureEngine(appId: appId)
        guard let kit = agoraKit else {
            log(L10n.t("log.engineCreateFailed", in: L10n.current()))
            return
        }

        kit.enableVideo()
        kit.enableAudio()
        kit.setClientRole(.audience)

        let option = AgoraRtcChannelMediaOptions()
        option.clientRoleType = .audience
        option.publishMicrophoneTrack = false
        option.publishCameraTrack = false
        option.autoSubscribeAudio = true
        option.autoSubscribeVideo = true

        let joinToken = token.isEmpty ? nil : token
        let result = kit.joinChannel(byToken: joinToken, channelId: channelId, uid: uid, mediaOptions: option, joinSuccess: nil)
        if result == 0 {
            log(L10n.t("log.joinCallOk", in: L10n.current(), channelId, Int(uid)))
        } else {
            log(L10n.t("log.joinCallFail", in: L10n.current(), result))
        }
    }

    func leaveChannel() {
        agoraKit?.leaveChannel(nil)
    }

    // MARK: - 远端视频渲染

    /// 把远端 uid 的视频渲染绑定到指定 NSView（切换 uid 时用同一 view 重新绑定即可）。
    func setupRemoteVideo(remoteUid: UInt, view: NSView) {
        guard let kit = agoraKit else { return }
        let canvas = AgoraRtcVideoCanvas()
        canvas.uid = remoteUid
        canvas.view = view
        canvas.renderMode = .fit
        let ret = kit.setupRemoteVideo(canvas)
        var line = L10n.t("log.setupRemoteVideo", in: L10n.current(), Int(remoteUid))
        if ret != 0 {
            line += L10n.t("log.setupRet", in: L10n.current(), Int(ret))
        }
        log(line)
    }

    // MARK: - 多声道私参

    /// 逐声道调整远端解码音量（1-based 声道序号，volume 0~100 为百分比）。
    /// 选择收听的声道 = 目标声道设 100，其余设 0。
    func setChannelVolume(remoteUid: UInt, index: Int, volume: Int) {
        guard let kit = agoraKit else { return }
        let param = "{\"che.audio.neteq.adjust_decoded_volume\":{\"remoteUid\":\(remoteUid),\"index\":\(index),\"volume\":\(volume)}}"
        let ret = kit.setParameters(param)
        let lang = L10n.current()
        var line = L10n.t("log.setChannelVolume", in: lang, Int(remoteUid), index, volume)
        if ret != 0 {
            line += L10n.t("log.setParamsRet", in: lang, Int(ret))
        }
        log(line)
    }

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

extension AgoraManager: AgoraRtcEngineDelegate {
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
            self.remoteUsers = []
            self.statusText = L10n.t("status.notJoined", in: lang)
        }
    }

    func rtcEngine(_ engine: AgoraRtcEngineKit, didJoinedOfUid uid: UInt, elapsed: Int) {
        // 观众加入也会触发此回调；远端列表只收录发布音频的 host，添加见 remoteAudioStateChangedOfUid。
        log(L10n.t("log.remoteJoined", in: L10n.current(), Int(uid)))
    }

    func rtcEngine(_ engine: AgoraRtcEngineKit, didOfflineOfUid uid: UInt, reason: AgoraUserOfflineReason) {
        // 主播离开或转为观众时都会触发，正好作为列表移除时机。
        log(L10n.t("log.remoteLeft", in: L10n.current(), Int(uid), reason.rawValue))
        DispatchQueue.main.async {
            self.remoteUsers.removeAll { $0.uid == uid }
        }
    }

    func rtcEngine(_ engine: AgoraRtcEngineKit, remoteAudioStateChangedOfUid uid: UInt, state: AgoraAudioRemoteState, reason: AgoraAudioRemoteReason, elapsed: Int) {
        // starting/decoding 表示对端音频正在被实际接收，对方即在发布音频的 host；
        // stopped（如对端静音）不移除——静音的 host 仍是 host，可能恢复。
        guard state == .starting || state == .decoding else { return }
        DispatchQueue.main.async {
            if !self.remoteUsers.contains(where: { $0.uid == uid }) {
                self.remoteUsers.append(RemoteUser(uid: uid))
                self.log(L10n.t("log.remoteHostPublishing", in: L10n.current(), Int(uid)))
            }
        }
    }

    func rtcEngine(_ engine: AgoraRtcEngineKit, remoteVideoStateChangedOfUid uid: UInt, state: AgoraVideoRemoteState, reason: AgoraVideoRemoteReason, elapsed: Int) {
        // starting/decoding/frozen 表示对端视频正在发布；stopped/failed 表示不再发布。
        switch state {
        case .starting, .decoding:
            DispatchQueue.main.async {
                if let i = self.remoteUsers.firstIndex(where: { $0.uid == uid }), !self.remoteUsers[i].hasVideo {
                    self.remoteUsers[i].hasVideo = true
                    self.log(L10n.t("log.remoteVideoPublishing", in: L10n.current(), Int(uid)))
                }
            }
        case .stopped, .failed:
            DispatchQueue.main.async {
                if let i = self.remoteUsers.firstIndex(where: { $0.uid == uid }), self.remoteUsers[i].hasVideo {
                    self.remoteUsers[i].hasVideo = false
                    self.log(L10n.t("log.remoteVideoStopped", in: L10n.current(), Int(uid)))
                }
            }
        default:
            break
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

    func rtcEngine(_ engine: AgoraRtcEngineKit, remoteAudioStats stats: AgoraRtcRemoteAudioStats) {
        DispatchQueue.main.async {
            if let index = self.remoteUsers.firstIndex(where: { $0.uid == stats.uid }) {
                self.remoteUsers[index].receivedBitrateKbps = Int(stats.receivedBitrate)
            }
        }
    }
}
