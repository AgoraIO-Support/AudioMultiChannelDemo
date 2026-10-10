import SwiftUI

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
        .onAppear { loadSelectedWav() }
        .onChange(of: selectedWav) { _ in loadSelectedWav() }
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
