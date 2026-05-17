import AppKit
import Foundation
import SystemExtensions

private let extensionIdentifier = "com.axelheckert.driver.FireWireOHCIProbe"

final class ExtensionDelegate: NSObject, OSSystemExtensionRequestDelegate {
    private var finished = false

    func activate() {
        print("Main bundle: \(Bundle.main.bundleURL.path)")
        print("Looking for embedded system extensions in Contents/Library/SystemExtensions")

        let request = OSSystemExtensionRequest.activationRequest(
            forExtensionWithIdentifier: extensionIdentifier,
            queue: .main
        )
        request.delegate = self

        print("Submitting activation request for \(extensionIdentifier)")
        OSSystemExtensionManager.shared.submitRequest(request)
    }

    func request(
        _ request: OSSystemExtensionRequest,
        actionForReplacingExtension existing: OSSystemExtensionProperties,
        withExtension ext: OSSystemExtensionProperties
    ) -> OSSystemExtensionRequest.ReplacementAction {
        print("Replacing existing system extension \(existing.bundleIdentifier) \(existing.bundleVersion)")
        return .replace
    }

    func requestNeedsUserApproval(_ request: OSSystemExtensionRequest) {
        print("System approval required. Open System Settings and approve FireWireOHCIProbe if prompted.")
    }

    func request(
        _ request: OSSystemExtensionRequest,
        didFinishWithResult result: OSSystemExtensionRequest.Result
    ) {
        switch result {
        case .completed:
            print("Activation completed.")
        case .willCompleteAfterReboot:
            print("Activation will complete after reboot.")
        @unknown default:
            print("Activation finished with unknown result: \(result.rawValue)")
        }
        finish(exitCode: 0)
    }

    func request(_ request: OSSystemExtensionRequest, didFailWithError error: Error) {
        let nsError = error as NSError
        print("Activation failed: \(nsError.domain) code=\(nsError.code)")
        print(nsError.localizedDescription)
        if !nsError.userInfo.isEmpty {
            print("userInfo: \(nsError.userInfo)")
        }
        finish(exitCode: 1)
    }

    private func finish(exitCode: Int32) {
        guard !finished else {
            return
        }
        finished = true
        fflush(stdout)
        fflush(stderr)
        NSApp.terminate(nil)
        exit(exitCode)
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    private let extensionDelegate = ExtensionDelegate()

    func applicationDidFinishLaunching(_ notification: Notification) {
        extensionDelegate.activate()
    }
}

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.setActivationPolicy(.accessory)
app.run()
