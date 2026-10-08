import SwiftUI

@main
struct MultiChannelAudioReceiverApp: App {
    @StateObject private var agoraManager = AgoraManager()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(agoraManager)
                .frame(minWidth: 720, minHeight: 660)
        }
    }
}
