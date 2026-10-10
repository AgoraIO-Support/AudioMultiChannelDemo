import SwiftUI

@main
struct MultiChannelAudioSenderApp: App {
    @StateObject private var senderManager = AgoraSenderManager()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(senderManager)
                .frame(minWidth: 720, minHeight: 620)
        }
    }
}
