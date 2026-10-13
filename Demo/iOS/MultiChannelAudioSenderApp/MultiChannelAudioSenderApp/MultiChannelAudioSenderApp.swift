import SwiftUI

@main
struct MultiChannelAudioSenderApp: App {
    @StateObject private var senderManager = AgoraSenderManager()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(senderManager)
        }
    }
}
