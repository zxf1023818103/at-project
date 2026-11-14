import SwiftUI
import UIKit
import Foundation
// Same duration parser as Flash Studio; the transfer implementation is copied
// verbatim from L2CAPBench.swift, without the CS/ASK application's permission scope.
enum UploadMode {
    static var automaticSeconds: Int {
        let raw = ProcessInfo.processInfo.arguments.first(where: { $0.hasPrefix("--seconds=") })
        return min(60, max(1, Int(raw?.dropFirst("--seconds=".count) ?? "10") ?? 10))
    }
}
@main
struct CoCBenchApp: App {
    @StateObject private var bench = L2CAPBench()
    @Environment(\.scenePhase) private var scenePhase
    var body: some Scene {
        WindowGroup {
            L2CAPView(bench: bench)
                .onAppear { UIApplication.shared.isIdleTimerDisabled = true; bench.connect() }
                .onChange(of: scenePhase) { _, phase in bench.setActive(phase == .active) }
        }
    }
}
