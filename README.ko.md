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

[English](README.md) · [한국어](README.ko.md) · [다운로드](https://github.com/jshsakura/oc-potluck/releases/latest) · [소개·설치 안내](https://jshsakura.github.io/oc-potluck/ko/)

PotLuck은 로컬 통신(LDN)을 지원하는 Nintendo Switch 게임이 두 가지 방식으로 서로를 찾게 해 줍니다.

- **원격:** 떨어진 곳에 있는 기기끼리 직접 운영하거나 고른 중계 서버를 거쳐 만납니다.
- **로컬:** 같은 와이파이에 있는 기기끼리 서버 없이 바로 만납니다.

Atmosphere 시스모듈과 오버레이로 동작합니다. 활성화된 모듈이 백그라운드에서 멀티플레이를 처리하며, 원격·로컬 모드 변경은 기기 재부팅 없이 다음에 시작하는 게임부터 적용됩니다. 실기에서 메모리를 적게 쓰도록 맞췄습니다.

PotLuck은 세 부분으로 구성됩니다. Switch 쪽 구성 요소를 설치하고, 원격 플레이가 필요할 때 중계 서버를 운영하거나 선택합니다.

| 구성 요소 | 실행 위치 | 역할 |
|---|---|---|
| 설치 앱 (`potluck-installer.nro`) | Switch 홈브루 메뉴 | 설치·업데이트·삭제, 서버 설정과 연결 점검을 합니다. 게임을 시작하기 전에 종료해도 됩니다 |
| 시스모듈과 오버레이 (`potluck.ovl`) | Switch / Atmosphere | 모듈이 백그라운드에서 멀티플레이 통신을 처리하고, 오버레이가 설정과 상태를 보여 줍니다 |
| 중계 서버와 Redis | Docker 호스트 | 서버가 원격 방 검색과 통신 중계를 맡고, Redis는 서버 통계를 저장합니다. 로컬 모드에는 둘 다 필요 없습니다 |

## 자세한 사용 설명서

| 필요한 안내 | 문서 |
| --- | --- |
| 설치·첫 플레이 | [설치](https://jshsakura.github.io/oc-potluck/ko/guides/installation/) · [사용법](https://jshsakura.github.io/oc-potluck/ko/guides/overlay/) |
| 옵션·기본값·적용 시점 | [설정](https://jshsakura.github.io/oc-potluck/ko/guides/configuration/) |
| NAS·시놀로지에서 운영 | [NAS](https://jshsakura.github.io/oc-potluck/ko/guides/synology/) · [Docker Compose](https://jshsakura.github.io/oc-potluck/ko/guides/relay-server/) · [소스로 직접 실행](https://jshsakura.github.io/oc-potluck/ko/guides/relay-native/) · [compose.nas.yml](deploy/compose.nas.yml) |
| 업데이트·삭제·복구 | [유지 관리](https://jshsakura.github.io/oc-potluck/ko/guides/maintenance/) · [문제 해결](https://jshsakura.github.io/oc-potluck/ko/guides/troubleshooting/) |

## 기능

- **모듈 하나로 두 모드:** 오버레이에서 원격과 로컬을 고르면 다음에 시작하는 게임부터 적용됩니다.
- **설정 없이 바로 동작:** 처음 설치하면 원격 모드로 시작하고, 서버 주소를 정하기 전까지는 로컬 모드로 동작합니다.
- **ldn_mitm 사용자와 함께:** 로컬 모드는 ldn_mitm과 같은 LAN 통신 방식을 씁니다.
- **비공개 채널:** 원격 모드에서 같은 8자리 채널을 넣은 사람끼리만 서로의 방이 보입니다.
- **ldn_mitm 대체:** LDN 모듈은 하나만 동작할 수 있어서 설치 도우미가 ldn_mitm의 자동 실행을 끕니다. 그래도 ldn_mitm이 먼저 켜지면 PotLuck이 물러나고 오버레이가 그 사실을 알려 줍니다. 둘 다 자동 실행으로 두면 ldn_mitm 쪽이 부팅 중에 기기를 멈출 수 있으니 하나만 켜 두어야 합니다.
- **직접 운영하는 중계 서버:** 미리 빌드한 서버 이미지를 명령 한 줄로 띄웁니다.

## 준비물

- [Atmosphere](https://github.com/Atmosphere-NX/Atmosphere)가 설치된 본인 소유의 Nintendo Switch
- 로컬 통신 플레이를 지원하는 본인 소유의 게임
- 오버레이를 쓰려면 nx-ovlloader와 Ultrahand 같은 오버레이 메뉴
- 원격 모드를 쓰려면 직접 띄운 중계 서버. PotLuck은 공용 서버를 제공하지 않으므로 나나 친구 중 한 명이 서버를 열어야 합니다([중계 서버](#중계-서버) 참고). 로컬 모드에는 필요 없습니다

참가자 모두 로컬 통신을 지원하는 게임과 서로 호환되는 게임 버전이 필요합니다. PotLuck은 게임의 LDN 메뉴를 이용하며, 싱글플레이 게임에 멀티플레이를 추가하거나 공식 온라인 서비스를 대신하지 않습니다. 처음에는 기기 두 대로 연결을 확인한 뒤 인원을 늘리세요.

## 설치

### 설치 도우미로 설치 (권장)

1. [릴리스](https://github.com/jshsakura/oc-potluck/releases/latest)에서 `potluck-installer.nro`를 받아 SD 카드의 `switch/` 폴더에 넣습니다.
2. 홈브루 메뉴에서 열고 설치를 고릅니다. 최신 릴리스를 내려받아 확인한 뒤 설치합니다.
3. 기기를 다시 시작합니다.

설치 도우미에서 서버 주소 설정, 연결 시험, 진단, 업데이트, 삭제도 할 수 있습니다. ldn_mitm을 쓰고 있었다면 신경 쓸 것이 없습니다. 설치 도우미가 ldn_mitm의 자동 실행 파일(`flags/boot2.flag`) 하나만 알아서 끄고, PotLuck을 삭제할 때 다시 켭니다.

설치·업데이트 전에는 게임을 종료하고, 설정을 직접 수정하기 전에는 기존 `config.ini`를 백업하세요. 업데이트는 설치 도우미에서 하거나, 같은 릴리스의 모듈과 오버레이를 함께 덮어쓴 뒤 기기를 다시 시작합니다. 설치 도우미는 릴리스 파일을 임시로 풀어 확인한 뒤 교체하며, 설치가 중단되면 다시 열었을 때 복구할 수 있습니다. 복구 중에는 관련 임시 파일을 직접 지우지 마세요.

### 직접 설치

1. [릴리스](https://github.com/jshsakura/oc-potluck/releases/latest)에서 `potluck-v*.zip`을 받아 SD 카드 루트에 풉니다.
2. ldn_mitm이 설치되어 있으면 `atmosphere/contents/4200000000000010/flags/boot2.flag`를 지웁니다. 설치 도우미를 쓰면 도우미가 대신 하는 단계입니다. LDN 모듈은 한 번에 하나만 동작할 수 있습니다.
3. 기기를 다시 시작합니다.

## 사용법

1. **L + 십자키 아래 + 오른쪽 스틱 클릭**으로 오버레이를 열고 **potluck.** 을 고릅니다.
2. 게임을 켜기 전에 **모드**를 고릅니다. 원격 플레이는 모든 기기에 같은 서버와 채널을 저장하고 서버 연결 시험을 합니다. 로컬 플레이는 같은 와이파이에 연결합니다.
3. 게임을 시작하고 **로컬 통신** 메뉴를 엽니다. 한 명이 방을 만들고 나머지 참가자가 들어갑니다.
4. 원격·로컬 모드를 바꾼 뒤에는 게임을 완전히 종료하고 다시 시작합니다. 이미 켜진 게임은 시작할 때의 모드를 유지합니다.

오버레이 맨 위 카드에는 지금 게임이 지원 목록에 있는지, 연결 상태, 방 인원, 지연 시간이 나옵니다. 시스템 언어가 한국어이면 한국어로, 아니면 영어로 표시합니다.

| 설정 | 모드 | 하는 일 |
|---|---|---|
| 모드 | 공통 | 원격·로컬·끔을 고릅니다. 끄거나, 꺼진 채 부팅한 모듈을 다시 켤 때는 오버레이의 재부팅 안내를 따릅니다 |
| 채널 | 원격 | 16진수 8자리이며 같은 채널끼리만 방이 보입니다. 비우면 공개 채널입니다 |
| P2P 직접 연결 | 원격 | 방 검색 뒤 기기끼리 직접 통신하는 선택 기능이며 기본값은 꺼짐입니다. 공유기·NAT 환경에 따라 동작 여부가 달라집니다 |
| 강제 재연결 | 원격 | 중계 서버에 다시 연결합니다 |
| 서버 주소, 포트 | 공통 | 중계 서버입니다. 주소를 비우면 로컬 모드로 동작하고, 포트는 기본값 30456을 그대로 보여 줍니다 |
| 서버 연결 시험 | 공통 | 저장한 서버에 한 번 연결해 지연 시간을 보여 줍니다 |

오버레이는 시스템 키보드를 띄울 수 없어서 주소, 포트, 채널은 화면 키패드로 입력합니다. 십자키로 옮기고 A를 누르거나 키를 직접 터치합니다. **+** 나 저장 버튼으로 저장하고 **B** 로 지웁니다.

### 원격 모드

오버레이, 설치 도우미(시스템 키보드), `atmosphere/contents/4200000000504C4B/config.ini` 중 편한 곳에서 중계 서버 주소를 정합니다.

```ini
[server]
host = relay.example.com
port = 30456

[ldn]
mode = remote
```

`host`가 비어 있으면 원격 모드도 로컬 모드로 동작합니다. 기본으로 들어 있는 공개 서버는 없습니다.

`host`에는 호스트 이름이나 IP 주소만 넣고 `http://`, `https://`, 경로는 붙이지 않습니다. TCP 포트는 별도의 포트 항목에 넣습니다. 연결 시험 성공은 서버에 접속할 수 있다는 뜻이며, 게임의 방 검색이나 실제 플레이 성공까지 보장하지는 않습니다. 채널 코드는 방 검색을 구분하는 값이며 암호화나 강한 인증을 제공하지 않습니다.

### 로컬 모드

모든 기기가 같은 와이파이에 있어야 하고, 공유기가 기기끼리 통신하도록 허용해야 합니다(AP 격리, 클라이언트 격리 끄기). PotLuck은 ldn_mitm과 같이 **11452** 포트의 UDP 브로드캐스트와 TCP를 씁니다.

로컬 모드는 Nintendo의 원래 근거리 무선 연결 대신 공유기의 LAN을 이용합니다. 같은 와이파이 이름이 보여도 게스트 네트워크나 별도 VLAN으로 분리되어 있으면 방 검색이 막힐 수 있습니다. 인터넷 중계 서버나 포트 포워딩은 필요하지 않습니다.

## 중계 서버

미리 빌드한 이미지를 Redis와 함께 띄웁니다. 소스를 받거나 빌드할 필요가 없습니다.

```sh
curl -fLO https://github.com/jshsakura/oc-potluck/releases/latest/download/compose.server.yml
docker compose -f compose.server.yml up -d
```

**TCP 30456** 인바운드를 열고 각 기기에 서버 주소를 넣습니다. 이미지는 amd64와 arm64를 지원하고, root 없이 읽기 전용 루트 파일 시스템과 메모리, PID 제한 아래에서 동작합니다. 통신은 암호화되지 않은 TCP이므로 믿을 수 있는 서버만 쓰는 것이 좋습니다.

`compose.server.yml`이 있는 디렉터리에서 다음 명령으로 상태·최근 로그를 확인하고 이미지를 갱신할 수 있습니다.

```sh
docker compose -f compose.server.yml ps
docker compose -f compose.server.yml logs --tail=100 ldn-server
docker compose -f compose.server.yml pull
docker compose -f compose.server.yml up -d
```

Compose 파일은 버전 태그가 지정된 서버 이미지를 사용합니다. `pull`은 지정한 태그를 갱신하며, 다른 버전으로 옮기려면 이미지 태그를 바꾸거나 `LDN_SERVER_IMAGE`를 설정해야 합니다. 운영 설정이 담긴 Compose 파일을 보관하고 버전을 바꾸기 전에 릴리스 안내를 확인하세요.

서버가 공유기 뒤에 있다면 선택한 TCP 포트를 서버 호스트로 포워딩합니다. 기본값인 P2P 꺼짐 상태에서는 참가자 기기가 중계 서버로 접속할 수 있으면 됩니다. Redis는 호스트 포트를 공개하지 않고 내부 Docker 네트워크에만 연결되므로 이 구성을 유지하세요. Redis에는 임시 통계가 들어가고 `ldn-data` 볼륨에는 서버의 IP 차단 목록이 남습니다. 컨테이너 자원 제한은 호스트를 보호하지만 게임 지연 시간이나 비정상 클라이언트에 대한 내성을 보장하지는 않습니다.

## SD 카드의 파일

| 경로 | 내용 |
|---|---|
| `atmosphere/contents/4200000000504C4B/` | 모듈, 첫 부팅에 만들어지는 `config.ini`, `gamelist.txt`, `game_names.tsv` |
| `config/potluck/` | 진단 로그. 로깅을 켜면 `potluck.log`가 만들어집니다 |
| `switch/.overlays/potluck.ovl` | 오버레이 |
| `switch/potluck-installer.nro` | 설치 도우미를 썼다면 설치 도우미 |

설치 도우미에서 설정을 남길지 선택해 삭제하거나, 모듈 디렉터리와 `switch/.overlays/potluck.ovl`을 직접 지운 뒤 재부팅합니다. 모듈 디렉터리를 지우면 설정도 사라지므로 필요하면 `config.ini`를 먼저 백업하세요. 직접 삭제하면 ldn_mitm의 부팅 플래그가 자동으로 복원되지는 않습니다. 더 이상 필요 없는 `config/potluck/`의 로그와 설치 앱 NRO도 지울 수 있습니다. 예전 `config/ryu_ldn_nx/config.ini`의 설정은 첫 부팅에 옮겨집니다.

`gamelist.txt`는 원격 모드에서 PotLuck이 통신을 중계할 게임 목록입니다. 목록에 있다고 동작이 보장되지는 않습니다.

## 문제 해결

- **오버레이에 다른 LDN 모듈이 켜져 있다고 나옵니다.** ldn_mitm이 먼저 켜진 것입니다. ldn_mitm의 `flags/boot2.flag`를 지우거나 설치 도우미로 다시 설치한 뒤 재시작합니다.
- **원격 모드에서 방이 보이지 않습니다.** 설치 도우미의 연결 시험으로 서버 주소와 포트를 확인하고, 모두 같은 서버와 같은 채널을 쓰는지, 서버의 TCP 30456이 열려 있는지 확인합니다.
- **로컬 모드에서 방이 보이지 않습니다.** 모든 기기가 같은 네트워크에 있는지, 공유기가 무선 기기끼리의 통신을 막고 있지 않은지 확인합니다.
- **게임이 연결되지 않습니다.** 모드를 바꾼 뒤에는 게임을 다시 시작하고, 지원 목록에 있는 게임인지 확인합니다.
- **방은 보이는데 참가하거나 플레이할 수 없습니다.** 참가자들의 게임 버전이 호환되는지 확인합니다. P2P가 켜져 있다면 끄고 설정의 재시작 안내에 따라 중계 전용으로 시험합니다. 방이 검색되는 것만으로 실제 게임 통신까지 확인되지는 않습니다.
- **끊기거나 지연이 심합니다.** 같은 기기로 로컬·원격 모드를 비교하고, 중계 호스트의 부하와 네트워크를 확인합니다. 평소에는 진단 로그를 끄고, 짧게 문제를 재현할 때만 켜세요.

게임 문제를 제보할 때는 PotLuck·게임 버전, 로컬·원격 모드, 인원, P2P 설정, 방을 만든 기기, 방 생성·참가·종료 순서를 함께 적어 주세요. 로그가 있다면 `config/potluck/potluck.log`에서 관련 구간만 첨부하고 IP 주소·채널 코드·플레이어 이름·토큰은 지우세요. 보안 문제는 [보안 정책](SECURITY.md)에 따라 비공개로 제보합니다.

## 빌드

```sh
git clone --recursive https://github.com/jshsakura/oc-potluck.git
cd oc-potluck
docker compose run --rm build
docker compose run --rm test
```

모듈과 오버레이는 `output/`과 `potluck-release.zip`으로 나옵니다. 설치 도우미는 `installer/`에서 빌드합니다([installer/BUILD.md](installer/BUILD.md) 참고). 빌더 이미지는 amd64와 arm64용으로 배포되어 ARM 호스트에서도 그대로 빌드됩니다. 테스트는 전체 4 GiB 메모리 제한 아래에서 돕니다.

중계 서버는 이 저장소의 일부입니다. 소스는 [server/src](server/src)에 있으며 MIT 라이선스인 Ryujinx LDN 서버에서 출발해 여기서 직접 관리합니다. 이미지는 [server/Dockerfile](server/Dockerfile)이 이 소스로 빌드합니다. [server/verify-container.sh](server/verify-container.sh)는 테스트 전체가 공유하는 메모리 제한 아래에서 컨테이너 배포와 프로토콜 통합을 확인합니다. 호스트 테스트와 CI만으로 실기 호환성이나 지연 시간을 보장할 수 없으므로 실제 기기에서 플레이·연결 해제·게임 종료도 확인해야 합니다.

## 만든 사람들

- PotLuck은 Ethiquema의 [ryu_ldn_nx](https://github.com/Ethiquema/ryu_ldn_nx)(GPL-2.0)를 이어 개발합니다. 원본의 프로토콜 구현을 이어받아 데드락과 메모리 수명 문제를 고쳤고, 원격 모드의 LDN 위 TCP와 필요할 때만 늘어나는 IPC 스레드를 더했습니다.
- 로컬 모드는 spacemeowx2의 [ldn_mitm](https://github.com/spacemeowx2/ldn_mitm)(GPL-2.0) LAN 구현을 옮겨 왔습니다.
- [Atmosphere](https://github.com/Atmosphere-NX/Atmosphere), [libnx](https://github.com/switchbrew/libnx), [libultrahand](https://github.com/ppkantorski/libultrahand)를 바탕으로 합니다.

## 법적 고지

PotLuck은 독립 프로젝트이며 Nintendo와 관련이 없고 Nintendo의 승인이나 후원을 받지 않았습니다. Nintendo Switch는 Nintendo의 상표입니다. PotLuck에는 Nintendo의 코드, 키, 펌웨어, 게임 데이터가 들어 있지 않으며, 게임의 무단 복제본을 구하거나 실행하는 데 쓰이지 않습니다. 본인 소유의 기기와 게임에서만 사용하십시오.

커스텀 펌웨어와 홈브루 사용에 따른 책임은 사용자에게 있으며, 보증이나 공식 온라인 서비스 이용에 영향을 줄 수 있습니다. 원격 모드는 사용자가 고른 중계 서버로 게임 통신을 보내며, 그 서버의 운영 책임은 운영자에게 있습니다.

[GPL-2.0](LICENSE) · [기여 안내](CONTRIBUTING.md) · [보안](SECURITY.md)
