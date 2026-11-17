import SwiftUI
import Foundation

/// 命令行自动化配置（供回归测试脚本驱动，UI 手动操作不受影响）。
/// 通过启动参数传入（映射进 UserDefaults）。模拟器示例：
///   xcrun simctl launch --console <device> com.agora.demo.MultiChannelAudioSenderApp \
///       -autoChannel test8ch -autoUid 12345 -autoWav 0 \
///       -autoJoin YES -autoPublish YES -autoQuitAfter 90
/// - autoWav: 内置素材下标（0=8ch, 1=6ch, 2=4ch, 3=3ch, 4=5ch, 5=7ch）
/// - autoWavPath: 自定义 WAV 文件路径（优先于 autoWav；须 48 kHz / 16bit / 1~8 声道。
///   受 iOS 沙盒限制：真机仅可读取 app 容器内路径；模拟器上主机绝对路径通常可读）
/// - autoQuitAfter: 秒数，>0 时到点自动退出 App（iOS 上实现为 exit(EXIT_SUCCESS)）
struct AutoRunConfig {
    var channel: String? = nil
    var uid: String? = nil
    var wavIndex: Int? = nil
    var wavPath: String? = nil
    var join = false
    var publish = false
    var quitAfter = 0

    static func fromDefaults(_ d: UserDefaults = .standard) -> AutoRunConfig {
        var c = AutoRunConfig()
        c.channel = d.string(forKey: "autoChannel")
        c.uid = d.string(forKey: "autoUid")
        if let s = d.string(forKey: "autoWav"), let i = Int(s) { c.wavIndex = i }
        c.wavPath = d.string(forKey: "autoWavPath")
        c.join = d.bool(forKey: "autoJoin")
        c.publish = d.bool(forKey: "autoPublish")
        c.quitAfter = d.integer(forKey: "autoQuitAfter")
        return c
    }
}

struct ContentView: View {
    @EnvironmentObject var senderManager: AgoraSenderManager
    @AppStorage("uiLanguage") private var uiLanguage = "en"

    @AppStorage("appId") private var appId = ""
    @State private var token = ""
    @State private var channelId = ""
    @State private var uidText = "0"

    @State private var dumpEnabled = false

    /// 内置素材（Resources 内）
    private let wavChoices: [(label: String, file: String)] = [
        ("8 ch — 8_Channel_ID_signed_16_bit_pcm.wav", "8_Channel_ID_signed_16_bit_pcm"),
        ("6 ch — 6_Channel_ID_48k.wav", "6_Channel_ID_48k"),
        ("4 ch — 4_Channel_ID_48k.wav", "4_Channel_ID_48k"),
        // 奇数声道素材：Web 端无法解码，仅 Native/Linux 端可验证（选中后界面有警示条）
        ("3 ch ⚠ — odd_3ch.wav", "odd_3ch"),
        ("5 ch ⚠ — odd_5ch.wav", "odd_5ch"),
        ("7 ch ⚠ — odd_7ch.wav", "odd_7ch"),
    ]
    @State private var selectedWav = 0
    @State private var wav: WavFile?
    /// 已加载素材名（幂等加载用）
    @State private var loadedWavName: String?
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
        // iPhone 竖屏：整页可滚动，功能区自上而下排布
        ScrollView {
            VStack(spacing: 12) {
                languageBar
                joinPanel
                sendPanel
                logPanel
            }
            .padding(12)
        }
        .onAppear {
            loadSelectedWav()   // 每次出现都确保素材已加载（幂等）
            autoRunOnce()
        }
        .onChange(of: selectedWav) { _ in loadSelectedWav() }
        .onChange(of: senderManager.isJoined) { joined in
            // 自动发布：仅在自动化 join 成功后触发一次（纯手动 join 不触发）
            if joined && autoCfg.join && autoCfg.publish && !autoPublished {
                autoPublished = true
                senderManager.log("[AutoRun] joined, auto publishing")
                publishButtonTapped()
            }
        }
    }

    // MARK: - 命令行自动化入口

    /// 进程级一次性守卫：onAppear 会随视图重建重复触发，自动化动作只允许执行一次
    private static var didAutoRun = false

    private func autoRunOnce() {
        guard !Self.didAutoRun else { return }
        Self.didAutoRun = true
        // 素材优先级：-autoWavPath（自定义路径）> -autoWav（内置下标）> 默认（下标 0）
        if let path = autoCfg.wavPath {
            loadCustomWav(path: path)
        } else if let idx = autoCfg.wavIndex, wavChoices.indices.contains(idx), idx != selectedWav {
            selectedWav = idx   // onChange 会触发加载
            loadSelectedWav()   // 显式再加载一次，保证紧随的 join 用到新素材（不依赖 onChange 时机）
        }
        if let ch = autoCfg.channel { channelId = ch }
        if let u = autoCfg.uid { uidText = u }
        if autoCfg.quitAfter > 0 {
            DispatchQueue.main.asyncAfter(deadline: .now() + .seconds(autoCfg.quitAfter)) {
                // iOS 无 NSApplication.terminate；回归自动化退出用 exit 结束进程。
                // 先停推流并离开频道：exit 后 SDK 的进程级清理会销毁 engine，若 pacer 仍在跑
                // 会在退出前打出一串 -7(NOT_INITIALIZED) 噪音日志
                senderManager.stopPublishing()
                senderManager.leaveChannel()
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                    exit(EXIT_SUCCESS)
                }
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
            .pickerStyle(.menu)
            .fixedSize()
        }
    }

    // MARK: - 分组容器（iOS 14 兼容）

    /// GroupBox 需要 iOS 15；此处用等价的「标题 + 圆角卡片」自绘容器替代，视觉/分组语义一致
    private func sectionBox<Content: View>(_ title: String, @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title)
                .font(.subheadline)
                .foregroundColor(.secondary)
            content()
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(10)
        .background(Color.gray.opacity(0.10))
        .cornerRadius(8)
    }

    // MARK: - Join panel

    private func fieldRow(_ label: String, placeholder: String, text: Binding<String>,
                          numeric: Bool = false, maxWidth: CGFloat = 520) -> some View {
        HStack {
            Text(label)
                .frame(width: 80, alignment: .trailing)
            TextField(placeholder, text: text)
                .textFieldStyle(.roundedBorder)
                .keyboardType(numeric ? .numberPad : .default)
                .frame(maxWidth: maxWidth)
                .disabled(senderManager.isJoined)
        }
    }

    private var joinPanel: some View {
        sectionBox(tr("join.section")) {
            VStack(alignment: .leading, spacing: 8) {
                fieldRow(tr("join.appId"), placeholder: tr("join.appId.placeholder"), text: $appId)
                fieldRow(tr("join.token"), placeholder: tr("join.token.placeholder"), text: $token)
                fieldRow(tr("join.channelId"), placeholder: tr("join.channelId.placeholder"), text: $channelId)
                fieldRow(tr("join.uid"), placeholder: tr("join.uid.placeholder"), text: $uidText,
                         numeric: true, maxWidth: 160)
                HStack {
                    Text(senderManager.statusText)
                        .foregroundColor(senderManager.isJoined ? .green : .secondary)
                        .lineLimit(1)
                    if senderManager.localUid != 0 {
                        Text(tr("join.localUid", Int(senderManager.localUid)))
                            .foregroundColor(.secondary)
                            .lineLimit(1)
                    }
                    Spacer()
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
        sectionBox(tr("send.section")) {
            VStack(alignment: .leading, spacing: 10) {
                HStack(spacing: 12) {
                    Text(tr("send.wav"))
                    Picker("", selection: $selectedWav) {
                        ForEach(0..<wavChoices.count, id: \.self) { i in
                            Text(wavChoices[i].label).tag(i)
                        }
                    }
                    .labelsHidden()
                    .pickerStyle(.menu)
                    .disabled(senderManager.isJoined)
                }

                if let wav = wav {
                    Text(tr("send.wavInfo",
                            "\(wav.sampleRate)", wav.channels, wav.bitsPerSample,
                            wav.durationSeconds, AgoraSenderManager.payloadType(forChannels: wav.channels)))
                        .font(.callout)
                        .foregroundColor(.secondary)
                }

                // 奇数声道（3/5/7）素材警示：Web SDK 无法解码，仅 Native/Linux 端可接收验证
                if let wav = wav, wav.channels > 2, wav.channels % 2 == 1 {
                    Text(tr("send.oddChannelWarning"))
                        .font(.callout)
                        .foregroundColor(.orange)
                        .lineLimit(nil)
                        .fixedSize(horizontal: false, vertical: true)
                        .padding(6)
                        .background(Color.orange.opacity(0.12))
                        .cornerRadius(6)
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

    /// textSelection 需要 iOS 15；iOS 14 下降级为纯展示（功能等价，仅不可选择复制）
    @ViewBuilder
    private var logContent: some View {
        if #available(iOS 15.0, *) {
            Text(senderManager.logs.joined(separator: "\n"))
                .textSelection(.enabled)
        } else {
            Text(senderManager.logs.joined(separator: "\n"))
        }
    }

    private var logPanel: some View {
        sectionBox(tr("log.section")) {
            ScrollViewReader { proxy in
                ScrollView {
                    logContent
                        .font(.system(size: 11, design: .monospaced))
                        .frame(maxWidth: .infinity, alignment: .leading)
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
        // 幂等：同名素材已加载则跳过（避免自动化路径 onAppear/onChange 双行日志）
        if loadedWavName == name, wav != nil { return }
        guard let url = Bundle.main.url(forResource: name, withExtension: "wav") else {
            senderManager.log(L10n.t("log.wavError", in: L10n.current(), "\(name).wav not in bundle"))
            wav = nil
            loadedWavName = nil
            return
        }
        do {
            let w = try WavFile.load(url: url)
            wav = w
            loadedWavName = name
            senderManager.log(L10n.t("log.wavLoaded", in: L10n.current(),
                                     "\(name).wav", w.sampleRate, w.channels, w.bitsPerSample, w.durationSeconds))
        } catch {
            wav = nil
            loadedWavName = nil
            senderManager.log(L10n.t("log.wavError", in: L10n.current(), error.localizedDescription))
        }
    }

    /// 自动化专用：从任意文件路径加载 WAV（-autoWavPath），不走内置素材幂等缓存。
    /// iOS 沙盒限制：真机仅可读取 app 容器内路径。
    private func loadCustomWav(path: String) {
        do {
            let w = try WavFile.load(url: URL(fileURLWithPath: path))
            wav = w
            loadedWavName = nil
            senderManager.log(L10n.t("log.wavLoaded", in: L10n.current(),
                                     URL(fileURLWithPath: path).lastPathComponent,
                                     w.sampleRate, w.channels, w.bitsPerSample, w.durationSeconds))
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
