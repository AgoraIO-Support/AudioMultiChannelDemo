import SwiftUI
import AppKit

/// 命令行自动化配置（供回归测试脚本驱动，UI 手动操作不受影响）。
/// 通过启动参数传入（映射进 UserDefaults），例如：
///   MultiChannelAudioSender -autoChannel test8ch -autoUid 12345 -autoWav 0 \
///       -autoJoin YES -autoPublish YES -autoQuitAfter 90
/// - autoWav: 素材下标（0=8ch, 1=6ch, 2=4ch）
/// - autoQuitAfter: 秒数，>0 时到点自动退出 App
struct AutoRunConfig {
    var channel: String? = nil
    var uid: String? = nil
    var wavIndex: Int? = nil
    var join = false
    var publish = false
    var quitAfter = 0

    static func fromDefaults(_ d: UserDefaults = .standard) -> AutoRunConfig {
        var c = AutoRunConfig()
        c.channel = d.string(forKey: "autoChannel")
        c.uid = d.string(forKey: "autoUid")
        if let s = d.string(forKey: "autoWav"), let i = Int(s) { c.wavIndex = i }
        c.join = d.bool(forKey: "autoJoin")
        c.publish = d.bool(forKey: "autoPublish")
        c.quitAfter = d.integer(forKey: "autoQuitAfter")
        return c
    }
}

struct ContentView: View {
    @EnvironmentObject var senderManager: AgoraSenderManager
    @AppStorage("uiLanguage") private var uiLanguage = "en"

    @AppStorage("appId") private var appId = "43f818097c124394821bd44598b7ca9d"
    @State private var token = ""
    @State private var channelId = ""
    @State private var uidText = "0"

    @State private var dumpEnabled = false

    /// 内置素材（Resources 内）
    private let wavChoices: [(label: String, file: String)] = [
        ("8 ch — 8_Channel_ID_signed_16_bit_pcm.wav", "8_Channel_ID_signed_16_bit_pcm"),
        ("6 ch — 6_Channel_ID_48k.wav", "6_Channel_ID_48k"),
        ("4 ch — 4_Channel_ID_48k.wav", "4_Channel_ID_48k"),
    ]
    @State private var selectedWav = 0
    @State private var wav: WavFile?
    /// 自动化配置与“已自动发布”守卫（防止 isJoined 反复触发重复发布）
    private let autoCfg = AutoRunConfig.fromDefaults()
    @State private var autoPublished = false

    private var lang: UILanguage {
        UILanguage(rawValue: uiLanguage) ?? .en
    }

    private func tr(_ key: String) -> String {
        L10n.t(key, in: lang)
    }

    private func tr(_ key: String, _ args: CVarArg...) -> String {
        String(format: L10n.t(key, in: lang), arguments: args)
    }

    var body: some View {
        VStack(spacing: 12) {
            languageBar
            joinPanel
            sendPanel
            logPanel
            Spacer(minLength: 0)
        }
        .padding(12)
        .onAppear { autoRunOnAppear() }
        .onChange(of: selectedWav) { _ in loadSelectedWav() }
        .onChange(of: senderManager.isJoined) { joined in
            // 自动发布：join 成功后触发一次
            if joined && autoCfg.publish && !autoPublished {
                autoPublished = true
                senderManager.log("[AutoRun] joined, auto publishing")
                publishButtonTapped()
            }
        }
    }

    // MARK: - 命令行自动化入口

    private func autoRunOnAppear() {
        if let idx = autoCfg.wavIndex, wavChoices.indices.contains(idx) {
            selectedWav = idx
        }
        loadSelectedWav()
        if let ch = autoCfg.channel { channelId = ch }
        if let u = autoCfg.uid { uidText = u }
        if autoCfg.quitAfter > 0 {
            DispatchQueue.main.asyncAfter(deadline: .now() + .seconds(autoCfg.quitAfter)) {
                NSApplication.shared.terminate(nil)
            }
        }
        if autoCfg.join {
            senderManager.log("[AutoRun] auto join: channel=\(channelId) uid=\(uidText)")
            joinButtonTapped()
        }
    }

    // MARK: - Language picker

    private var languageBar: some View {
        HStack {
            Spacer()
            Text(tr("language.label"))
            Picker("", selection: $uiLanguage) {
                ForEach(UILanguage.allCases) { language in
                    Text(language.displayName).tag(language.rawValue)
                }
            }
            .labelsHidden()
            .fixedSize()
        }
    }

    // MARK: - Join panel

    private var joinPanel: some View {
        GroupBox(tr("join.section")) {
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    Text(tr("join.appId"))
                        .frame(width: 80, alignment: .trailing)
                    TextField(tr("join.appId.placeholder"), text: $appId)
                        .textFieldStyle(.roundedBorder)
                        .frame(maxWidth: 520)
                        .disabled(senderManager.isJoined)
                }
                HStack {
                    Text(tr("join.token"))
                        .frame(width: 80, alignment: .trailing)
                    TextField(tr("join.token.placeholder"), text: $token)
                        .textFieldStyle(.roundedBorder)
                        .frame(maxWidth: 520)
                        .disabled(senderManager.isJoined)
                }
                HStack {
                    Text(tr("join.channelId"))
                        .frame(width: 80, alignment: .trailing)
                    TextField(tr("join.channelId.placeholder"), text: $channelId)
                        .textFieldStyle(.roundedBorder)
                        .frame(maxWidth: 520)
                        .disabled(senderManager.isJoined)
                }
                HStack {
                    Text(tr("join.uid"))
                        .frame(width: 80, alignment: .trailing)
                    TextField(tr("join.uid.placeholder"), text: $uidText)
                        .textFieldStyle(.roundedBorder)
                        .frame(maxWidth: 160)
                        .disabled(senderManager.isJoined)
                    Spacer()
                    Text(senderManager.statusText)
                        .foregroundColor(senderManager.isJoined ? .green : .secondary)
                    if senderManager.localUid != 0 {
                        Text(tr("join.localUid", Int(senderManager.localUid)))
                            .foregroundColor(.secondary)
                    }
                    Button(senderManager.isJoined ? tr("join.button.leave") : tr("join.button.join")) {
                        joinButtonTapped()
                    }
                    .disabled(!senderManager.isJoined && (appId.isEmpty || channelId.isEmpty || wav == nil))
                }
                Toggle(tr("join.dumpToggle"), isOn: $dumpEnabled)
                    .onChange(of: dumpEnabled) { newValue in
                        senderManager.setFrameDump(enabled: newValue)
                    }
            }
            .padding(6)
        }
    }

    // MARK: - 发送面板

    private var sendPanel: some View {
        GroupBox(tr("send.section")) {
            VStack(alignment: .leading, spacing: 10) {                HStack(spacing: 12) {
                    Text(tr("send.wav"))
                    Picker("", selection: $selectedWav) {
                        ForEach(0..<wavChoices.count, id: \.self) { i in
                            Text(wavChoices[i].label).tag(i)
                        }
                    }
                    .labelsHidden()
                    .fixedSize()
                    .disabled(senderManager.isJoined)
                }

                if let wav = wav {
                    Text(tr("send.wavInfo",
                            "\(wav.sampleRate)", wav.channels, wav.bitsPerSample,
                            wav.durationSeconds, AgoraSenderManager.payloadType(forChannels: wav.channels)))
                        .font(.callout)
                        .foregroundColor(.secondary)
                }

                HStack(spacing: 12) {
                    Button(senderManager.isPublishing ? tr("send.stop") : tr("send.publish")) {
                        publishButtonTapped()
                    }
                    .disabled(!senderManager.isJoined || wav == nil)
                    if senderManager.isPublishing || senderManager.sentFrames > 0 {
                        Text(tr("send.stats", senderManager.sentFrames, Double(senderManager.sentFrames) * 0.01))
                            .font(.callout)
                            .foregroundColor(.secondary)
                    } else if !senderManager.isJoined {
                        Text(tr("send.needJoin"))
                            .font(.callout)
                            .foregroundColor(.secondary)
                    }
                }

                Text(tr("send.loopHint"))
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(6)
        }
    }

    // MARK: - Log panel

    private var logPanel: some View {
        GroupBox(tr("log.section")) {
            ScrollViewReader { proxy in
                ScrollView {
                    Text(senderManager.logs.joined(separator: "\n"))
                        .font(.system(size: 11, design: .monospaced))
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .textSelection(.enabled)
                        .padding(4)
                        .id("logBottom")
                }
                .onChange(of: senderManager.logs.count) { _ in
                    proxy.scrollTo("logBottom", anchor: .bottom)
                }
            }
            .frame(height: 160)
        }
    }

    // MARK: - Actions

    private func loadSelectedWav() {
        let name = wavChoices[selectedWav].file
        guard let url = Bundle.main.url(forResource: name, withExtension: "wav") else {
            senderManager.log(L10n.t("log.wavError", in: L10n.current(), "\(name).wav not in bundle"))
            wav = nil
            return
        }
        do {
            let w = try WavFile.load(url: url)
            wav = w
            senderManager.log(L10n.t("log.wavLoaded", in: L10n.current(),
                                     "\(name).wav", w.sampleRate, w.channels, w.bitsPerSample, w.durationSeconds))
        } catch {
            wav = nil
            senderManager.log(L10n.t("log.wavError", in: L10n.current(), error.localizedDescription))
        }
    }

    private func joinButtonTapped() {
        if senderManager.isJoined {
            senderManager.leaveChannel()
            return
        }
        guard let wav = wav else { return }
        let uid = UInt(uidText.trimmingCharacters(in: .whitespaces)) ?? 0
        senderManager.joinChannel(appId: appId.trimmingCharacters(in: .whitespaces),
                                  token: token.trimmingCharacters(in: .whitespaces),
                                  channelId: channelId.trimmingCharacters(in: .whitespaces),
                                  uid: uid, wav: wav)
        if dumpEnabled {
            senderManager.setFrameDump(enabled: true)
        }
    }

    private func publishButtonTapped() {
        if senderManager.isPublishing {
            senderManager.stopPublishing()
        } else if let wav = wav {
            senderManager.startPublishing(wav: wav)
        }
    }
}
