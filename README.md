# PotLuck

<p align="center">
  <img src="overlay/potluck.jpg" width="160" alt="PotLuck icon">
</p>

<p align="center">
  <a href="https://github.com/jshsakura/oc-potluck/releases/latest"><img src="https://img.shields.io/github/v/release/jshsakura/oc-potluck?label=release" alt="Latest release"></a>
  <a href="https://github.com/jshsakura/oc-potluck/releases"><img src="https://img.shields.io/github/downloads/jshsakura/oc-potluck/total" alt="Downloads"></a>
  <a href="https://github.com/jshsakura/oc-potluck/actions/workflows/build.yml"><img src="https://github.com/jshsakura/oc-potluck/actions/workflows/build.yml/badge.svg" alt="Build"></a>
  <a href="LICENSE"><img src="https://img.shields.io/github/license/jshsakura/oc-potluck" alt="License: GPL-2.0"></a>
</p>

[English](README.md) · [한국어](README.ko.md) · [Downloads](https://github.com/jshsakura/oc-potluck/releases/latest) · [Documentation](https://jshsakura.github.io/oc-potluck/)

PotLuck lets Nintendo Switch games that support local wireless play (LDN) find each other in two ways:

- **Remote:** consoles in different places meet through a relay server you run or choose.
- **Local:** consoles on the same Wi-Fi network meet directly, without a server.

It runs as an Atmosphere sysmodule with an overlay. When enabled, the module handles multiplayer in the background; switching between remote and local modes takes effect when you next start a game, without rebooting the console. It is tuned for low memory use on real hardware.

PotLuck has three parts. Install the Switch components once; run a server only if you need remote play.

| Part | Where it runs | Role |
|---|---|---|
| Installer app (`potluck-installer.nro`) | Switch Homebrew Menu | Installs, updates and removes PotLuck; configures the server and runs connection checks. It can be closed before playing |
| Sysmodule and overlay (`potluck.ovl`) | Switch / Atmosphere | The module handles multiplayer in the background; the overlay changes settings and shows status |
| Relay server and Redis | A Docker host | The server finds rooms and relays remote traffic; Redis stores server statistics. Local mode needs neither |

## Full manual

| Task | Documentation |
| --- | --- |
| Install and play | [Install](https://jshsakura.github.io/oc-potluck/guides/installation/) · [Playing](https://jshsakura.github.io/oc-potluck/guides/overlay/) |
| Options, defaults and timing | [Configuration](https://jshsakura.github.io/oc-potluck/guides/configuration/) |
| Host on a NAS / Synology | [NAS](https://jshsakura.github.io/oc-potluck/guides/synology/) · [Docker Compose](https://jshsakura.github.io/oc-potluck/guides/relay-server/) · [Run from source](https://jshsakura.github.io/oc-potluck/guides/relay-native/) · [compose.nas.yml](deploy/compose.nas.yml) |
| Update, remove and recover | [Maintenance](https://jshsakura.github.io/oc-potluck/guides/maintenance/) · [Troubleshooting](https://jshsakura.github.io/oc-potluck/guides/troubleshooting/) |

## Features

- **Two modes in one module.** Pick remote or local in the overlay; the next game you start uses it.
- **Works before any setup.** A fresh install starts in remote mode, and until you set a server address it plays as local mode.
- **Plays with ldn_mitm users.** Local mode speaks the same LAN protocol as ldn_mitm.
- **Private channels.** In remote mode, players who enter the same 8-digit channel see only each other's rooms.
- **Replaces ldn_mitm.** Only one LDN module can run, so the installer turns off ldn_mitm's autostart. If ldn_mitm still starts first, PotLuck steps aside and the overlay says so. If both are set to start, ldn_mitm itself can stop the console at boot, so keep only one enabled.
- **Self-hostable relay.** A prebuilt server image runs with one command.

## Requirements

- A Nintendo Switch you own, running [Atmosphere](https://github.com/Atmosphere-NX/Atmosphere)
- Games you own that offer local wireless play
- For the overlay: nx-ovlloader with an overlay menu such as Ultrahand
- For remote mode: a relay server address (see [Relay server](#relay-server))

Every player needs a compatible local wireless game and game version. The game must offer an LDN menu; PotLuck does not turn a single-player game into multiplayer or replace its official online service. Start with two consoles before adding more players.

## Install

### With the installer (recommended)

1. Download `potluck-installer.nro` from [Releases](https://github.com/jshsakura/oc-potluck/releases/latest) and copy it to `switch/` on the SD card.
2. Open it from the Homebrew Menu and choose install. It downloads the latest release, checks it, and installs it.
3. Restart the console.

The installer also sets the server address, tests the connection, shows diagnostics, updates and uninstalls PotLuck. If ldn_mitm is installed, it turns off only ldn_mitm's autostart (`flags/boot2.flag`) and turns it back on if you uninstall PotLuck.

Close the game before installing or updating, and back up an existing `config.ini` before manual changes. To update, use the installer's update action or replace the module and overlay together from one release, then restart the console. The installer stages and checks release files before replacing them and can recover an interrupted installation when opened again; do not remove its recovery files during that process.

### By hand

1. Download `potluck-v*.zip` from [Releases](https://github.com/jshsakura/oc-potluck/releases/latest) and extract it to the root of the SD card.
2. If ldn_mitm is installed, delete `atmosphere/contents/4200000000000010/flags/boot2.flag`. Only one LDN module can run at a time.
3. Restart the console.

## Use

1. Open the overlay with **L + D-Pad Down + Right Stick Click** and choose **potluck.**
2. Before starting the game, choose a **mode**. For remote play, save the same server and channel on every console and run the server connection test. For local play, join the same Wi-Fi network.
3. Start the game and open its **local wireless** menu. Have one player host a room and the others join it.
4. After changing between remote and local modes, fully close and reopen the game. A running game keeps the mode it started with.

The card at the top of the overlay shows whether the running game is on the supported list, the connection, the room size and the latency. The overlay follows the system language: Korean on a Korean system, English otherwise.

| Setting | Mode | What it does |
|---|---|---|
| Mode | both | Selects remote, local or off; follow the overlay's reboot notice when switching off or enabling a module that booted off |
| Channel | remote | 8 hex digits; only the same channel sees your rooms. Empty is the public channel |
| P2P | remote | Optional direct traffic between consoles after room discovery; off by default. Router/NAT support affects whether it works |
| Force reconnect | remote | Reconnects to the relay server |
| Server address, Port | both | The relay server; an empty address plays as local mode. The port shows its default, 30456 |
| Test the server | both | Connects to the saved server once and shows the latency |

An overlay cannot open the system keyboard, so the address, port and channel are typed on an on-screen keypad: move with the D-Pad and press A, or touch the keys. **+** or the Save button saves, **B** deletes.

### Remote mode

Set the relay server address in the overlay, in the installer (with the system keyboard), or in `atmosphere/contents/4200000000504C4B/config.ini`:

```ini
[server]
host = relay.example.com
port = 30456

[ldn]
mode = remote
```

With no `host`, remote mode plays as local mode. There is no built-in public server.

Use a hostname or IP address for `host`, without `http://`, `https://` or a path. Use the separate port field for the TCP port. A successful connection test confirms server reachability; it does not prove that a game's room discovery or gameplay works. Channel codes filter room discovery and do not provide encryption or strong authentication.

### Local mode

All consoles must be on the same Wi-Fi network, and the router must let them reach each other (turn off AP or client isolation). PotLuck uses UDP broadcast and TCP on port **11452**, the same as ldn_mitm.

Local mode uses the router's LAN rather than Nintendo's original nearby wireless link. A guest network or separate VLAN can prevent discovery even when both consoles show the same Wi-Fi name. No Internet relay or port forwarding is needed for this mode.

## Relay server

Run the prebuilt image with Redis; no checkout or build is needed:

```sh
curl -fLO https://github.com/jshsakura/oc-potluck/releases/latest/download/compose.server.yml
docker compose -f compose.server.yml up -d
```

Allow inbound **TCP 30456** and enter the server's address on each console. The image supports amd64 and arm64 and runs without root, with a read-only root filesystem and memory and PID limits. Traffic is plain TCP and is not encrypted, so only use servers you trust.

Run these commands in the directory containing `compose.server.yml` to inspect or update the server:

```sh
docker compose -f compose.server.yml ps
docker compose -f compose.server.yml logs --tail=100 ldn-server
docker compose -f compose.server.yml pull
docker compose -f compose.server.yml up -d
```

The compose file selects a versioned server image. `pull` refreshes that selected tag; moving to another version requires updating the image tag or setting `LDN_SERVER_IMAGE`. Keep the compose file with your deployment settings, and check release notes before changing versions.

If the host is behind a router, forward the selected TCP port to that host. With the default P2P-off setup, players only need an outbound connection to the relay. Redis has no published host port and is on an internal Docker network; keep it that way. Its data is transient statistics, while the `ldn-data` volume keeps the server's IP ban file. The container resource limits protect the host but do not guarantee game latency or resistance to abusive clients.

## Files on the SD card

| Path | Contents |
|---|---|
| `atmosphere/contents/4200000000504C4B/` | The module, `config.ini` (created on first boot), `gamelist.txt` and `game_names.tsv` |
| `config/potluck/` | Diagnostic logs, including `potluck.log` when logging is enabled |
| `switch/.overlays/potluck.ovl` | The overlay |
| `switch/potluck-installer.nro` | The installer, if you used it |

Uninstall from the installer and choose whether to keep settings, or manually remove the module directory and `switch/.overlays/potluck.ovl`, then restart. Removing the module directory also removes its settings; back up `config.ini` first if needed. Manual removal does not restore ldn_mitm's boot flag. The `config/potluck/` logs and the installer NRO can also be removed when no longer needed. Settings from an older `config/ryu_ldn_nx/config.ini` are carried over on the first boot.

`gamelist.txt` lists the games whose traffic PotLuck carries in remote mode. Being on the list does not guarantee that a game works.

## Troubleshooting

- **The overlay says another LDN module is running.** ldn_mitm started first. Remove its `flags/boot2.flag` (or reinstall with the installer) and restart.
- **Remote mode shows no rooms.** Check the server address and port in the installer's connection test, make sure everyone uses the same server and channel, and that TCP 30456 is open on the server.
- **Local mode shows no rooms.** Check that every console is on the same network and that the router does not isolate wireless clients.
- **A game does not connect.** Restart the game after changing modes, and check whether it is on the supported list.
- **A room is visible, but joining or playing fails.** Check that all players have compatible game versions. If P2P is enabled, try relay-only play with P2P off and follow the setting's restart notice. A room appearing in a scan does not verify the gameplay path.
- **Play stutters or disconnects.** Compare local and remote play with the same consoles, check the relay host's load and network, and turn diagnostic logging off for normal play. Enable it only while collecting a short reproduction.

When reporting a game issue, include the PotLuck and game versions, local/remote mode, player count, P2P setting, which console hosted, and the exact host/join/exit steps. If available, attach only the relevant portion of `config/potluck/potluck.log` after removing IP addresses, channel codes, player names and tokens. Report security problems privately through the [security policy](SECURITY.md).

## Build

```sh
git clone --recursive https://github.com/jshsakura/oc-potluck.git
cd oc-potluck
docker compose run --rm build
docker compose run --rm test
```

The module and overlay go to `output/` and `potluck-release.zip`. The installer builds from `installer/` (see [installer/BUILD.md](installer/BUILD.md)). The builder image is published for amd64 and arm64, so ARM hosts build natively. Tests run under an aggregate 4 GiB memory limit.

The relay server is part of this repository: its source is in [server/src](server/src), started from the MIT-licensed Ryujinx LDN server and maintained here, and [server/Dockerfile](server/Dockerfile) builds the image from it. [server/verify-container.sh](server/verify-container.sh) checks the container deployment and protocol integration with a shared test memory budget. Host tests and CI do not establish real-console compatibility or a latency guarantee; verify gameplay, disconnect and game-exit behavior on hardware as well.

## Credits

- PotLuck continues [ryu_ldn_nx](https://github.com/Ethiquema/ryu_ldn_nx) by Ethiquema (GPL-2.0) and keeps its protocol work, adding deadlock and lifetime fixes, TCP over LDN in remote mode and on-demand IPC threads.
- Local mode is ported from the LAN implementation of [ldn_mitm](https://github.com/spacemeowx2/ldn_mitm) by spacemeowx2 (GPL-2.0).
- Built on [Atmosphere](https://github.com/Atmosphere-NX/Atmosphere), [libnx](https://github.com/switchbrew/libnx) and [libultrahand](https://github.com/ppkantorski/libultrahand).

## Legal

PotLuck is an independent project. It is not affiliated with, endorsed by or sponsored by Nintendo. Nintendo Switch is a trademark of Nintendo. PotLuck contains no Nintendo code, keys, firmware or game data, and it does not help obtain or run unauthorized copies of games; use it only with a console and games you own.

Running custom firmware and homebrew is at your own risk and may affect your warranty or your access to official online services. Remote mode sends game traffic to the relay server you choose; that server's operator is responsible for it.

[GPL-2.0](LICENSE) · [Contributing](CONTRIBUTING.md) · [Security](SECURITY.md)
