import Foundation

enum UILanguage: String, CaseIterable, Identifiable {
    case en
    case zhHans = "zh-Hans"

    var id: String { rawValue }

    var displayName: String {
        switch self {
        case .en: return "English"
        case .zhHans: return "中文"
        }
    }
}

enum L10n {
    /// 读取 @AppStorage("uiLanguage") 持久化的当前语言（未设置时默认英文）。
    static func current() -> UILanguage {
        let raw = UserDefaults.standard.string(forKey: "uiLanguage") ?? UILanguage.en.rawValue
        return UILanguage(rawValue: raw) ?? .en
    }

    static func t(_ key: String, in lang: UILanguage) -> String {
        table[key]?[lang] ?? table[key]?[.en] ?? key
    }

    static func t(_ key: String, in lang: UILanguage, _ args: CVarArg...) -> String {
        String(format: t(key, in: lang), arguments: args)
    }

    private static let table: [String: [UILanguage: String]] = [
        // 语言选择器
        "language.label": [.en: "Language", .zhHans: "语言"],

        // 加入频道面板
        "join.section": [.en: "Join Channel (audience role, receive-only)", .zhHans: "加入频道（观众角色，纯接收）"],
        "join.appId": [.en: "App ID"],
        "join.appId.placeholder": [.en: "Agora App ID"],
        "join.token": [.en: "Token"],
        "join.token.placeholder": [.en: "Optional; leave empty for projects without certificate", .zhHans: "可选，无证书项目留空"],
        "join.channelId": [.en: "Channel ID", .zhHans: "频道 ID"],
        "join.channelId.placeholder": [.en: "Channel name, same as the sender", .zhHans: "频道名，与发送端一致"],
        "join.uid": [.en: "UID"],
        "join.uid.placeholder": [.en: "0 = auto-assign", .zhHans: "0 = 自动分配"],
        "join.localUid": [.en: "Local UID: %ld", .zhHans: "本端 UID: %ld"],
        "join.button.join": [.en: "Join", .zhHans: "加入频道"],
        "join.button.leave": [.en: "Leave", .zhHans: "离开频道"],
        "join.dumpToggle": [.en: "Dump audio frames (che.audio.frame_dump)", .zhHans: "Dump 音频帧（che.audio.frame_dump）"],

        // 远端多声道控制面板
        "mc.section": [.en: "Remote Multi-Channel Control (che.audio.neteq.adjust_decoded_volume)", .zhHans: "远端多声道控制（che.audio.neteq.adjust_decoded_volume）"],
        "mc.audioOnly": [.en: "Audio only (no video from this host)", .zhHans: "仅音频（该 host 未发布视频）"],
        "mc.remoteUser": [.en: "Remote user", .zhHans: "远端用户"],
        "mc.noSelection": [.en: "Not selected", .zhHans: "未选择"],
        "mc.recvBitrate": [.en: "Audio recv: %ld kbps", .zhHans: "音频接收码率: %ld kbps"],
        "mc.videoRecvBitrate": [.en: "Video recv: %ld kbps", .zhHans: "视频接收码率: %ld kbps"],
        "mc.monitorWindow": [.en: "last ~2 min, kbps", .zhHans: "最近约 2 分钟，kbps"],
        "mc.playbackSection": [.en: "Channel playback (per-channel volume)", .zhHans: "声道播放（逐声道音量）"],
        "mc.channelCount": [.en: "Channels", .zhHans: "声道数"],
        "mc.noRemote": [.en: "No remote user yet. Start a sender in the same channel first (e.g. sample_send_wav_multiopus in Demo/linux sending 8 channels).",
                        .zhHans: "暂无远端用户。请先在同一频道启动一个发送端（例如 Demo/linux 的 sample_send_wav_multiopus 8 声道发送）。"],
        "mc.channelRow": [.en: "Channel %ld", .zhHans: "声道 %ld"],
        "mc.countHint": [.en: "The channel count selector only controls how many rows are shown: choose 2 for a normal stereo stream, or the matching count for a 3~8-channel OPUSMC stream.",
                         .zhHans: "声道数选择器仅控制显示的行数：验证普通立体声流时选 2，验证 3~8 声道 OPUSMC 流时选对应声道数。"],

        // 监控面板
        "monitor.section": [.en: "Monitor — UID %ld", .zhHans: "监控 — UID %ld"],
        "monitor.section.none": [.en: "Monitor", .zhHans: "监控"],

        // 日志面板
        "log.section": [.en: "Log", .zhHans: "日志"],

        // AgoraManager 状态与日志
        "status.notJoined": [.en: "Not in channel", .zhHans: "未加入频道"],
        "status.joined": [.en: "Joined channel", .zhHans: "已加入频道"],
        "log.engineCreated": [.en: "RTC engine created", .zhHans: "RTC 引擎已创建"],
        "log.emptyParams": [.en: "App ID and Channel ID must not be empty", .zhHans: "App ID 与频道 ID 不能为空"],
        "log.engineCreateFailed": [.en: "Failed to create the engine; check the App ID", .zhHans: "引擎创建失败，请检查 App ID"],
        "log.joinCallOk": [.en: "joinChannel call succeeded: channel=%@, uid=%ld, role=audience (receive-only)", .zhHans: "joinChannel 调用成功: channel=%@, uid=%ld, 角色=观众（纯接收）"],
        "log.joinCallFail": [.en: "joinChannel call failed: %ld", .zhHans: "joinChannel 调用失败: %ld"],
        "log.setChannelVolume": [.en: "Channel volume: uid=%ld ch%ld -> %ld%%", .zhHans: "声道音量: uid=%ld 声道%ld -> %ld%%"],
        "log.setParamsRet": [.en: " (setParameters returned %ld)", .zhHans: " (setParameters 返回 %ld)"],
        "log.dumpDeferred": [.en: "Engine not created yet; the dump setting will be applied after joining", .zhHans: "引擎未创建，Dump 状态将在加入频道后应用"],
        "log.frameDump": [.en: "Frame dump: %@", .zhHans: "音频帧 Dump: %@"],
        "log.on": [.en: "on", .zhHans: "开启"],
        "log.off": [.en: "off", .zhHans: "关闭"],
        "log.joinSuccess": [.en: "Joined channel: %@, local uid=%ld, elapsed=%ldms", .zhHans: "加入频道成功: %@, 本端 uid=%ld, elapsed=%ldms"],
        "log.left": [.en: "Left channel", .zhHans: "已离开频道"],
        "log.remoteJoined": [.en: "Remote user joined (audience or host): uid=%ld", .zhHans: "远端用户加入（观众或主播）: uid=%ld"],
        "log.remoteHostPublishing": [.en: "Remote host is publishing audio: uid=%ld", .zhHans: "远端 host 开始发布音频: uid=%ld"],
        "log.remoteVideoPublishing": [.en: "Remote host is publishing video: uid=%ld", .zhHans: "远端 host 开始发布视频: uid=%ld"],
        "log.remoteVideoStopped": [.en: "Remote host stopped video: uid=%ld", .zhHans: "远端 host 停止发布视频: uid=%ld"],
        "log.setupRemoteVideo": [.en: "Bind remote video view: uid=%ld", .zhHans: "绑定远端视频视图: uid=%ld"],
        "log.setupRet": [.en: " (setupRemoteVideo returned %ld)", .zhHans: " (setupRemoteVideo 返回 %ld)"],
        "log.remoteLeft": [.en: "Remote user left: uid=%ld, reason=%ld", .zhHans: "远端用户离开: uid=%ld, reason=%ld"],
        "log.connState": [.en: "Connection state: %@, reason=%ld", .zhHans: "连接状态: %@, reason=%ld"],
        "conn.disconnected": [.en: "Disconnected", .zhHans: "已断开"],
        "conn.connecting": [.en: "Connecting", .zhHans: "连接中"],
        "conn.connected": [.en: "Connected", .zhHans: "已连接"],
        "conn.reconnecting": [.en: "Reconnecting", .zhHans: "重连中"],
        "conn.failed": [.en: "Connection failed", .zhHans: "连接失败"],
        "conn.unknown": [.en: "Unknown state (%ld)", .zhHans: "未知状态(%ld)"],
        "log.sdkError": [.en: "SDK error: %ld", .zhHans: "SDK 错误: %ld"],
    ]
}
