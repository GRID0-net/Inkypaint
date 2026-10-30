// swift-tools-version: 6.0
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import PackageDescription
let package = Package(name: "InkypaintIPABuilder", platforms: [.macOS(.v14)], targets: [
    .executableTarget(name: "InkypaintIPABuilder", swiftSettings: [.swiftLanguageMode(.v5)])
])
