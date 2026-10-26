// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import Foundation
import Observation

/// Online play on GRID0+: the account (its user name in the launcher's defaults, its password in the Keychain), its
/// friends and requests, and the game's settings that turn it on ([Network] in the game's .ini: network, grid0,
/// grid0_server, grid0_user). The game signs in itself, with the password the launcher hands it at start.
@Observable
@MainActor
final class OnlineModel {
    enum Account: Equatable {
        case signedOut
        case signingIn
        case signedIn
    }

    private(set) var account: Account = .signedOut
    private(set) var me: Grid0Player?
    private(set) var friends: [Grid0Player] = []
    private(set) var incoming: [Grid0FriendRequest] = []
    private(set) var outgoing: [Grid0FriendRequest] = []
    private(set) var error: String?
    private(set) var notice: String?
    private(set) var refreshed: Date?
    private(set) var busy: Set<String> = [] // the requests and friends an action is under way for
    let patches = Grid0Patches()

    private(set) var username: String
    private(set) var server: String
    private var session: Grid0Session?
    /// The game's settings, the app model's (it changes with the data folder)
    var settings: GameSettings?

    init() {
        username = UserDefaults.standard.string(forKey: "grid0User") ?? ""
        server = UserDefaults.standard.string(forKey: "grid0Server") ?? Grid0.defaultServer
        if !username.isEmpty, let password = Grid0Keychain.load(account: username) {
            session = Grid0Session(server: server, username: username, password: password)
            account = .signedIn
        }
    }

    var isSignedIn: Bool { account == .signedIn }

    // MARK: the game's settings

    private static let network = "Network"
    /// Online play: on in the game's settings, with an account to play as
    var playsOnline: Bool { isSignedIn && settings?.file.value("grid0") == "1" }

    func setPlaysOnline(_ on: Bool) {
        guard let s = settings else { return }
        s.setRaw("grid0", on ? "1" : "0", section: Self.network)
        if on {
            s.setRaw("network", "1", section: Self.network) // GRID0+ needs the game online
            s.setRaw("grid0_user", username, section: Self.network)
            s.setRaw("grid0_server", server, section: Self.network)
        }
    }

    /// The password the game signs in with, when it plays online
    func passwordForGame() -> String? {
        guard playsOnline else { return nil }
        return Grid0Keychain.load(account: username)
    }

    // MARK: the account

    func signIn(username: String, password: String, server: String) async {
        let username = username.trimmingCharacters(in: .whitespaces)
        let server = server.trimmingCharacters(in: .whitespaces).isEmpty ? Grid0.defaultServer
            : server.trimmingCharacters(in: .whitespaces)
        guard !username.isEmpty, !password.isEmpty else {
            error = "Enter your GRID0+ user name and password."
            return
        }
        account = .signingIn
        error = nil
        let candidate = Grid0Session(server: server, username: username, password: password)
        do {
            try await candidate.signIn()
            let me = try await candidate.me()
            try Grid0Keychain.save(password, account: username)
            if self.username != username, !self.username.isEmpty { Grid0Keychain.delete(account: self.username) }
            self.username = username
            self.server = server
            UserDefaults.standard.set(username, forKey: "grid0User")
            UserDefaults.standard.set(server, forKey: "grid0Server")
            session = candidate
            self.me = me
            account = .signedIn
            setPlaysOnline(true)
            await refresh()
        } catch {
            account = session == nil ? .signedOut : .signedIn
            self.error = error.localizedDescription
        }
    }

    func signOut() {
        setPlaysOnline(false)
        Grid0Keychain.delete(account: username)
        session = nil
        me = nil
        friends = []
        incoming = []
        outgoing = []
        refreshed = nil
        error = nil
        account = .signedOut
    }

    // MARK: friends

    /// The account, its friends and its requests, fetched together
    func refresh() async {
        guard let session else { return }
        do {
            async let who = session.me()
            async let list = session.friends()
            async let requests = session.requests()
            let (m, f, r) = try await (who, list, requests)
            me = m
            friends = f.sorted { a, b in
                if a.isOnline != b.isOnline { return a.isOnline }
                if a.favorite != b.favorite { return a.favorite }
                return a.nickname.localizedCaseInsensitiveCompare(b.nickname) == .orderedAscending
            }
            incoming = r.incoming
            outgoing = r.outgoing
            refreshed = Date()
            error = nil
        } catch {
            self.error = error.localizedDescription
        }
    }

    /// A request to the friend code given (SW-1234-5678-9012, or its twelve digits)
    func sendRequest(_ code: String) async -> Bool {
        let digits = code.filter(\.isNumber)
        guard digits.count == 12 else {
            error = "A friend code has twelve digits: SW-1234-5678-9012."
            return false
        }
        let formatted = "SW-" + [0, 4, 8].map { i in
            String(digits[digits.index(digits.startIndex, offsetBy: i)..<digits.index(digits.startIndex, offsetBy: i + 4)])
        }.joined(separator: "-")
        return await act(formatted, done: "Friend request sent to \(formatted).") {
            try await $0.sendRequest(friendCode: formatted)
        }
    }

    func accept(_ r: Grid0FriendRequest) async {
        _ = await act(r.id, done: "You and \(r.other.nickname) are friends now.") { try await $0.settle(r.id, "accept") }
    }
    func decline(_ r: Grid0FriendRequest) async {
        _ = await act(r.id, done: nil) { try await $0.settle(r.id, "deny") }
    }
    func cancel(_ r: Grid0FriendRequest) async {
        _ = await act(r.id, done: nil) { try await $0.settle(r.id, "cancel") }
    }
    func remove(_ f: Grid0Player) async {
        _ = await act(f.id, done: "\(f.nickname) was removed from your friends.") { try await $0.remove(friend: f.id) }
    }

    func dismissNotice() { notice = nil }

    private func act(_ key: String, done: String?, _ body: (Grid0Session) async throws -> Void) async -> Bool {
        guard let session, !busy.contains(key) else { return false }
        busy.insert(key)
        defer { busy.remove(key) }
        do {
            try await body(session)
            error = nil
            notice = done
            await refresh()
            return true
        } catch {
            self.error = error.localizedDescription
            return false
        }
    }
}
