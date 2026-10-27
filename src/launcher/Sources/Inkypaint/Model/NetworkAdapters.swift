// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import Darwin
import Foundation

/// The Mac's network adapters with an IPv4 address, for the game's LAN adapter setting (lan_adapter in [Network]:
/// the runtime's net_iface.c finds the adapter by the same name, and says when it is missing)
struct NetworkAdapter: Hashable {
    let name: String
    let address: String

    /// ZeroTier makes feth adapters on macOS (zt on Linux), Hamachi ham ones, by the runtime's snx_net_virtual_lan
    var virtualLAN: Bool { name.hasPrefix("feth") || name.hasPrefix("zt") || name.hasPrefix("ham") }

    var label: String {
        let kind = virtualLAN ? "virtual LAN (ZeroTier)"
            : name.hasPrefix("en") ? "Wi-Fi or Ethernet"
            : name.hasPrefix("utun") || name.hasPrefix("ipsec") ? "VPN"
            : name.hasPrefix("bridge") ? "bridge" : ""
        return kind.isEmpty ? "\(name), \(address)" : "\(name), \(address) (\(kind))"
    }

    static func current() -> [NetworkAdapter] {
        var list: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&list) == 0, let first = list else { return [] }
        defer { freeifaddrs(list) }
        var out: [NetworkAdapter] = []
        for entry in sequence(first: first, next: { $0.pointee.ifa_next }) {
            let a = entry.pointee
            guard let addr = a.ifa_addr, addr.pointee.sa_family == UInt8(AF_INET),
                  a.ifa_flags & UInt32(IFF_UP) != 0, a.ifa_flags & UInt32(IFF_LOOPBACK) == 0 else { continue }
            var host = [CChar](repeating: 0, count: Int(NI_MAXHOST))
            guard getnameinfo(addr, socklen_t(addr.pointee.sa_len), &host, socklen_t(host.count), nil, 0,
                              NI_NUMERICHOST) == 0 else { continue }
            let adapter = NetworkAdapter(name: String(cString: a.ifa_name), address: String(cString: host))
            if !out.contains(adapter) { out.append(adapter) }
        }
        return out
    }

    /// The setting's choices: the automatic ones, then each adapter by name
    static func choices() -> [(value: String, label: String)] {
        var out: [(value: String, label: String)] = [
            ("", "Automatic (the one the internet goes through)"),
            ("virtual", "Virtual LAN when connected (ZeroTier, Hamachi)"),
        ]
        out += current().map { (value: $0.name, label: $0.label) }
        out.append(("all", "Every adapter (experimental)"))
        return out
    }
}
