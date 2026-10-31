// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import AppKit
import Combine
import Foundation

@MainActor
final class BuilderModel: ObservableObject {
    @Published var project: String
    @Published var installation: String
    @Published var workers = 8
    @Published var includeDlc = false
    @Published private(set) var busy = false
    @Published private(set) var status = "Ready"
    @Published private(set) var log = ""
    @Published private(set) var builtIPA: URL?
    private var task: Process?
    private var pipe: Pipe?
    private var stopping = false
    var onStopped: (() -> Void)?

    init() {
        let defaults = UserDefaults.standard
        let bundled = Bundle.main.url(forResource: "project", withExtension: "txt")
            .flatMap { try? String(contentsOf: $0, encoding: .utf8) }?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
        project = defaults.string(forKey: "project") ?? bundled
        installation = defaults.string(forKey: "installation") ?? ""
        if installation.isEmpty { useExistingInstall() }
    }

    func useExistingInstall() {
        let saved = UserDefaults(suiteName: "io.github.inkypaint.launcher")?.string(forKey: "dataFolder")
        let appData = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("inkypaint/data").path
        let candidates = [saved, project.isEmpty ? nil : project + "/local/data", appData].compactMap { $0 }
        installation = candidates.first { FileManager.default.fileExists(atPath: $0) } ?? appData
    }

    func chooseProject() {
        if let path = folder(title: "Choose the inkypaint project folder") { project = path }
    }
    func chooseInstallation() {
        if let path = folder(title: "Choose the existing inkypaint data or game folder") { installation = path }
    }
    private func folder(title: String) -> String? {
        let panel = NSOpenPanel()
        panel.title = title
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        return panel.runModal() == .OK ? panel.url?.path : nil
    }

    func run(check: Bool) {
        guard !busy else { return }
        let root = URL(fileURLWithPath: project).standardizedFileURL
        let script = root.appendingPathComponent("scripts/package-ios-all.sh")
        guard FileManager.default.fileExists(atPath: script.path) else {
            status = "Choose the inkypaint project folder first."
            return
        }
        guard let runner = Bundle.main.url(forResource: "run-ios-job", withExtension: "py") else {
            status = "The app's build runner is missing. Repackage the Mac helper."
            return
        }
        UserDefaults.standard.set(project, forKey: "project")
        UserDefaults.standard.set(installation, forKey: "installation")
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/env")
        process.arguments = ["python3", runner.path, "/bin/bash", script.path] + (check ? ["--check"] : []) + [installation]
        process.currentDirectoryURL = root
        var environment = ProcessInfo.processInfo.environment
        environment["PATH"] = "/opt/homebrew/bin:/usr/local/bin:" + (environment["PATH"] ?? "/usr/bin:/bin:/usr/sbin:/sbin")
        environment["INKY_JOBS"] = String(workers)
        environment["INKY_IOS_INCLUDE_DLC"] = includeDlc ? "1" : "0"
        environment["PYTHONUNBUFFERED"] = "1"
        process.environment = environment
        let output = Pipe()
        process.standardOutput = output
        process.standardError = output
        log = ""
        builtIPA = nil
        stopping = false
        busy = true
        status = check ? "Checking files and dependencies…" : "Preparing build…"
        output.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let bytes = handle.availableData
            if bytes.isEmpty { handle.readabilityHandler = nil; return }
            let text = String(decoding: bytes, as: UTF8.self)
            DispatchQueue.main.async { self?.append(text) }
        }
        process.terminationHandler = { [weak self] ended in
            let code = ended.terminationStatus
            DispatchQueue.main.async {
                guard let self else { return }
                self.busy = false
                self.task = nil
                self.pipe = nil
                if self.stopping { self.status = "Stopped" }
                else if code == 0 {
                    self.status = check ? "Files and dependencies checked" : "IPA ready to sign"
                    if !check {
                        self.builtIPA = root.appendingPathComponent("build/ios-aot/package/inkypaint-ios-aot.ipa")
                    }
                } else { self.status = "Failed (exit \(code)). See the build output." }
                self.onStopped?()
            }
        }
        task = process
        pipe = output
        do { try process.run() }
        catch {
            output.fileHandleForReading.readabilityHandler = nil
            task = nil; pipe = nil; busy = false
            status = "Could not start: \(error.localizedDescription)"
        }
    }
    private func append(_ text: String) {
        log += text
        if log.count > 200_000 { log = String(log.suffix(160_000)) }
        if busy && !stopping, let line = log.components(separatedBy: "\n").last(where: { $0.hasPrefix("INKY_STAGE: ") }) {
            status = String(line.dropFirst("INKY_STAGE: ".count))
        }
    }
    func stop() {
        guard let task, task.isRunning else { return }
        stopping = true
        status = "Stopping build…"
        task.terminate() // Python forwards TERM to this build's owned session only.
    }
    func showIPA() {
        if let builtIPA { NSWorkspace.shared.activateFileViewerSelecting([builtIPA]) }
    }
    func showLogs() {
        NSWorkspace.shared.open(URL(fileURLWithPath: project).appendingPathComponent("logs"))
    }
}
