// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import AppKit
import SwiftUI

/// A control's key (key_<id>): click, then press a key, a mouse button or turn the wheel. Esc keeps what it was.
/// The arrow goes back to the game's default (empty), the cross leaves the control without a key (None).
struct KeyCaptureField: View {
    let current: String
    let fallback: String
    let pick: (String) -> Void
    @State private var listening = false
    @State private var monitor: Any?

    var body: some View {
        HStack(spacing: 6) {
            Button {
                listening ? stop() : start()
            } label: {
                Text(listening ? "Press a key or button…" : current == "None" ? "No key" : KeyNames.shown(current.isEmpty ? fallback : current))
                    .foregroundStyle(listening || !current.isEmpty ? .primary : .secondary)
                    .frame(minWidth: 170, alignment: .leading)
            }
            .buttonStyle(.bordered)
            .tint(listening ? Ink.lime : nil)
            Button { pick("") } label: { Image(systemName: "arrow.uturn.backward") }
                .buttonStyle(.borderless)
                .disabled(current.isEmpty)
                .help("The default: \(KeyNames.shown(fallback))")
            Button { pick("None") } label: { Image(systemName: "xmark") }
                .buttonStyle(.borderless)
                .disabled(current == "None")
                .help("No key")
        }
        .onDisappear { stop() }
    }

    private func start() {
        listening = true
        // after this click: the press that started listening isn't the key
        DispatchQueue.main.async {
            monitor = NSEvent.addLocalMonitorForEvents(
                matching: [.keyDown, .flagsChanged, .leftMouseDown, .rightMouseDown, .otherMouseDown, .scrollWheel]
            ) { event in
                guard listening else { return event }
                var name: String?
                switch event.type {
                case .keyDown:
                    if event.keyCode == 0x35 { stop(); return nil } // Esc
                    name = KeyNames.mac[event.keyCode]
                case .flagsChanged:
                    // a modifier going down: its own flag is set now
                    if let n = KeyNames.mac[event.keyCode], KeyNames.isDown(event) { name = n } else { return nil }
                case .leftMouseDown: name = "Mouse Left"
                case .rightMouseDown: name = "Mouse Right"
                case .otherMouseDown:
                    name = event.buttonNumber == 2 ? "Mouse Middle" : event.buttonNumber == 3 ? "Mouse X1"
                        : event.buttonNumber == 4 ? "Mouse X2" : nil
                case .scrollWheel:
                    if event.scrollingDeltaY == 0 { return nil }
                    // the wheel as SDL reads it: up is away from the player, whatever natural scrolling does
                    let up = (event.scrollingDeltaY > 0) != event.isDirectionInvertedFromDevice
                    name = up ? "Wheel Up" : "Wheel Down"
                default: break
                }
                if let name {
                    pick(name)
                    stop()
                }
                return nil
            }
        }
    }

    private func stop() {
        listening = false
        if let monitor { NSEvent.removeMonitor(monitor) }
        monitor = nil
    }
}

/// SDL's names of the Mac's keys, by where they are (SDL's scancodes are places on the keyboard, named as a US layout
/// has them): kVK_ virtual key codes
enum KeyNames {
    static let mac: [UInt16: String] = [
        0x00: "A", 0x01: "S", 0x02: "D", 0x03: "F", 0x04: "H", 0x05: "G", 0x06: "Z", 0x07: "X", 0x08: "C", 0x09: "V",
        0x0B: "B", 0x0C: "Q", 0x0D: "W", 0x0E: "E", 0x0F: "R", 0x10: "Y", 0x11: "T",
        0x12: "1", 0x13: "2", 0x14: "3", 0x15: "4", 0x16: "6", 0x17: "5", 0x18: "=", 0x19: "9", 0x1A: "7", 0x1B: "-",
        0x1C: "8", 0x1D: "0", 0x1E: "]", 0x1F: "O", 0x20: "U", 0x21: "[", 0x22: "I", 0x23: "P", 0x24: "Return",
        0x25: "L", 0x26: "J", 0x27: "'", 0x28: "K", 0x29: ";", 0x2A: "\\", 0x2B: ",", 0x2C: "/", 0x2D: "N",
        0x2E: "M", 0x2F: ".", 0x30: "Tab", 0x31: "Space", 0x32: "`", 0x33: "Backspace", 0x36: "Right GUI",
        0x37: "Left GUI", 0x38: "Left Shift", 0x39: "CapsLock", 0x3A: "Left Alt", 0x3B: "Left Ctrl",
        0x3C: "Right Shift", 0x3D: "Right Alt", 0x3E: "Right Ctrl", 0x40: "F17", 0x41: "Keypad .", 0x43: "Keypad *",
        0x45: "Keypad +", 0x47: "Numlock", 0x4B: "Keypad /", 0x4C: "Keypad Enter", 0x4E: "Keypad -",
        0x4F: "F18", 0x50: "F19", 0x51: "Keypad =", 0x52: "Keypad 0", 0x53: "Keypad 1", 0x54: "Keypad 2",
        0x55: "Keypad 3", 0x56: "Keypad 4", 0x57: "Keypad 5", 0x58: "Keypad 6", 0x59: "Keypad 7", 0x5A: "F20",
        0x5B: "Keypad 8", 0x5C: "Keypad 9", 0x60: "F5", 0x61: "F6", 0x62: "F7", 0x63: "F3", 0x64: "F8", 0x65: "F9",
        0x67: "F11", 0x69: "F13", 0x6A: "F16", 0x6B: "F14", 0x6D: "F10", 0x6F: "F12", 0x71: "F15", 0x72: "Insert",
        0x73: "Home", 0x74: "PageUp", 0x75: "Delete", 0x76: "F4", 0x77: "End", 0x78: "F2", 0x79: "PageDown",
        0x7A: "F1", 0x7B: "Left", 0x7C: "Right", 0x7D: "Down", 0x7E: "Up",
    ]

    /// whether a flagsChanged event is its modifier going down
    static func isDown(_ event: NSEvent) -> Bool {
        let flags = event.modifierFlags
        switch event.keyCode {
        case 0x38, 0x3C: return flags.contains(.shift)
        case 0x3B, 0x3E: return flags.contains(.control)
        case 0x3A, 0x3D: return flags.contains(.option)
        case 0x36, 0x37: return flags.contains(.command)
        case 0x39: return flags.contains(.capsLock)
        default: return false
        }
    }

    /// SDL's names as a Mac says them
    static func shown(_ names: String) -> String {
        names.split(separator: ",").map { part -> String in
            let name = part.trimmingCharacters(in: .whitespaces)
            switch name {
            case "Delete": return "Forward Delete"
            case "Backspace": return "Delete"
            default: return name.replacingOccurrences(of: "Alt", with: "Option").replacingOccurrences(of: "GUI", with: "Command")
            }
        }.joined(separator: ", ")
    }
}
