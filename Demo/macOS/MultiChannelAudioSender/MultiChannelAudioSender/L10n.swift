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
        "join.section": [.en: "Join Channel (host role, publish multi-channel audio)", .zhHans: "加入频道（主播角色，发布多声道音频）"],
        "join.appId": [.en: "App ID"],
        "join.appId.placeholder": [.en: "Agora App ID"],
        "join.token": [.en: "Token"],
        "join.token.placeholder": [.en: "Optional; leave empty for projects without certificate", .zhHans: "可选，无证书项目留空"],
        "join.channelId": [.en: "Channel ID", .zhHans: "频道 ID"],
        "join.channelId.placeholder": [.en: "Channel name, same as the receiver", .zhHans: "频道名，与接收端一致"],
        "join.uid": [.en: "UID"],
        "join.uid.placeholder": [.en: "0 = auto-assign", .zhHans: "0 = 自动分配"],
        "join.localUid": [.en: "Local UID: %ld", .zhHans: "本端 UID: %ld"],
        "join.button.join": [.en: "Join", .zhHans: "加入频道"],
        "join.button.leave": [.en: "Leave", .zhHans: "离开频道"],
        "join.dumpToggle": [.en: "Dump audio frames (che.audio.frame_dump)", .zhHans: "Dump 音频帧（che.audio.frame_dump）"],

        // 发送面板
        "send.section": [.en: "Multi-Channel WAV Publish (pushExternalAudioFrameRawData)", .zhHans: "多声道 WAV 发布（pushExternalAudioFrameRawData）"],
        "send.wav": [.en: "WAV file", .zhHans: "WAV 素材"],
        "send.wavInfo": [.en: "%@ Hz · %ld ch · %ld-bit · %.1f s · payload %ld", .zhHans: "%@ Hz · %ld 声道 · %ld 位 · %.1f 秒 · payload %ld"],
        "send.publish": [.en: "Publish", .zhHans: "发布"],
        "send.stop": [.en: "Stop", .zhHans: "停止发布"],
        "send.stats": [.en: "Sent %ld frames (%.1f s of audio)", .zhHans: "已发送 %ld 帧（%.1f 秒音频）"],
        "send.needJoin": [.en: "Join a channel first", .zhHans: "请先加入频道"],
        "send.loopHint": [.en: "The file loops until stopped. Frames are pushed at 10 ms per frame.", .zhHans: "文件循环发送直到停止；按 10 ms/帧 节奏推入。"],
        "send.oddChannelWarning": [.en: "⚠ Odd channel count (3/5/7): can be received/verified by Native (macOS) / Linux SDK receivers only — the Web SDK cannot decode odd-channel streams (Web supports even 4/6/8 only).", .zhHans: "⚠ 奇数声道（3/5/7）：仅 Native（macOS）/ Linux SDK 端可接收验证；Web SDK 无法解码奇数声道流（仅支持 4/6/8 偶数声道）。"],

        // 日志面板
        "log.section": [.en: "Log", .zhHans: "日志"],

        // AgoraSenderManager 状态与日志
        "status.notJoined": [.en: "Not in channel", .zhHans: "未加入频道"],
        "status.joined": [.en: "Joined channel", .zhHans: "已加入频道"],
        "log.engineCreated": [.en: "RTC engine created", .zhHans: "RTC 引擎已创建"],
        "log.emptyParams": [.en: "App ID and Channel ID must not be empty", .zhHans: "App ID 与频道 ID 不能为空"],
        "log.engineCreateFailed": [.en: "Failed to create the engine; check the App ID", .zhHans: "引擎创建失败，请检查 App ID"],
        "log.joinCallOk": [.en: "joinChannel call succeeded: channel=%@, uid=%ld, role=broadcaster (publish custom audio track)", .zhHans: "joinChannel 调用成功: channel=%@, uid=%ld, 角色=主播（发布自定义音轨）"],
        "log.joinCallFail": [.en: "joinChannel call failed: %ld", .zhHans: "joinChannel 调用失败: %ld"],
        "log.joinSuccess": [.en: "Joined channel: %@, local uid=%ld, elapsed=%ldms", .zhHans: "加入频道成功: %@, 本端 uid=%ld, elapsed=%ldms"],
        "log.left": [.en: "Left channel", .zhHans: "已离开频道"],
        "log.privateParams": [.en: "Multi-channel params: payload=%ld channels=%ld sampleRate=%ld (set before join)", .zhHans: "多声道私参: payload=%ld channels=%ld sampleRate=%ld（join 前设置）"],
        "log.customTrack": [.en: "Custom audio track created (mixable, no 3A), trackId=%ld", .zhHans: "自定义音轨已创建（mixable，不经过 3A），trackId=%ld"],
        "log.wavLoaded": [.en: "WAV loaded: %@ (%ld Hz, %ld ch, %ld-bit, %.1f s)", .zhHans: "WAV 已加载: %@（%ld Hz，%ld 声道，%ld 位，%.1f 秒）"],
        "log.wavError": [.en: "WAV load failed: %@", .zhHans: "WAV 加载失败: %@"],
        "log.wavTooShort": [.en: "WAV too short: %ld bytes < one 10 ms frame (%ld bytes); not publishing", .zhHans: "WAV 过短：%ld 字节不足一帧 10 ms（%ld 字节），未发布"],
        "log.publishStart": [.en: "Publishing: %ld ch @ %ld Hz, 10 ms/frame, loop", .zhHans: "开始发布: %ld 声道 @ %ld Hz，10 ms/帧，循环"],
        "log.publishStop": [.en: "Publishing stopped", .zhHans: "已停止发布"],
        "log.pushError": [.en: "pushExternalAudioFrameRawData returned %ld", .zhHans: "pushExternalAudioFrameRawData 返回 %ld"],
        "log.frameDump": [.en: "Frame dump: %@", .zhHans: "音频帧 Dump: %@"],
        "log.on": [.en: "on", .zhHans: "开启"],
        "log.off": [.en: "off", .zhHans: "关闭"],
        "log.dumpDeferred": [.en: "Engine not created yet; the dump setting will be applied after joining", .zhHans: "引擎未创建，Dump 状态将在加入频道后应用"],
        "log.setParamsRet": [.en: " (setParameters returned %ld)", .zhHans: " (setParameters 返回 %ld)"],
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
