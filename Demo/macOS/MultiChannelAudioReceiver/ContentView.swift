import SwiftUI
import AppKit

/// 远端视频渲染视图：把选定 uid 的视频绑定到一个 NSView（AgoraRtcVideoCanvas）。
struct RemoteVideoView: NSViewRepresentable {
    let uid: UInt
    @EnvironmentObject var agoraManager: AgoraManager

    final class Coordinator {
        var boundUid: UInt?
    }

    func makeCoordinator() -> Coordinator { Coordinator() }

    func makeNSView(context: Context) -> NSView {
        let view = NSView()
        view.wantsLayer = true
        view.layer?.backgroundColor = NSColor.black.cgColor
        agoraManager.setupRemoteVideo(remoteUid: uid, view: view)
        context.coordinator.boundUid = uid
        return view
    }

    func updateNSView(_ nsView: NSView, context: Context) {
        if context.coordinator.boundUid != uid {
            agoraManager.setupRemoteVideo(remoteUid: uid, view: nsView)
            context.coordinator.boundUid = uid
        }
    }
}

struct ChannelControlState: Equatable {
    var enabled = true
    var volume: Double = 100
}

struct ContentView: View {
    @EnvironmentObject var agoraManager: AgoraManager
    @AppStorage("uiLanguage") private var uiLanguage = "en"

    @AppStorage("appId") private var appId = "43f818097c124394821bd44598b7ca9d"
    @State private var token = ""
    @State private var channelId = ""
    @State private var uidText = "0"

    @State private var dumpEnabled = false
    @State private var channelCount = 8
    @State private var selectedUid: UInt?
    @State private var channelStates = [ChannelControlState](repeating: ChannelControlState(), count: 8)

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
            channelPanel
            videoPanel
            logPanel
        }
        .padding(12)
        .onChange(of: agoraManager.remoteUsers.map { $0.uid }) { uids in
            if let current = selectedUid, uids.contains(current) {
                return
            }
            selectedUid = uids.first
            if selectedUid != nil {
                applyAllChannels()
            }
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
                        .disabled(agoraManager.isJoined)
                }
                HStack {
                    Text(tr("join.token"))
                        .frame(width: 80, alignment: .trailing)
                    TextField(tr("join.token.placeholder"), text: $token)
                        .textFieldStyle(.roundedBorder)
                        .disabled(agoraManager.isJoined)
                }
                HStack {
                    Text(tr("join.channelId"))
                        .frame(width: 80, alignment: .trailing)
                    TextField(tr("join.channelId.placeholder"), text: $channelId)
                        .textFieldStyle(.roundedBorder)
                        .disabled(agoraManager.isJoined)
                }
                HStack {
                    Text(tr("join.uid"))
                        .frame(width: 80, alignment: .trailing)
                    TextField(tr("join.uid.placeholder"), text: $uidText)
                        .textFieldStyle(.roundedBorder)
                        .frame(maxWidth: 160)
                        .disabled(agoraManager.isJoined)
                    Spacer()
                    Text(agoraManager.statusText)
                        .foregroundColor(agoraManager.isJoined ? .green : .secondary)
                    if agoraManager.localUid != 0 {
                        Text(tr("join.localUid", Int(agoraManager.localUid)))
                            .foregroundColor(.secondary)
                    }
                    Button(agoraManager.isJoined ? tr("join.button.leave") : tr("join.button.join")) {
                        joinButtonTapped()
                    }
                    .disabled(!agoraManager.isJoined && (appId.isEmpty || channelId.isEmpty))
                }
                Toggle(tr("join.dumpToggle"), isOn: $dumpEnabled)
                    .onChange(of: dumpEnabled) { newValue in
                        agoraManager.setFrameDump(enabled: newValue)
                    }
            }
            .padding(6)
        }
    }

    // MARK: - Remote multi-channel control

    private var channelPanel: some View {
        GroupBox(tr("mc.section")) {
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    Text(tr("mc.remoteUser"))
                    Picker("", selection: $selectedUid) {
                        Text(tr("mc.noSelection")).tag(UInt?.none)
                        ForEach(agoraManager.remoteUsers) { user in
                            Text("\(user.uid)").tag(UInt?.some(user.uid))
                        }
                    }
                    .labelsHidden()
                    .frame(maxWidth: 180)
                    .onChange(of: selectedUid) { _ in
                        applyAllChannels()
                    }
                    if let uid = selectedUid,
                       let user = agoraManager.remoteUsers.first(where: { $0.uid == uid }) {
                        Text(tr("mc.recvBitrate", user.receivedBitrateKbps))
                            .foregroundColor(.secondary)
                    }
                    Spacer()
                    Text(tr("mc.channelCount"))
                    Picker("", selection: $channelCount) {
                        ForEach(1...8, id: \.self) { count in
                            Text("\(count)").tag(count)
                        }
                    }
                    .labelsHidden()
                    .frame(width: 70)
                }

                if selectedUid == nil {
                    Text(tr("mc.noRemote"))
                        .font(.callout)
                        .foregroundColor(.secondary)
                } else {
                    ForEach(1...channelCount, id: \.self) { index in
                        channelRow(index: index)
                    }
                    Text(tr("mc.countHint"))
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }
            .padding(6)
        }
    }

    @ViewBuilder
    private func channelRow(index: Int) -> some View {
        HStack {
            Text(tr("mc.channelRow", index))
                .frame(width: 76, alignment: .leading)
            Toggle("", isOn: enabledBinding(index))
                .labelsHidden()
            Slider(value: volumeBinding(index), in: 0...100, step: 1)
                .disabled(!channelStates[index - 1].enabled)
            Text("\(Int(channelStates[index - 1].volume))%")
                .monospacedDigit()
                .frame(width: 44, alignment: .trailing)
        }
    }

    // MARK: - Remote video

    private var videoPanel: some View {
        GroupBox(tr("video.section")) {
            ZStack {
                Color.black
                if let uid = selectedUid,
                   agoraManager.remoteUsers.first(where: { $0.uid == uid })?.hasVideo == true {
                    RemoteVideoView(uid: uid)
                } else {
                    Text(tr(selectedUid == nil ? "video.noRemote" : "video.noVideo"))
                        .font(.callout)
                        .foregroundColor(.secondary)
                }
            }
            .frame(maxWidth: .infinity)
            .frame(height: 300)
            .cornerRadius(4)
            .padding(6)
        }
    }

    // MARK: - Log panel

    private var logPanel: some View {
        GroupBox(tr("log.section")) {
            ScrollViewReader { proxy in
                ScrollView {
                    Text(agoraManager.logs.joined(separator: "\n"))
                        .font(.system(size: 11, design: .monospaced))
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .textSelection(.enabled)
                        .padding(4)
                        .id("logBottom")
                }
                .onChange(of: agoraManager.logs.count) { _ in
                    proxy.scrollTo("logBottom", anchor: .bottom)
                }
            }
            .frame(minHeight: 130)
        }
    }

    // MARK: - Actions

    private func joinButtonTapped() {
        if agoraManager.isJoined {
            agoraManager.leaveChannel()
            return
        }
        let uid = UInt(uidText.trimmingCharacters(in: .whitespaces)) ?? 0
        agoraManager.joinChannel(appId: appId.trimmingCharacters(in: .whitespaces),
                                 token: token.trimmingCharacters(in: .whitespaces),
                                 channelId: channelId.trimmingCharacters(in: .whitespaces),
                                 uid: uid)
        if dumpEnabled {
            agoraManager.setFrameDump(enabled: true)
        }
    }

    private func enabledBinding(_ index: Int) -> Binding<Bool> {
        Binding(
            get: { channelStates[index - 1].enabled },
            set: { newValue in
                channelStates[index - 1].enabled = newValue
                applyChannel(index)
            }
        )
    }

    private func volumeBinding(_ index: Int) -> Binding<Double> {
        Binding(
            get: { channelStates[index - 1].volume },
            set: { newValue in
                channelStates[index - 1].volume = newValue
                if channelStates[index - 1].enabled {
                    applyChannel(index)
                }
            }
        )
    }

    private func applyChannel(_ index: Int) {
        guard agoraManager.isJoined, let uid = selectedUid else { return }
        let state = channelStates[index - 1]
        agoraManager.setChannelVolume(remoteUid: uid, index: index, volume: state.enabled ? Int(state.volume) : 0)
    }

    /// 把全部 8 个声道的当前 UI 状态应用到选中的远端 uid（隐藏行的状态也一并下发，保证行为确定）。
    private func applyAllChannels() {
        guard agoraManager.isJoined, let uid = selectedUid else { return }
        for index in 1...8 {
            let state = channelStates[index - 1]
            agoraManager.setChannelVolume(remoteUid: uid, index: index, volume: state.enabled ? Int(state.volume) : 0)
        }
    }
}
