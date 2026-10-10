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

/// 接收码率实时走势（最近 60 个样本，约 2 分钟）。
struct BitrateSparkline: View {
    let samples: [Int]
    var color: Color = .green

    var body: some View {
        GeometryReader { geo in
            let maxV = max(samples.max() ?? 0, 1)
            Path { path in
                guard samples.count > 1 else { return }
                let stepX = geo.size.width / CGFloat(samples.count - 1)
                for (i, v) in samples.enumerated() {
                    let x = stepX * CGFloat(i)
                    let y = geo.size.height * (1 - CGFloat(v) / CGFloat(maxV))
                    if i == 0 {
                        path.move(to: CGPoint(x: x, y: y))
                    } else {
                        path.addLine(to: CGPoint(x: x, y: y))
                    }
                }
            }
            .stroke(color, lineWidth: 1.5)
        }
        .background(Color.primary.opacity(0.06))
        .cornerRadius(4)
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
            // 双栏：左 = 控制（选谁、怎么听），右 = 监控（所选 uid 的视频与码率）
            HStack(alignment: .top, spacing: 12) {
                controlPanel
                    .frame(width: 430)
                monitorPanel
                    .frame(maxWidth: .infinity)
            }
            .frame(minHeight: 480)
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
                        .frame(maxWidth: 520)
                        .disabled(agoraManager.isJoined)
                }
                HStack {
                    Text(tr("join.token"))
                        .frame(width: 80, alignment: .trailing)
                    TextField(tr("join.token.placeholder"), text: $token)
                        .textFieldStyle(.roundedBorder)
                        .frame(maxWidth: 520)
                        .disabled(agoraManager.isJoined)
                }
                HStack {
                    Text(tr("join.channelId"))
                        .frame(width: 80, alignment: .trailing)
                    TextField(tr("join.channelId.placeholder"), text: $channelId)
                        .textFieldStyle(.roundedBorder)
                        .frame(maxWidth: 520)
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

    // MARK: - 左栏：控制面板

    private var controlPanel: some View {
        GroupBox(tr("mc.section")) {
            VStack(alignment: .leading, spacing: 10) {
                HStack(spacing: 12) {
                    Text(tr("mc.remoteUser"))
                    Picker("", selection: $selectedUid) {
                        Text(tr("mc.noSelection")).tag(UInt?.none)
                        ForEach(agoraManager.remoteUsers) { user in
                            Text(verbatim: "UID \(user.uid)").tag(UInt?.some(user.uid))
                        }
                    }
                    .labelsHidden()
                    .frame(maxWidth: 180)
                    .onChange(of: selectedUid) { _ in
                        applyAllChannels()
                    }
                }

                Divider()

                if selectedUid == nil {
                    Text(tr("mc.noRemote"))
                        .font(.callout)
                        .foregroundColor(.secondary)
                } else {
                    HStack {
                        Text(tr("mc.playbackSection"))
                            .font(.caption)
                            .foregroundColor(.secondary)
                        Spacer()
                        Text(tr("mc.channelCount"))
                            .font(.caption)
                            .foregroundColor(.secondary)
                        Picker("", selection: $channelCount) {
                            ForEach(1...8, id: \.self) { count in
                                Text("\(count)").tag(count)
                            }
                        }
                        .labelsHidden()
                        .frame(width: 60)
                    }
                    ForEach(1...channelCount, id: \.self) { index in
                        channelRow(index: index)
                    }
                    Text(tr("mc.countHint"))
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }
            .padding(8)
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

    // MARK: - 右栏：监控面板（视频 + 接收码率走势）

    private var selectedUser: RemoteUser? {
        guard let uid = selectedUid else { return nil }
        return agoraManager.remoteUsers.first(where: { $0.uid == uid })
    }

    /// 流实际宽高比（remoteVideoStats），拿不到时按 16:9。
    private func videoAspect(_ user: RemoteUser) -> CGFloat {
        guard user.videoWidth > 0, user.videoHeight > 0 else { return 16.0 / 9.0 }
        return CGFloat(user.videoWidth) / CGFloat(user.videoHeight)
    }

    /// 一路码率的标题 + 走势图 + Y 轴（最大值/0）。
    @ViewBuilder
    private func bitrateBlock(title: String, samples: [Int], color: Color) -> some View {
        Text(title + " · " + tr("mc.monitorWindow"))
            .font(.caption)
            .foregroundColor(.secondary)
        HStack(alignment: .top, spacing: 4) {
            BitrateSparkline(samples: samples, color: color)
                .frame(height: 64)
            VStack(alignment: .trailing) {
                Text("\(max(samples.max() ?? 0, 1))")
                Spacer()
                Text("0")
            }
            .font(.system(size: 9))
            .foregroundColor(.secondary)
            .frame(height: 64)
        }
    }

    private var monitorPanel: some View {
        GroupBox(selectedUser.map { tr("monitor.section", Int($0.uid)) } ?? tr("monitor.section.none")) {
            VStack(alignment: .leading, spacing: 10) {
                if let user = selectedUser {
                    if user.hasVideo {
                        HStack {
                            RemoteVideoView(uid: user.uid)
                                .aspectRatio(videoAspect(user), contentMode: .fit)
                                .cornerRadius(4)
                            Spacer(minLength: 0)
                        }
                        bitrateBlock(title: tr("mc.videoRecvBitrate", user.videoReceivedBitrateKbps),
                                     samples: user.videoBitrateHistory, color: .blue)
                    } else {
                        Text(tr("mc.audioOnly"))
                            .font(.callout)
                            .foregroundColor(.secondary)
                    }
                    bitrateBlock(title: tr("mc.recvBitrate", user.receivedBitrateKbps),
                                 samples: user.bitrateHistory, color: .green)
                } else {
                    Text(tr("mc.noRemote"))
                        .font(.callout)
                        .foregroundColor(.secondary)
                }
            }
            .padding(8)
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
            .frame(minHeight: 110)
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
