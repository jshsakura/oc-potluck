# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.3.12] - 2026-10-29

### Changed
- Status page: the hero is a plain card with mode tags and its buttons
  beside it on wide screens, and the header lines up with the content. The board headings and numbers are a
  step smaller, the guide cards and footer links carry line icons like the
  docs site, and the header drops its duplicate GitHub button.
  Server image v0.2.9.
- Repo: a privacy check (pre-commit and commit-msg hooks, plus a CI job)
  blocks public IPs, email addresses, tokens and a private denylist of real
  hosts from files and commit messages.

## [0.3.11] - 2026-10-29

### Removed
- Status page: the relay address card. It read the page's own host, so a
  public page showed its real address to anyone. Players get the address
  from whoever runs the relay. Server image v0.2.7.

## [0.3.10] - 2026-10-29

### Changed
- Status page: a title and the line "play local wireless with friends far
  away" lead the page, with the install guide and GitHub. A card shows this
  relay's address (the page's own host on port 30456) with a copy button,
  and five cards under the room list open the install, overlay, settings,
  troubleshooting and relay guides on GitHub Pages. GitHub is also in the
  header. Rooms, players and games sit in icon cards. Server image v0.2.6.

## [0.3.9] - 2026-10-29

### Added
- Relay server: the "who's on" page can be served by the relay container
  itself. Set `LDN_WEB_ENABLED=true` and it answers on the status port
  (127.0.0.1:30457 by default, `LDN_STATUS_BIND_IP` to change), from files
  read once at start; no request path reaches the file system. HTML gets
  `LDN_SITE_ORIGIN` for the share card. Off by default, and `/rooms` works
  as before either way. The nginx example now proxies the page instead of
  serving copied files. Server image v0.2.5.
- Relay server: the status port limits itself so it can face the internet
  without a proxy and never slow the relay: 256 connections, 8 per address,
  5 requests a second per address with bursts of 40 (429 past that), 10 s to
  send a request, 30 s idle, 16 KB per request. A proxy needs no setting:
  a loopback or private peer's X-Real-IP names the viewer, and a public
  peer's headers are never believed. nginx stays optional, for HTTPS.

### Changed
- Status page: one screen, the live room list, with a short line and a
  button to the introduction and install guide on GitHub Pages. The long
  introduction, install steps and troubleshooting moved there.

## [0.3.8] - 2026-10-29

### Fixed
- Module: what a game leaves behind no longer outlives it. bsd:u sessions a
  game opened and closed without using stayed open on the real service, and
  were released only after a relay LDN session; a game played without one
  left them behind, and they piled up with every launch. The per-game
  session count also kept one entry per launch. Both are now released when
  the next game opens its first socket session.
- Module: the relay address was copied onto itself on every connect, which
  is undefined behavior.

### Added
- CI: the unit tests also run under AddressSanitizer, LeakSanitizer and
  UndefinedBehaviorSanitizer.

### Changed
- Overlay: one line under the mode row says what the mode needs. Remote
  mode without a server address says it plays local, and local mode says it
  only reaches consoles on the same Wi-Fi router. A mode change says, for a
  few seconds, that it applies once the game is closed and started again,
  instead of "from the next game". Remote mode with a server adds nothing.

## [0.3.7] - 2026-10-29

### Fixed
- Firmware 23: PotLuck is built against Atmosphere-libs for 23.0.1, which
  knows the shadow stack region firmware 23.0.0 added. The old libraries
  only knew 22.1.0, and a console on firmware 23 could crash when the next
  game launched.

### Changed
- Overlay: built with size optimization, link-time optimization and unused
  section removal, from 1,074 KB to 947 KB.
- Supported firmware is stated: up to 23.0.1, with Atmosphere 1.12.0 or
  newer. On a newer firmware the installer and the overlay show a warning,
  and the module log records it. CI fails when the stated version and the
  one Atmosphere-libs knows disagree.

## [0.3.6] - 2026-10-28

### Fixed
- Module: a game's room search was read eight bytes off. Games send the
  channel, six bytes of padding and then the scan filter; PotLuck read the
  filter from the start, so its scene id came out of the middle of the
  game's LocalCommunicationId, its flags and network type read as 0, and
  every room handed back to the game was stamped with that wrong scene id.
  Games that check the scene id, such as Splatoon 3, threw every room away
  and showed "room not found" while the relay held the room. Scan and
  ScanPrivate now take the channel and filter as one block, for relay and
  local mode alike.
- Log: the file stops at 2 MB and moves to `potluck.log.1`, so at most
  4 MB stays on the SD card. A verbose log of one evening had reached 40 MB,
  97% of it two lines written every time a game polled its socket; those,
  GetState and the overlay's once-a-second status queries are no longer
  logged.
- Local mode: when setting up the console's network fails (the game then
  shows a code such as 2110-0106), the log names the step and the code.

## [0.3.5] - 2026-10-28

### Added
- Relay server: a read-only room list for a status page (`GET /rooms`),
  off unless `LDN_STATUS_PORT` is set. `compose.server.yml` turns it on and
  publishes it on `127.0.0.1:30457` only, so it is reachable through a
  reverse proxy on the same host and nowhere else. Rooms with a passphrase
  show their size but never player names. Server image v0.2.4.
- `web/`: a status page in the CookingFoil style that shows which games
  have rooms open, how full each room is, and who is in the public ones,
  refreshed every 10 seconds. It comes with an og-image and a host nginx
  example (`deploy/nginx/potluck-status.conf`) with HTTPS, a strict CSP and
  a rate limit on the room list. `deploy/web-publish.sh` publishes the page
  and fills in the host for the share card; the repository keeps example
  addresses only.
- Integration test: the room list names players in a public room, hides
  them in a room with a passphrase, and answers nothing but `/rooms`.

## [0.3.4] - 2026-10-28

### Performance
- Relay server: game data is relayed on the thread that received it,
  straight from the receive buffer, whenever nothing else of that client
  is waiting to be handled; anything else still goes through the client's
  queue in order. Each frame is built with one allocation, and the thread
  pool starts with room for a burst. Server image v0.2.3.
  Measured on loopback (two clients relaying 1200 byte game data back and
  forth through the server, 2000 round trips, old and new server up side
  by side, six runs each): p50 about 238 us to 186 us, p95 about 380 us
  to 265 us, p99 about 790 us to 370 us. These are local figures; over the
  internet the network itself dominates.
- Module: the relay receive thread goes straight back to the socket after
  receiving instead of resting 1 ms after every batch.

### Added
- Integration tests: a relay round-trip benchmark, and `ONLY=<name>` to
  run selected tests.

## [0.3.3] - 2026-10-28

### Fixed
- Relay server: what waits to go out to one client is capped at 2 MiB. A
  client that reads slower than its room talks is closed instead of
  growing the server's memory without end (the receive side was already
  capped). A scan answers with at most 24 rooms, as nn::ldn does. Server
  image v0.2.2.
- Relay server: the host can reject any member by node ID. After a member
  left, a higher node ID was compared with the head count and refused.
- The relay send's 5 second deadline also holds when the server takes a
  byte at a time; it was only checked after the socket was full.
- poll() with more than 64 entries no longer loses its proxy sockets: a
  longer array borrows a heap copy (up to 1024 entries) and proxy entries
  are tracked apart from it, so one proxy among many ignored entries is
  still woken.
- A game's nonblocking send (MSG_DONTWAIT or O_NONBLOCK) on an LDN socket
  never waits for the relay: if the connection is busy and nothing went
  out it gets EAGAIN, as from a full kernel socket. A frame that has
  started is always finished, so the stream never carries half of one.

## [0.3.2] - 2026-10-28

### Added
- Log: every control packet to and from the relay is one line, such as
  `-> server: CreateAccessPoint (376 B)` or `<- server: Connected (1164 B)`.
  Game data is not logged per packet; while it flows, one line every
  5 seconds gives the packets and kilobytes each way. Pings are logged
  only at the verbose level.
- Overlay: the status card adds game data sent, game data received (packets
  and size) and the newest message from the server, in words.
- Latency: the module measures its round trip to the relay every 5 seconds
  and the overlay shows it. It was never measured before, so the latency
  row always read "-". The relay server (image v0.2.1) answers the
  module's measuring ping; a server that does not answer only leaves the
  latency unknown and never drops the connection.

## [0.3.1] - 2026-10-28

### Added
- Installer: every menu row has a line icon, the same set as the
  CookingFoil client (mostly redrawn from Lucide, ISC; the license is in
  the app's romfs).
- Installer: a Language row on the main menu picks the console's language,
  English or Korean. The choice is saved in
  `config/potluck_installer/language.txt` and applies at once.

## [0.3.0] - 2026-10-28

### Changed
- The relay server is now part of this repository (`server/src`) instead of
  a submodule pointing at an outside project. It started from the
  MIT-licensed Ryujinx LDN server and is maintained here. The server image
  is v0.2.0.

### Security
- Relay server: joining a room checks the channel. A room ID from another
  channel is refused as not found, and a room whose host rejects all
  stations refuses new ones.
- Relay server: a session belongs to one room at a time. Joining the same
  room again is refused instead of taking a second slot, and joining
  another room leaves the old one first, so no ghost players or stale
  addresses stay behind. A full room is checked before anything changes.
- Relay server: a room size outside 1 to 8 and advertise data over 384
  bytes are refused; the advertise size can no longer disagree with the
  data.
- Relay server: a relay packet whose length field does not match its
  payload is dropped.
- Relay server: each session may have at most 4 MiB or 4096 messages
  waiting; a malformed message, a negative length or a flood closes that
  session. Only finished messages count as activity, so a frame sent a
  byte at a time no longer keeps a session alive, and a session that never
  initializes is closed after 30 seconds.
- Relay server: the MAC address memory used for reconnects is bounded.

### Fixed
- Relay server: a player leaving could leave a ghost node in the room
  when slots before it had been freed.
- Relay server: scans no longer sleep 200 ms on the session's thread, which
  delayed that player's relay data. A repeated scan with the same filter
  within 200 ms reuses the last result.

### Performance
- Relay server: a broadcast relay packet is encoded once instead of once
  per player, and routing reads a membership snapshot without taking the
  room lock.
- Relay server: a session no longer owns an OS thread. Its messages wait
  in a bounded queue and are handled in order on the thread pool, so the
  container's process limit no longer caps the number of players near
  250.
- Relay server: a session's receive buffer is 4 KiB instead of 128 KiB;
  a larger frame borrows a pooled buffer while it is assembled.
- Relay server: room changes are folded into one statistics recount
  instead of starting one per change.
- Relay server: built for .NET 10 as a native AOT binary tuned for speed,
  with a single workstation heap and no background GC thread.
- Relay server: checking a host's P2P port waits on the connect instead
  of spinning for up to 2 seconds.

## [0.2.9] - 2026-10-28

### Fixed
- Installer touch: the L and R page keys in the game list can be tapped
  (L goes back a page, R and its label go forward); a row half under the
  header or the footer can be tapped; the page no longer moves under a
  finger until it has travelled 16 pixels, so a tap lands on the row it was
  drawn on; a greyed row ignores A and taps alike.
- select reached the module in the wrong shape. bsd:u sends nfds and the
  timeout inline and six buffers; the handler expected the timeout as a
  seventh buffer, so a game's select could not be served.
- poll and select on LDN proxy sockets returned at once instead of
  waiting, turning a game's wait into a busy loop of IPC calls. They now
  sleep until a proxy socket changes, the timeout passes or the game
  exits. Proxy entries are hidden from the real service, and real sockets
  that share a wait with proxy sockets are checked every 10 ms, so proxy
  data wakes the wait at once.
- The relay connection's send could block without limit when the server
  stopped reading. Sending never blocks in the kernel now and a frame has
  5 seconds in all; a frame cut short drops the connection, so the stream
  never carries half a frame.
- A server packet larger than the receive buffer stalled every packet
  behind it; it is dropped with a warning. The receive thread handles at
  most 16 packets per pass and a frame's reassembly shares one timeout, so
  a busy or slow peer cannot keep it from stopping when a game exits.
- A datagram that arrived before its socket was bound and was too large to
  park was cut to 1400 bytes; it is dropped whole now, and parked
  datagrams go only to a socket bound to their address.
- LAN mode: a short TCP write left half a frame on the stream; the peer is
  dropped instead.
- Every relay data packet logged at INFO and forced an SD card flush;
  packet logs are verbose only and no packet path flushes.

## [0.2.8] - 2026-10-28

### Changed
- One switch for the log: `[debug] enabled = 1` writes
  `config/potluck/potluck.log`. `log_to_file` is gone (an old line is
  ignored), since a sysmodule has no console and a log you cannot read
  helps nobody. The installer's P2P and diagnostics page drops its separate
  file row and shows where the log goes.
- Turning the log on or off, or changing its level, applies at once from
  the installer or the overlay. It needed a restart before.

## [0.2.7] - 2026-10-28

### Changed
- Logs moved to `config/potluck/` (`potluck.log`, `overlay-startup.log`),
  next to the installer's `config/potluck_installer/`. Settings stay in
  `atmosphere/contents/4200000000504C4B/config.ini`. Removing PotLuck from
  the installer deletes the log folder too.

## [0.2.6] - 2026-10-28

### Fixed
- The console no longer freezes when the next app starts after a game that
  used the network exits. PotLuck kept its copy of the game's socket memory
  handle, so the exited game's memory was never released and the next
  launch could not start.
- Remote play works again. Every socket reply carries the result first and
  errno second, but PotLuck read and wrote them the other way round. A
  game's second socket looked like a failure, was never tracked, and its
  traffic went to Wi-Fi instead of the relay (Splatoon 3 hosted rooms
  nobody could see). Replies PotLuck answers itself (relay receive, bind,
  accept and the rest) now carry the right result, and accept, getpeername,
  getsockname, getsockopt and sysctl return their length field.

## [0.2.5] - 2026-10-27

### Fixed
- The console no longer freezes when another app starts after a game
  exits. A game's blocking calls on its normal sockets (recv, accept, poll,
  select, connect) were forwarded on PotLuck's two IPC threads and stayed
  there once the game was gone, so nothing answered sm's MITM queries and
  the whole console stopped. Poll is now forwarded in 100 ms slices, and
  recv, recvfrom, read and accept wait in the same slices until their
  socket is readable, honoring non-blocking sockets and SO_RCVTIMEO.
  Between slices the call checks the application pid that the maintenance
  thread reads from pm every 100 ms; once the game is gone the call ends
  with EINTR and the thread is free. A wait longer than one
  slice starts a spare thread. Relay connects and the end of a relay
  session also keep a thread free.
- Overlay: the hints under the settings fit on one line.
- Overlay keypad: keys, the input field and the buttons are drawn a step
  lighter than the panel so they read as keys, the chosen key is filled
  yellow, and the key legend under the buttons is gone.
- A mixed poll (relay and real sockets) added the relay sockets to the
  errno field instead of the count.
- Proxy socket waits notice a game exit through pm. The kernel's process
  list still shows a game that exited while the socket service maps its
  memory, so the old check never fired.
- Installer: installed and newest version share one line, and the last
  menu entry reads "Uninstall the PotLuck module".
- Installer: a tap on a menu row runs it, and a tap on a footer key presses
  that button. Touch only scrolled before.
- Installer: the ldn_mitm help card says plainly that ldn_mitm is not
  needed, and the warning when both start at boot says PotLuck stays idle
  (the console still boots) instead of claiming a boot failure.

### Added
- Mode "Off" beside remote and local. New games stop going through PotLuck
  at once, and from the next boot nothing is intercepted at all; the
  overlay can turn it back on (after a reboot).

### Changed
- Overlay: the mode row shows the mode games play in, so remote without a
  server reads as local. Without a server address the row offers only local
  and off; with one, picking remote tests the server at once.
- Overlay: the link reads "Waiting for a game" until a game opens local
  play, since the relay is only dialed then.
- Overlay main page: the status card, then LAN Play mode, a connection
  test (remote only) and Settings, which holds the channel, P2P, force
  reconnect, server address and port. The status card and the keypad
  field drop the yellow side bar and stand out by a lighter tone.

## [0.2.4] - 2026-10-27

### Fixed
- Overlay: the status rows scroll with the list. They were an element that
  set its own position, so once the longer settings list scrolled they
  stayed at the top, overlapped the rows below and cut the yellow cursor.
- Overlay: the main page is the status card and one Settings row, and the
  settings (mode, channel, P2P, force reconnect, server address, port) have
  their own page, so neither page scrolls. Saving an address tests it at
  once and shows the result.
- Overlay keypad: the D-Pad reaches the new Cancel, Delete and Save buttons
  under the keys; B cancels at once and X deletes one character.
- Overlay: a name DNS cannot find is reported as such instead of "set a
  server address first".
- Installer: long screens (help, the safety check) scroll instead of being
  cut at the footer: right stick or a touch drag, and the page follows the
  menu cursor. The right stick no longer moves the cursor as well.

## [0.2.3] - 2026-10-27

### Added
- The installer restarts the console: "Restart the console" on the main
  screen, and "Restart the console now" after an install, update or removal.
  It uses the normal system restart, so running software closes first.

## [0.2.2] - 2026-10-27

### Fixed
- The installer's NACP marks only the languages it has text for (English and
  Korean) instead of nacptool's eight, so catalogs show the right languages.
- Release notes carry the version in their title instead of `${VERSION}`.

## [0.2.1] - 2026-10-27

### Added
- The overlay sets the relay server again: address and port on an on-screen
  keypad that also takes touch, a connection test, and the P2P switch. The
  port row shows the default, 30456, instead of "default".

### Changed
- The overlay's status rows are drawn without the panel that was cut off at
  the top.
- The installer's port row shows `30456 (default)`, and when the system
  keyboard does not open it says so instead of doing nothing.
- The installer's safety check shows what the running system confirmed, and
  a serial the system reports blanked with blank_prodinfo on is shown green as
  safe; open DNS alone no longer raises the red ban warning.
- The installer applies saved settings at once by asking the module to read
  config.ini again; a reload that cannot read the file keeps the live
  settings instead of falling back to defaults, and checks the address.
- The installer's connection test tells a missing name, an IPv6-only name, a
  silent DNS server and a closed port apart.
- The installer moves with the left stick too, lists Remove last, names the
  game list "LAN-play games", and its help is split into titled cards.
- The READMEs and the overlay installer README carry the app icon and
  release, build and license badges; release notes show the icon.

## [0.2.0] - 2026-10-27

### Added
- The installer homebrew app, `potluck-installer.nro`, attached to every
  release. It installs, updates and uninstalls PotLuck from the latest
  release, moves older ryu_ldn_nx installs, sets the server address, tests the
  connection and shows diagnostics. Every SD change is staged and journaled,
  so a failure or power loss rolls back on the next start. It turns off only
  ldn_mitm's autostart and turns it back on when PotLuck is uninstalled.
- Local mode: rooms on the same Wi-Fi network, using the LAN implementation of
  [ldn_mitm](https://github.com/spacemeowx2/ldn_mitm) by spacemeowx2 (GPL-2.0)
  and its protocol, so PotLuck plays with ldn_mitm users. The overlay switches
  between remote and local; a game uses the mode set when it started.

### Changed
- `[ldn] enabled` is replaced by `[ldn] mode = remote | local`. The module is
  always on; the overlay's LAN play switch became the mode row, and the
  channel and force reconnect rows show only in remote mode.
- There is no default relay server any more. A fresh install starts in remote
  mode and, until a server address is set, plays as local mode; clearing the
  address does the same.
- When another module (ldn_mitm) already holds `ldn:u` at boot, PotLuck stays
  idle instead of stopping the console with a fatal error, and the overlay
  says so.

## [0.1.0] - 2026-10-27

First release as PotLuck, an independent continuation of
[ryu_ldn_nx](https://github.com/Ethiquema/ryu_ldn_nx) by Ethiquema (GPL-2.0).

### Changed
- Renamed to PotLuck: title ID `4200000000504C4B`, service `potluck`,
  overlay `switch/.overlays/potluck.ovl`, release asset `potluck-v*.zip`.
- Settings, log and game data moved into the module folder
  `atmosphere/contents/4200000000504C4B/`; `config.ini` is created on first
  boot and no `*.example` file is installed.
- The overlay is now an in-game check titled `potluck.`: a status card (game,
  relay, room size, latency) and three settings (LAN play, channel, force
  reconnect), in the CookingFoil butter palette following the console theme.
  Starting or stopping the module, the server address, P2P, the connection
  test and diagnostics moved to the installer.
- The room passphrase is shown as a channel: players with the same 8-digit
  code see each other's rooms; empty is the public channel.
- Saving settings rewrites only the `config.ini` lines whose value changed and
  keeps comments, spacing and line endings. The removed keys `use_tls`,
  `ping_interval` and `interface` are ignored.

### Added
- Settings from `config/ryu_ldn_nx/config.ini` are migrated on first boot.
- TCP over LDN in relay mode (connect, accept, stream data, close).
- Spare IPC threads that start only while games block on proxy sockets.
- Invalid `[server] host` or `port` values fall back to the default server
  with a warning in the log.

### Fixed
- Self-deadlocks after disconnects that left games unable to exit.
- Use-after-free of proxy sockets closed by another IPC thread.
- A failed or interrupted settings save no longer loses `config.ini`: the
  new file is written beside it and swapped in through a backup that is
  restored on the next boot if needed.
