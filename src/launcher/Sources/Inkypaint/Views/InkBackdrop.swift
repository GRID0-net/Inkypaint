// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import SwiftUI

/// The Play screen's backdrop: a night-purple field with splats of the three inks drifting slowly, which the sidebar's
/// glass extends under. Still when Reduce Motion is on.
struct InkBackdrop: View {
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @State private var drift = false

    var body: some View {
        GeometryReader { geo in
            let w = geo.size.width, h = geo.size.height
            ZStack {
                LinearGradient(colors: [Ink.dusk, Ink.night], startPoint: .topTrailing, endPoint: .bottomLeading)
                RadialGradient(colors: [Ink.purple.opacity(0.55), .clear], center: .init(x: 0.78, y: 0.28),
                               startRadius: 10, endRadius: max(w, h) * 0.6)

                SplatMark(splat: .burst, color: Ink.purple, size: h * 0.95, angle: drift ? -8 : -14)
                    .position(x: w * 0.80, y: h * 0.34)
                    .offset(y: drift ? -10 : 10)
                SplatMark(splat: .streak, color: Ink.lime, size: h * 0.62, angle: drift ? 24 : 18)
                    .position(x: w * 0.97, y: h * 0.86)
                    .offset(x: drift ? 6 : -6)
                SplatMark(splat: .drip, color: Ink.magenta, size: h * 0.30, angle: 8)
                    .position(x: w * 0.56, y: h * 0.10)
                    .offset(y: drift ? 4 : -4)
                SplatMark(splat: .streak, color: Ink.magenta.opacity(0.85), size: h * 0.16, angle: -30)
                    .position(x: w * 0.30, y: h * 0.80)
                SplatMark(splat: .burst, color: Ink.lime.opacity(0.9), size: h * 0.12, angle: 50)
                    .position(x: w * 0.45, y: h * 0.40)

                // the text side stays readable
                LinearGradient(colors: [Ink.night.opacity(0.85), Ink.night.opacity(0.0)], startPoint: .bottomLeading,
                               endPoint: .center)
            }
        }
        .ignoresSafeArea()
        .onAppear {
            guard !reduceMotion else { return }
            withAnimation(.easeInOut(duration: 7).repeatForever(autoreverses: true)) { drift.toggle() }
        }
    }
}

/// The burst of ink when the game starts: a lime splat thrown from the Play button.
struct InkBurst: View {
    var trigger: Int
    @State private var scale = 0.1
    @State private var opacity = 0.0

    var body: some View {
        SplatMark(splat: .burst, color: Ink.lime, size: 420, angle: Double(trigger * 53))
            .scaleEffect(scale)
            .opacity(opacity)
            .allowsHitTesting(false)
            .onChange(of: trigger) {
                scale = 0.1
                opacity = 1
                withAnimation(.spring(response: 0.45, dampingFraction: 0.6)) { scale = 1.0 }
                withAnimation(.easeOut(duration: 0.9).delay(0.35)) { opacity = 0 }
            }
    }
}
