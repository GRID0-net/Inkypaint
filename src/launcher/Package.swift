// swift-tools-version: 6.0
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
//
// The inkypaint launcher: a SwiftUI app for macOS 26 and later (Liquid Glass). Open this folder in Xcode 26 to work on
// it, or build the self-contained app with `./inkypaint.sh launcher` (scripts/package-launcher.sh), which puts the
// runtime, MoltenVK and LLVM inside it. Its pictures (the ink splats, the icon) are in dist/launcher, where the
// repository keeps its binaries: the script copies them into the app, and a run from Xcode reads them there.
import PackageDescription

let package = Package(
    name: "InkypaintLauncher",
    platforms: [.macOS("26.0")],
    targets: [
        .executableTarget(
            name: "Inkypaint",
            path: "Sources/Inkypaint",
            swiftSettings: [.swiftLanguageMode(.v5)]
        )
    ]
)
