// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import AppKit
import SwiftUI

/// Splatoon's inks, for accents on a system-looking app.
enum Ink {
    static let lime = Color(red: 0.80, green: 0.95, blue: 0.20)
    static let purple = Color(red: 0.36, green: 0.24, blue: 0.96)
    static let magenta = Color(red: 0.93, green: 0.22, blue: 0.78)
    static let night = Color(red: 0.07, green: 0.04, blue: 0.17)
    static let dusk = Color(red: 0.17, green: 0.08, blue: 0.42)
}

/// The splat masks in Resources, white on transparent: tinted where they are drawn.
enum Splat: String, CaseIterable {
    case burst = "splat-burst"
    case drip = "splat-drip"
    case streak = "splat-streak"

    var image: Image {
        guard let ns = Self.loaded[self] else { return Image(systemName: "drop.fill") }
        return Image(nsImage: ns).renderingMode(.template)
    }

    /// Each splat read once
    private static let loaded: [Splat: NSImage] = {
        var out: [Splat: NSImage] = [:]
        for splat in Splat.allCases {
            for bundle in bundles {
                if let url = bundle.url(forResource: splat.rawValue, withExtension: "png"),
                   let image = NSImage(contentsOf: url) {
                    out[splat] = image
                    break
                }
            }
        }
        return out
    }()

    /// Where the splats are: the app's Resources (scripts/package-launcher.sh copies them there), or SwiftPM's resource
    /// bundle beside the executable when run from Xcode or `swift run`. Not Bundle.module: it stops the app when its
    /// bundle isn't where the build left it, as in the packaged app
    private static let bundles: [Bundle] = {
        var found = [Bundle.main]
        let places = [Bundle.main.resourceURL, Bundle.main.executableURL?.deletingLastPathComponent()].compactMap { $0 }
        for place in places {
            let items = (try? FileManager.default.contentsOfDirectory(at: place, includingPropertiesForKeys: nil)) ?? []
            for item in items where item.lastPathComponent.hasSuffix("_Inkypaint.bundle") {
                if let bundle = Bundle(url: item) { found.append(bundle) }
            }
        }
        return found
    }()
}

/// The game's own logo, when the app was built with one: scripts/package-launcher.sh copies local/branding/*.png
/// into the app, and nothing of the game's goes into git, so a build without it shows the title in type instead.
enum Branding {
    static let logo: NSImage? = {
        for bundle in [Bundle.main] {
            if let url = bundle.url(forResource: "splatoon3-logo", withExtension: "png") { return NSImage(contentsOf: url) }
        }
        if let path = ProcessInfo.processInfo.environment["INKYPAINT_LOGO"] { return NSImage(contentsOfFile: path) }
        return nil
    }()
}

extension Font {
    /// The game's loud display type, as near as the system font gets: black, rounded, slanted.
    static func inkTitle(_ size: CGFloat) -> Font { .system(size: size, weight: .black, design: .rounded).italic() }
}

/// A splat of ink drawn at a point of its parent, sized and turned.
struct SplatMark: View {
    var splat: Splat
    var color: Color
    var size: CGFloat
    var angle: Double = 0

    var body: some View {
        splat.image
            .resizable()
            .aspectRatio(contentMode: .fit)
            .foregroundStyle(color)
            .frame(width: size, height: size)
            .rotationEffect(.degrees(angle))
            .accessibilityHidden(true)
    }
}

/// A numbered step badge in ink: a small splat with the number on it.
struct StepBadge: View {
    var number: Int
    var color: Color = Ink.purple
    var done = false

    var body: some View {
        ZStack {
            SplatMark(splat: .drip, color: done ? Ink.lime : color, size: 40, angle: Double(number * 37))
            if done {
                Image(systemName: "checkmark").font(.system(size: 14, weight: .black)).foregroundStyle(.black)
            } else {
                Text("\(number)").font(.inkTitle(17)).foregroundStyle(.white)
            }
        }
        .frame(width: 40, height: 40)
    }
}

/// A rounded panel of Liquid Glass with room inside.
struct GlassCard<Content: View>: View {
    var tint: Color? = nil
    @ViewBuilder var content: Content

    var body: some View {
        content
            .padding(20)
            .frame(maxWidth: .infinity, alignment: .leading)
            .glassEffect(tint.map { Glass.regular.tint($0.opacity(0.25)) } ?? .regular, in: .rect(cornerRadius: 24))
    }
}
