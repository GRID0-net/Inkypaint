// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import AppKit
import SwiftUI

/// Online play on GRID0+: signing in, the patches the game needs for it, and the account's friends and requests.
struct OnlineView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        let online = model.online
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                hero
                if let error = online.error { Banner(text: error, symbol: "exclamationmark.triangle.fill", tint: .orange) }
                if let notice = online.notice {
                    Banner(text: notice, symbol: "checkmark.circle.fill", tint: Ink.lime) { online.dismissNotice() }
                }
                if online.isSignedIn {
                    AccountCard()
                    if model.isPrepared { PatchesCard() }
                    AddFriendCard()
                    if !online.incoming.isEmpty || !online.outgoing.isEmpty { RequestsCard() }
                    FriendsCard()
                } else {
                    SignInCard()
                }
            }
            .padding(28)
            .frame(maxWidth: 760)
            .frame(maxWidth: .infinity)
        }
        .background { OnlineBackdrop() }
        .navigationTitle("Online")
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Button {
                    Task { await online.refresh() }
                } label: {
                    Label("Refresh", systemImage: "arrow.clockwise")
                }
                .keyboardShortcut("r", modifiers: [.command, .shift])
                .disabled(!online.isSignedIn)
            }
        }
        // while the page shows: the friends every 30 seconds, and the patches once
        .task(id: online.isSignedIn) {
            guard online.isSignedIn else { return }
            if model.isPrepared, online.patches.status == .unknown { await online.patches.update(model.paths) }
            while !Task.isCancelled {
                await online.refresh()
                try? await Task.sleep(for: .seconds(30))
            }
        }
    }

    private var hero: some View {
        HStack(alignment: .center, spacing: 16) {
            SplatMark(splat: .burst, color: Ink.magenta, size: 74, angle: 16)
                .overlay { Image(systemName: "globe").font(.title2.weight(.semibold)).foregroundStyle(.white) }
            VStack(alignment: .leading, spacing: 4) {
                Text(model.online.isSignedIn ? "Online on GRID0+" : "Play online").font(.inkTitle(30))
                Text("Turf War, Salmon Run and your friends, on GRID0+, the community's servers. inkypaint talks to "
                     + "them with your Mac's own TLS: no console firmware needed.")
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .padding(.bottom, 4)
    }
}

private struct OnlineBackdrop: View {
    var body: some View {
        GeometryReader { geo in
            ZStack {
                SplatMark(splat: .drip, color: Ink.magenta.opacity(0.16), size: 340, angle: 160)
                    .position(x: geo.size.width - 40, y: 60)
                SplatMark(splat: .streak, color: Ink.lime.opacity(0.13), size: 300, angle: -25)
                    .position(x: 50, y: geo.size.height - 30)
            }
        }
        .ignoresSafeArea()
    }
}

private struct Banner: View {
    let text: String
    let symbol: String
    let tint: Color
    var dismiss: (() -> Void)? = nil

    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: symbol).foregroundStyle(tint)
            Text(text).font(.callout).fixedSize(horizontal: false, vertical: true)
            Spacer(minLength: 0)
            if let dismiss {
                Button(action: dismiss) { Image(systemName: "xmark") }
                    .buttonStyle(.plain)
                    .foregroundStyle(.secondary)
                    .accessibilityLabel("Dismiss")
            }
        }
        .padding(.horizontal, 16).padding(.vertical, 12)
        .glassEffect(.regular.tint(tint.opacity(0.18)), in: .rect(cornerRadius: 16))
    }
}

// MARK: - signing in

/// GRID0+'s sign-in, on the Online page and in the question before a preparation (FilesView's OnlineFirstSheet)
struct SignInCard: View {
    @Environment(AppModel.self) private var model
    @State private var username = ""
    @State private var password = ""
    @State private var server = ""
    @State private var showServer = false
    @FocusState private var field: Field?
    enum Field { case username, password }

    var body: some View {
        let online = model.online
        GlassCard(tint: Ink.purple) {
            VStack(alignment: .leading, spacing: 14) {
                Text("Sign in to GRID0+").font(.title3.weight(.bold))
                VStack(spacing: 10) {
                    TextField("User name", text: $username)
                        .textContentType(.username)
                        .focused($field, equals: .username)
                        .onSubmit { field = .password }
                    SecureField("Password", text: $password)
                        .textContentType(.password)
                        .focused($field, equals: .password)
                        .onSubmit(signIn)
                }
                .textFieldStyle(.roundedBorder)
                .controlSize(.large)
                .disabled(online.account == .signingIn)

                DisclosureGroup("Server", isExpanded: $showServer) {
                    TextField(Grid0.defaultServer, text: $server)
                        .textFieldStyle(.roundedBorder)
                        .font(.callout.monospaced())
                        .padding(.top, 6)
                }
                .font(.callout)

                HStack {
                    Text("No account yet? Use /register with the GRID0+ bot on the GRID0 Discord server. Your password "
                         + "stays on this Mac.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                    Spacer(minLength: 16)
                    Button(action: signIn) {
                        HStack(spacing: 8) {
                            if online.account == .signingIn { ProgressView().controlSize(.small) }
                            Text("Sign In").font(.headline)
                        }
                        .padding(.horizontal, 8)
                    }
                    .buttonStyle(.glassProminent)
                    .tint(Ink.lime)
                    .foregroundStyle(.black)
                    .controlSize(.large)
                    .keyboardShortcut(.defaultAction)
                    .disabled(username.isEmpty || password.isEmpty || online.account == .signingIn)
                }
            }
        }
        .onAppear {
            username = online.username
            server = online.server == Grid0.defaultServer ? "" : online.server
            field = username.isEmpty ? .username : .password
        }
    }

    private func signIn() {
        guard !username.isEmpty, !password.isEmpty else { return }
        let (u, p, s) = (username, password, server)
        Task {
            await model.online.signIn(username: u, password: p, server: s)
            if model.online.isSignedIn { password = "" }
        }
    }
}

// MARK: - the account

private struct AccountCard: View {
    @Environment(AppModel.self) private var model
    @State private var copied = false

    var body: some View {
        let online = model.online
        GlassCard(tint: Ink.purple) {
            VStack(alignment: .leading, spacing: 16) {
                HStack(spacing: 14) {
                    Avatar(name: online.me?.nickname ?? online.username, id: online.me?.id ?? online.username, size: 56)
                    VStack(alignment: .leading, spacing: 3) {
                        Text(online.me?.nickname ?? online.username).font(.title2.weight(.bold))
                        Text("@\(online.me?.login ?? online.username)").foregroundStyle(.secondary)
                    }
                    Spacer()
                    if let code = online.me?.friendCode, !code.isEmpty {
                        Button {
                            NSPasteboard.general.clearContents()
                            NSPasteboard.general.setString(code, forType: .string)
                            copied = true
                            Task {
                                try? await Task.sleep(for: .seconds(1.5))
                                copied = false
                            }
                        } label: {
                            VStack(alignment: .trailing, spacing: 2) {
                                Text("FRIEND CODE").font(.caption2.weight(.heavy)).foregroundStyle(.secondary)
                                Label(copied ? "Copied" : code, systemImage: copied ? "checkmark" : "doc.on.doc")
                                    .font(.callout.monospaced().weight(.semibold))
                                    .contentTransition(.symbolEffect(.replace))
                            }
                            .padding(.horizontal, 6).padding(.vertical, 2)
                        }
                        .buttonStyle(.glass)
                        .help("Copy your friend code")
                    }
                }
                Divider()
                HStack {
                    Toggle(isOn: Binding(get: { online.playsOnline }, set: { online.setPlaysOnline($0) })) {
                        VStack(alignment: .leading, spacing: 2) {
                            Text("Play online").font(.headline)
                            Text("The game signs in to GRID0+ as you when it starts. Off, it plays offline.")
                                .font(.caption).foregroundStyle(.secondary)
                        }
                    }
                    .toggleStyle(.switch)
                    .tint(Ink.lime)
                    .disabled(model.isPlaying)
                    Spacer(minLength: 16)
                    Button("Sign Out", role: .destructive) { online.signOut() }
                        .buttonStyle(.glass)
                        .disabled(model.isPlaying)
                }
                if model.isPlaying {
                    Text("Changes here apply the next time the game starts.").font(.caption).foregroundStyle(.secondary)
                }
            }
        }
    }
}

/// The patches GRID0+ needs in the game's code, and the recompile that puts them there
private struct PatchesCard: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        let patches = model.online.patches
        let recompile = patches.needsRecompile(model.paths)
        GlassCard(tint: recompile ? Ink.lime : nil) {
            HStack(spacing: 14) {
                Image(systemName: icon(patches.status, recompile))
                    .font(.title2)
                    .foregroundStyle(recompile ? Ink.lime : Ink.purple)
                    .frame(width: 32)
                VStack(alignment: .leading, spacing: 3) {
                    Text("Game patches").font(.headline)
                    Text(detail(patches.status, recompile)).font(.callout).foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
                Spacer(minLength: 12)
                switch patches.status {
                case .checking:
                    ProgressView().controlSize(.small)
                case .failed:
                    Button("Try Again") { Task { await patches.update(model.paths) } }.buttonStyle(.glass)
                default:
                    if recompile {
                        Button("Recompile Now") { model.recompile() }
                            .buttonStyle(.glassProminent)
                            .tint(Ink.lime)
                            .foregroundStyle(.black)
                            .disabled(model.preparer.isRunning || model.isPlaying)
                    } else {
                        Button("Check for Updates") { Task { await patches.update(model.paths) } }.buttonStyle(.glass)
                    }
                }
            }
        }
    }

    private func icon(_ s: Grid0Patches.Status, _ recompile: Bool) -> String {
        if recompile { return "hammer.fill" }
        switch s {
        case .failed: return "exclamationmark.triangle.fill"
        case .current: return "checkmark.seal.fill"
        default: return "bandage.fill"
        }
    }

    private func detail(_ s: Grid0Patches.Status, _ recompile: Bool) -> String {
        if model.preparer.isRunning { return "Recompiling with the patches…" }
        switch s {
        case .unknown: return "GRID0+ patches the game's online client. Checking for them…"
        case .checking: return "Checking GRID0+'s newest patches…"
        case .failed(let why): return why
        case .current(let tag):
            return recompile
                ? "Downloaded (\(tag)). The game needs a recompile to use them. Only the code is redone, about as long as the first time."
                : "Up to date (\(tag)), and compiled into the game."
        }
    }
}

// MARK: - friends

private struct AddFriendCard: View {
    @Environment(AppModel.self) private var model
    @State private var code = ""

    var body: some View {
        GlassCard {
            HStack(spacing: 12) {
                Image(systemName: "person.badge.plus").font(.title3).foregroundStyle(Ink.purple)
                TextField("Friend code, SW-1234-5678-9012", text: $code)
                    .textFieldStyle(.roundedBorder)
                    .font(.body.monospaced())
                    .onSubmit(send)
                Button("Send Request", action: send)
                    .buttonStyle(.glass)
                    .disabled(code.filter(\.isNumber).count != 12 || model.online.busy.contains(code))
            }
        }
    }

    private func send() {
        let c = code
        Task { if await model.online.sendRequest(c) { code = "" } }
    }
}

private struct RequestsCard: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        let online = model.online
        GlassCard {
            VStack(alignment: .leading, spacing: 12) {
                if !online.incoming.isEmpty {
                    Text("Friend requests").font(.headline)
                    ForEach(online.incoming) { r in
                        PlayerRow(player: r.other, subtitle: r.other.friendCode) {
                            GlassEffectContainer(spacing: 8) {
                                HStack(spacing: 8) {
                                    Button("Accept") { Task { await online.accept(r) } }
                                        .buttonStyle(.glassProminent)
                                        .tint(Ink.lime)
                                        .foregroundStyle(.black)
                                    Button("Decline") { Task { await online.decline(r) } }
                                        .buttonStyle(.glass)
                                }
                            }
                            .disabled(online.busy.contains(r.id))
                        }
                    }
                }
                if !online.outgoing.isEmpty {
                    if !online.incoming.isEmpty { Divider() }
                    Text("Sent").font(.headline)
                    ForEach(online.outgoing) { r in
                        PlayerRow(player: r.other, subtitle: "Waiting for an answer") {
                            Button("Cancel") { Task { await online.cancel(r) } }
                                .buttonStyle(.glass)
                                .disabled(online.busy.contains(r.id))
                        }
                    }
                }
            }
        }
    }
}

private struct FriendsCard: View {
    @Environment(AppModel.self) private var model
    @State private var removing: Grid0Player?

    var body: some View {
        let online = model.online
        GlassCard {
            VStack(alignment: .leading, spacing: 12) {
                HStack {
                    Text("Friends").font(.headline)
                    if !online.friends.isEmpty {
                        Text("\(online.friends.filter(\.isOnline).count) online of \(online.friends.count)")
                            .font(.callout).foregroundStyle(.secondary)
                    }
                    Spacer()
                    if let refreshed = online.refreshed {
                        Text("Updated \(refreshed.formatted(date: .omitted, time: .shortened))")
                            .font(.caption).foregroundStyle(.tertiary)
                    }
                }
                if online.friends.isEmpty {
                    ContentUnavailableView {
                        Label("No friends yet", systemImage: "person.2")
                    } description: {
                        Text("Share your friend code, or send a request with someone else's.")
                    }
                    .frame(maxWidth: .infinity)
                } else {
                    ForEach(online.friends) { f in
                        PlayerRow(player: f, subtitle: f.activity, presence: true) {
                            if f.favorite {
                                Image(systemName: "star.fill").foregroundStyle(.yellow).help("Favourite")
                            }
                        }
                        .contextMenu {
                            Button("Copy Friend Code") {
                                NSPasteboard.general.clearContents()
                                NSPasteboard.general.setString(f.friendCode, forType: .string)
                            }
                            .disabled(f.friendCode.isEmpty)
                            Divider()
                            Button("Remove Friend…", role: .destructive) { removing = f }
                        }
                    }
                }
            }
        }
        .confirmationDialog("Remove \(removing?.nickname ?? "") from your friends?",
                            isPresented: Binding(get: { removing != nil }, set: { if !$0 { removing = nil } })) {
            Button("Remove", role: .destructive) {
                if let f = removing { Task { await online.remove(f) } }
                removing = nil
            }
        } message: {
            Text("You can send them a request again later.")
        }
    }
}

/// A player in a list: their picture, name, a line under it, and what goes at the end
private struct PlayerRow<Trailing: View>: View {
    let player: Grid0Player
    let subtitle: String
    var presence = false
    @ViewBuilder var trailing: Trailing

    var body: some View {
        HStack(spacing: 12) {
            Avatar(name: player.nickname, id: player.id, size: 38)
                .overlay(alignment: .bottomTrailing) {
                    if presence {
                        Circle()
                            .fill(player.isPlaying ? Ink.lime : player.isOnline ? Color.blue : Color.gray.opacity(0.6))
                            .frame(width: 12, height: 12)
                            .overlay { Circle().stroke(.background, lineWidth: 2) }
                            .offset(x: 2, y: 2)
                    }
                }
            VStack(alignment: .leading, spacing: 2) {
                Text(player.nickname.isEmpty ? player.login : player.nickname).font(.body.weight(.semibold))
                Text(subtitle).font(.caption).foregroundStyle(player.isPlaying && presence ? AnyShapeStyle(Ink.lime) : AnyShapeStyle(.secondary))
            }
            Spacer(minLength: 8)
            trailing
        }
        .padding(.vertical, 2)
        .accessibilityElement(children: .combine)
    }
}

/// A player's initial on a splat of their own colour
private struct Avatar: View {
    let name: String
    let id: String
    let size: CGFloat

    private var color: Color {
        let colors = [Ink.purple, Ink.magenta, Color.teal, Color.orange, Color.indigo, Color.pink]
        let sum = id.unicodeScalars.reduce(0) { $0 &+ Int($1.value) }
        return colors[sum % colors.count]
    }

    var body: some View {
        ZStack {
            SplatMark(splat: .burst, color: color, size: size, angle: Double(id.count * 29))
            Text(name.first.map { String($0).uppercased() } ?? "?")
                .font(.inkTitle(size * 0.42))
                .foregroundStyle(.white)
        }
        .frame(width: size, height: size)
        .accessibilityHidden(true)
    }
}
