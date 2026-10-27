# multiplayer!

LAN through GRID0 ZeroTier, or online through GRID0+! these are two separate ways to play.

## LAN / GRID0 ZeroTier

use the game's LAN mode with everyone on the same LAN, or join the same GRID0 ZeroTier network to do it over
the internet. ZeroTier gives you the shared network, the game still uses LAN play.

in the launcher:

1. enable **Network** in **Settings > LAN play**.
2. if you're using ZeroTier, select its adapter in **LAN adapter**. auto normally picks your internet connection,
   which might not be the ZeroTier one!
3. use the game's LAN mode to host or join with your friends.

you don't need a GRID0+ account for this. everyone needs to be on the same game version too, this port uses 11.3.0.

if you're editing the game's `.ini` directly, these go under `[Network]`:

```ini
network=1
lan_adapter=virtual
```

`virtual` looks for an active ZeroTier, Hamachi or Radmin adapter. you can also give `lan_adapter` a specific
adapter name or IPv4 address. empty or `auto` uses the default-route adapter. `all` sends broadcasts over every
adapter, but it's experimental, so picking the right adapter is preferable.

## GRID0+

the NSO replacement! this is for the game's online path, using GRID0+'s community servers.

sign in on the launcher's **Online** page, enable online play, and let it install the game patches. the patches
change game code, so they need a recompile before they'll take effect. signing in before the first setup avoids
doing that recompile twice.

the launcher sets `network=1`, `grid0=1`, `grid0_server` and `grid0_user`, then passes your password to the game
through stdin. it doesn't put the password in the command line or the game's `.ini`.

inkypaint redirects the relevant server names, supplies account and friend services, and provides certificates
for the game's TLS. the trusted roots come from the host with the SwitchNet CA added, so firmware isn't needed
for that certificate store. this isn't just turning certificate verification off!

launcher-specific details, including credential storage, are in the [macOS](../src/launcher/README.md) and
[Windows / Linux](../src/launcher-qt/README.md) docs.

## If it doesn't connect

say whether you're using LAN / ZeroTier or GRID0+, what happens when you try to join, and whether the other player
sees a disconnect. include the game log and your OS, CPU and GPU. a game hanging before it draws a window doesn't
by itself tell us the network is the problem!

[here's where to find the logs and what to include](TESTING.md).

## Underneath it

these are the runtime details if you're working on networking rather than just playing!

- `src/common/network/` handles host TCP/UDP sockets, polling, cancellation, timeouts, DNS and adapter discovery.
- `src/nx/hle/services/network/` implements the game's BSD, DNS, NIFM and GRID0+ service interfaces.
- `net_iface.c` finds the LAN adapter. `bsd.c` uses it for the address the game sees and where LAN broadcasts go.
- `grid0.c` handles GRID0+ sign-in and account integration. the launchers handle downloading its game patches.

this builds on Eden's networking design and service behavior from `lizzie/refactor-networking-12`, revision
`2014a5778273eef450bf3a48530d703750487a18`, mainly `src/core/internal_network/` and its sockets/NIFM services.
the adapted source keeps its Eden/yuzu notices. it's a C adaptation rather than a vendored copy of the C++ runtime.

host sockets stay nonblocking. a guest's blocking operation waits for readiness with its deadline, and cancellation
wakes those waits. guest descriptors refer to runtime-owned sockets, not raw host descriptors. the ABI tables in
`abi/*.inc` translate errors, flags, options and addresses between Horizon and the host.

local wireless (`net_ldn.c`) is separate from the game's native LAN mode. `ldn=lan` uses the ldn_mitm protocol over
UDP/TCP 11452. the inherited `ldn=room` option uses an Eden room through ENet, relaying local-wireless messages and
game UDP datagrams. that room support isn't what GRID0+ uses, and it isn't required for a ZeroTier LAN setup.

with `network=0`, NIFM reports offline and the runtime permits loopback traffic and numeric/localhost resolution.
the blocked-host list is a resolver policy, not a firewall or a security sandbox.

## Testing the runtime

`network_stack`, `network_icmp`, `bsd_service`, `ldn_room` and `room_service` cover sockets, service replies,
cancellation and the inherited room protocol. GRID0+ has service tests too. use the suites configured for your
platform, see [development](DEVELOPMENT.md).

passing those tests doesn't prove a real match works! actual LAN and GRID0+ matches still need testing with other
players, especially when changing timing, socket behavior, or the battle transition.
