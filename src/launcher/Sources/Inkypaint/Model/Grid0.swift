// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the inkypaint contributors
import Foundation
import Network
import Security

/// GRID0+, the community's replacement for the console's servers. The launcher signs in there and manages the
/// account's friends; the game does the same through the runtime (src/nx/hle/services/network/grid0.c), with the
/// password the launcher hands it when it starts.
///
/// The server is dialled at its address and named by the console's host it stands in for: SwitchNet's edge routes on
/// the TLS server name and the Host header. The connection is the Mac's own TLS (Network.framework), and the server is
/// trusted only when its certificate chains to the SwitchNet Local CA and names that host.
enum Grid0 {
    static let defaultServer = "89.168.58.206"
    /// The host a console signs in on: the sign-in and the emulator API are served there
    static let accountHost = "e0d67c509fb203858ebcb2fe3f88c2aa.baas.nintendo.com"
    /// The executable of 11.3.0, whose patches GRID0+ publishes (its build ID)
    static let gameBuild = "28C4287AEE36F7499DA60F3E68B54C70DA382D75"

    /// The SwitchNet Local CA's certificate: public, it grants nothing on its own
    static let ca: SecCertificate = {
        let der = Data(base64Encoded: caBase64, options: .ignoreUnknownCharacters)!
        return SecCertificateCreateWithData(nil, der as CFData)!
    }()
    private static let caBase64 = """
        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
        qLd+MwP25gjGF4afbc5D5HEhRO145t4=
        """

    /// "host" or "host:port" into the host and the port (443 without one)
    static func endpoint(_ server: String) -> (host: String, port: UInt16)? {
        let parts = server.trimmingCharacters(in: .whitespaces).split(separator: ":", omittingEmptySubsequences: false)
        guard let host = parts.first, !host.isEmpty, parts.count <= 2 else { return nil }
        guard parts.count == 2 else { return (String(host), 443) }
        guard let port = UInt16(parts[1]), port > 0 else { return nil }
        return (String(host), port)
    }

    /// The chain must end at the CA (no other root counts), and the leaf must name the host. The CA's certificates
    /// are long-lived, which the system's own TLS policy refuses for a server, so the chain is checked with the basic
    /// policy and the name here.
    static func verify(_ trust: SecTrust, host: String) -> Bool {
        SecTrustSetPolicies(trust, SecPolicyCreateBasicX509())
        SecTrustSetAnchorCertificates(trust, [ca] as CFArray)
        SecTrustSetAnchorCertificatesOnly(trust, true)
        var error: CFError?
        guard SecTrustEvaluateWithError(trust, &error),
              let chain = SecTrustCopyCertificateChain(trust) as? [SecCertificate], let leaf = chain.first
        else { return false }
        return dnsNames(leaf).contains { matches($0, host) }
    }

    static func dnsNames(_ cert: SecCertificate) -> [String] {
        guard let values = SecCertificateCopyValues(cert, [kSecOIDSubjectAltName] as CFArray, nil) as? [String: Any],
              let san = values[kSecOIDSubjectAltName as String] as? [String: Any],
              let entries = san[kSecPropertyKeyValue as String] as? [[String: Any]]
        else { return [] }
        return entries.compactMap { entry in
            guard (entry[kSecPropertyKeyLabel as String] as? String)?.localizedCaseInsensitiveContains("DNS") == true
            else { return nil }
            return entry[kSecPropertyKeyValue as String] as? String
        }
    }

    /// A certificate's name for a host: the same, or a wildcard for one label
    static func matches(_ name: String, _ host: String) -> Bool {
        let name = name.lowercased(), host = host.lowercased()
        if name == host { return true }
        guard name.hasPrefix("*."), let dot = host.firstIndex(of: ".") else { return false }
        return host[dot...] == name.dropFirst()
    }
}

struct Grid0Response: Sendable {
    let status: Int
    let body: Data

    var ok: Bool { (200..<300).contains(status) }
    var json: [String: Any]? { (try? JSONSerialization.jsonObject(with: body)) as? [String: Any] }
    /// What the server says went wrong, for the player
    var message: String {
        let j = json
        return (j?["message"] as? String) ?? (j?["error"] as? String) ?? (j?["detail"] as? String)
            ?? (j?["title"] as? String) ?? "The server answered \(status)."
    }
}

enum Grid0Error: LocalizedError {
    case badServer(String)
    case unreachable(String)
    case refused(String)
    case badAnswer

    var errorDescription: String? {
        switch self {
        case .badServer(let s): "\"\(s)\" isn't a server address. Use host or host:port."
        case .unreachable(let why): "Couldn't reach GRID0+: \(why)"
        case .refused(let why): why
        case .badAnswer: "GRID0+ sent an answer inkypaint doesn't understand."
        }
    }
}

/// One HTTP/1.1 request on a connection of its own (Connection: close), over the Mac's TLS
enum Grid0HTTP {
    static func request(server: String, host: String, method: String, path: String,
                        headers: [String: String] = [:], body: Data? = nil) async throws -> Grid0Response {
        guard let end = Grid0.endpoint(server), let port = NWEndpoint.Port(rawValue: end.port) else {
            throw Grid0Error.badServer(server)
        }
        var head = "\(method) \(path) HTTP/1.1\r\nHost: \(host)\r\nUser-Agent: inkypaint\r\n"
            + "Accept: application/json\r\nConnection: close\r\nContent-Length: \(body?.count ?? 0)\r\n"
        for (k, v) in headers { head += "\(k): \(v)\r\n" }
        var request = Data((head + "\r\n").utf8)
        if let body { request.append(body) }

        let tls = NWProtocolTLS.Options()
        let sec = tls.securityProtocolOptions
        sec_protocol_options_set_tls_server_name(sec, host)
        sec_protocol_options_set_min_tls_protocol_version(sec, .TLSv12)
        sec_protocol_options_add_tls_application_protocol(sec, "http/1.1")
        sec_protocol_options_set_verify_block(sec, { _, trust, complete in
            complete(Grid0.verify(sec_trust_copy_ref(trust).takeRetainedValue(), host: host))
        }, .global(qos: .userInitiated))
        let connection = NWConnection(host: NWEndpoint.Host(end.host), port: port, using: NWParameters(tls: tls))
        let raw = try await Exchange(connection: connection, request: request).run()
        return try parse(raw)
    }

    /// The status line, the headers and the body (a chunked one put together)
    static func parse(_ raw: Data) throws -> Grid0Response {
        guard let split = raw.range(of: Data("\r\n\r\n".utf8)),
              let head = String(data: raw[..<split.lowerBound], encoding: .utf8) else { throw Grid0Error.badAnswer }
        let lines = head.components(separatedBy: "\r\n")
        let status = lines.first?.split(separator: " ").dropFirst().first.flatMap { Int($0) }
        guard let status else { throw Grid0Error.badAnswer }
        var body = Data(raw[split.upperBound...])
        let chunked = lines.dropFirst().contains {
            $0.lowercased().hasPrefix("transfer-encoding:") && $0.lowercased().contains("chunked")
        }
        if chunked {
            var out = Data(), at = body.startIndex
            while let lineEnd = body[at...].range(of: Data("\r\n".utf8)) {
                let sizeText = String(data: body[at..<lineEnd.lowerBound], encoding: .ascii)?
                    .split(separator: ";").first.map(String.init) ?? ""
                guard let size = Int(sizeText.trimmingCharacters(in: .whitespaces), radix: 16), size > 0,
                      lineEnd.upperBound + size <= body.endIndex else { break }
                out.append(body[lineEnd.upperBound..<(lineEnd.upperBound + size)])
                at = min(body.endIndex, lineEnd.upperBound + size + 2)
            }
            body = out
        } else if let length = lines.dropFirst().first(where: { $0.lowercased().hasPrefix("content-length:") })
                    .flatMap({ Int($0.dropFirst(15).trimmingCharacters(in: .whitespaces)) }), length < body.count {
            body = body.prefix(length)
        }
        return Grid0Response(status: status, body: body)
    }
}

/// A connection's one request and whole answer, the continuation resumed once, whatever comes first
private final class Exchange: @unchecked Sendable {
    let connection: NWConnection
    let request: Data
    private let queue = DispatchQueue(label: "inkypaint.grid0")
    private var received = Data()
    private var continuation: CheckedContinuation<Data, Error>?

    init(connection: NWConnection, request: Data) {
        self.connection = connection
        self.request = request
    }

    func run() async throws -> Data {
        try await withCheckedThrowingContinuation { cont in
            queue.async {
                self.continuation = cont
                self.connection.stateUpdateHandler = { [self] state in
                    switch state {
                    case .ready: send()
                    case .failed(let error): finish(.failure(Grid0Error.unreachable(error.localizedDescription)))
                    case .waiting(let error): finish(.failure(Grid0Error.unreachable(error.localizedDescription)))
                    default: break
                    }
                }
                self.connection.start(queue: self.queue)
                self.queue.asyncAfter(deadline: .now() + 15) { [self] in
                    finish(.failure(Grid0Error.unreachable("the server didn't answer in time")))
                }
            }
        }
    }

    private func send() {
        connection.send(content: request, completion: .contentProcessed { [self] error in
            if let error { finish(.failure(Grid0Error.unreachable(error.localizedDescription))) } else { receive() }
        })
    }

    private func receive() {
        connection.receive(minimumIncompleteLength: 1, maximumLength: 1 << 16) { [self] data, _, done, error in
            if let data { received.append(data) }
            if done || error != nil {
                finish(received.isEmpty ? .failure(Grid0Error.unreachable(error?.localizedDescription
                                                                            ?? "the server closed the connection"))
                                        : .success(received))
            } else {
                receive()
            }
        }
    }

    private func finish(_ result: Result<Data, Error>) {
        guard let cont = continuation else { return }
        continuation = nil
        connection.cancel()
        cont.resume(with: result)
    }
}

/// A GRID0+ player: the account signed in, a friend, or the other side of a request
struct Grid0Player: Identifiable, Hashable, Sendable {
    var id: String // the network service account id, hex
    var nickname: String
    var login: String
    var friendCode: String
    var state: String // ONLINE, PLAYING, OFFLINE, or "" when hidden
    var appID: String
    var favorite: Bool

    init(json: [String: Any]) {
        id = json["id"] as? String ?? ""
        login = json["login"] as? String ?? ""
        let nick = json["nickname"] as? String ?? ""
        nickname = nick.isEmpty ? login : nick
        friendCode = json["friendCode"] as? String ?? ""
        state = json["state"] as? String ?? ""
        appID = json["appId"] as? String ?? ""
        favorite = json["isFavorite"] as? Bool ?? false
    }

    var isPlaying: Bool { state == "PLAYING" }
    var isOnline: Bool { state == "ONLINE" || state == "PLAYING" }
    /// What a friend is up to, in words
    var activity: String {
        switch state {
        case "PLAYING": appID.lowercased() == Game.titleID.lowercased() ? "Playing \(Game.name)" : "Playing a game"
        case "ONLINE": "Online"
        case "OFFLINE": "Offline"
        default: "Status hidden"
        }
    }
}

struct Grid0FriendRequest: Identifiable, Hashable, Sendable {
    var id: String
    var other: Grid0Player
}

/// The account's session: its sign-in and the emulator API (/emulator/v1/...) with the access token it gave
actor Grid0Session {
    let server: String
    let username: String
    private let password: String
    private var access: String?

    init(server: String, username: String, password: String) {
        self.server = server
        self.username = username
        self.password = password
    }

    /// POST /login with the user name and password, form encoded: the access token for the API
    func signIn() async throws {
        func escaped(_ s: String) -> String {
            var allowed = CharacterSet.alphanumerics
            allowed.insert(charactersIn: "-._~")
            return s.addingPercentEncoding(withAllowedCharacters: allowed) ?? s
        }
        let form = "username=\(escaped(username))&password=\(escaped(password))"
        let r = try await Grid0HTTP.request(server: server, host: Grid0.accountHost, method: "POST", path: "/login",
                                            headers: ["Content-Type": "application/x-www-form-urlencoded"],
                                            body: Data(form.utf8))
        guard r.ok else { throw Grid0Error.refused(r.status == 401 ? "Wrong user name or password." : r.message) }
        guard let token = r.json?["accessToken"] as? String, !token.isEmpty else { throw Grid0Error.badAnswer }
        access = token
    }

    /// A request to the API, signing in first, and again once when the token has expired
    func call(_ method: String, _ path: String, json: [String: Any]? = nil) async throws -> Grid0Response {
        for attempt in 0..<2 {
            if access == nil || attempt == 1 { try await signIn() }
            var headers = ["Authorization": "Bearer \(access ?? "")"]
            var body: Data?
            if let json {
                headers["Content-Type"] = "application/json"
                body = try JSONSerialization.data(withJSONObject: json)
            }
            let r = try await Grid0HTTP.request(server: server, host: Grid0.accountHost, method: method, path: path,
                                                headers: headers, body: body)
            if r.status != 401 || attempt == 1 { return r }
        }
        throw Grid0Error.badAnswer
    }

    func me() async throws -> Grid0Player {
        let r = try await call("GET", "/emulator/v1/me")
        guard r.ok, let j = r.json else { throw Grid0Error.refused(r.message) }
        return Grid0Player(json: j)
    }

    func friends() async throws -> [Grid0Player] {
        let r = try await call("GET", "/emulator/v1/friends")
        guard r.ok, let list = r.json?["friends"] as? [[String: Any]] else { throw Grid0Error.refused(r.message) }
        return list.map(Grid0Player.init(json:)).filter { !$0.id.isEmpty }
    }

    func requests() async throws -> (incoming: [Grid0FriendRequest], outgoing: [Grid0FriendRequest]) {
        let r = try await call("GET", "/emulator/v1/friend-requests")
        guard r.ok, let j = r.json else { throw Grid0Error.refused(r.message) }
        func read(_ key: String) -> [Grid0FriendRequest] {
            (j[key] as? [[String: Any]] ?? []).compactMap { e in
                guard let id = e["id"] as? String else { return nil }
                return Grid0FriendRequest(id: id, other: Grid0Player(json: e["other"] as? [String: Any] ?? [:]))
            }
        }
        return (read("incoming"), read("outgoing"))
    }

    func sendRequest(friendCode: String) async throws {
        let r = try await call("POST", "/emulator/v1/friend-requests", json: ["friendCode": friendCode])
        guard r.ok else { throw Grid0Error.refused(r.message) }
    }

    /// action is accept, deny or cancel
    func settle(_ request: String, _ action: String) async throws {
        let r = try await call("POST", "/emulator/v1/friend-requests/\(request)/\(action)", json: [:])
        guard r.ok else { throw Grid0Error.refused(r.message) }
    }

    func remove(friend: String) async throws {
        let r = try await call("DELETE", "/emulator/v1/friends/\(friend)")
        guard r.ok else { throw Grid0Error.refused(r.message) }
    }
}

/// The password, in the login Keychain under the user name
enum Grid0Keychain {
    private static let service = "io.github.inkypaint.grid0"

    private static func query(_ account: String) -> [String: Any] {
        [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service,
         kSecAttrAccount as String: account]
    }

    static func save(_ password: String, account: String) throws {
        SecItemDelete(query(account) as CFDictionary)
        var item = query(account)
        item[kSecValueData as String] = Data(password.utf8)
        item[kSecAttrLabel as String] = "inkypaint GRID0+ password"
        let status = SecItemAdd(item as CFDictionary, nil)
        guard status == errSecSuccess else {
            throw Grid0Error.refused("Couldn't keep the password in the Keychain (\(status)).")
        }
    }

    static func load(account: String) -> String? {
        var q = query(account)
        q[kSecReturnData as String] = true
        q[kSecMatchLimit as String] = kSecMatchLimitOne
        var out: CFTypeRef?
        guard SecItemCopyMatching(q as CFDictionary, &out) == errSecSuccess, let data = out as? Data else { return nil }
        return String(data: data, encoding: .utf8)
    }

    static func delete(account: String) { SecItemDelete(query(account) as CFDictionary) }
}
