# potluck-installer

<p align="center">
  <img src="icon.jpg" width="128" alt="potluck-installer icon">
</p>

PotLuck(로컬 무선 게임을 인터넷으로 잇는 시스모듈과 오버레이)을 설치, 업데이트, 삭제하는 홈브류 NRO입니다.
상점의 홈브류 목록에 일반 `.nro`로 올립니다.

## 하는 일

1. GitHub 최신 릴리스 버전을 보여 주고, SD에 설치된 버전과 나란히 표시합니다.
2. 릴리스 zip(`potluck-v*.zip`)을 받아 SD의 제자리에 풉니다. 쓰는 곳은 아래 둘뿐이고, zip 안의 다른 경로는 무시합니다.
   - `atmosphere/contents/4200000000504C4B/`
   - `switch/.overlays/potluck.ovl`
3. 모든 파일이 제자리에 들어간 뒤 마지막으로 PotLuck의 `flags/boot2.flag`를 켭니다. 모듈은 늘 켜 두고, 리모트와 로컬은 오버레이에서 고릅니다.
4. 끝나면 "재부팅하면 적용됩니다"만 안내합니다. 재부팅은 사람이 직접 합니다.
5. 삭제: 모듈 폴더와 오버레이만 지웁니다. 기본은 `config.ini`를 남기고, 두 번째 선택지로 설정까지 지웁니다.

## 서버 주소

메뉴의 "서버 주소"에서 주소(IPv4, 또는 점으로 나눈 이름: 각 부분 영문·숫자·하이픈 1~63자, 하이픈으로 시작·끝 금지, 전체 63자 이하)와 포트(1~65535)를 시스템 키보드로 입력하면 `config.ini`의 `[server]` 섹션 `host`, `port`만 바꿉니다. 다른 줄과 주석은 그대로 두고, 파일이 없으면 만듭니다. 잘못된 값은 저장하지 않고 한 줄로 알려 줍니다. 모듈이 시작할 때 읽으므로 재부팅하면 적용됩니다.

정해진 서버는 없습니다. 주소가 비어 있으면 화면에 "없음 (로컬 모드로 동작합니다)"라고 보여 주고, 모듈은 `[ldn] mode`가 `remote`여도 로컬 모드로 동작합니다. 리모트로 놀려면 쓸 중계 서버의 주소를 직접 넣어야 합니다.

## 그 밖의 메뉴

- 첫 화면의 연결 방식: `config.ini`의 `[ldn] mode`(`remote` 또는 `local`, 없으면 `remote`)를 보여 주기만 합니다. 서버 주소가 없으면 로컬로 보여 줍니다. 바꾸는 곳은 오버레이입니다.
- 서버 주소: 서버 연결 시험(TCP, 5초, 주소가 없으면 시험하지 않고 원격 모드에는 주소가 필요하다고 알립니다), 서버 주소 지우기(`host`, `port` 줄만 삭제).
- P2P와 진단: `[ldn] disable_p2p`(기본 1, 중계만), `[debug] enabled`(켜면 `config/potluck/potluck.log`에 기록), `level`.
- LAN 플레이 가능 게임: 모듈의 `gamelist.txt`에 있는 게임 중 이 기기에 설치된 것만 보여 줍니다(`game_names.tsv`로 이름 표시). 업데이트 때마다 덮어써지는 파일이라 보기만 합니다.
- `config.ini`는 바뀐 키 줄만 고치고, 없는 키는 그 섹션 끝에, 섹션이 없으면 파일 끝에 붙입니다. `use_tls`, `ping_interval`, `interface`, `[ldn] mode`는 쓰지도 지우지도 않습니다. 없어진 키인 `[ldn] enabled`는 쓰지 않습니다.
- 읽지 못한 `config.ini`(읽기 오류, 일부만 읽힘, 64 KiB 초과)는 고치지 않고 오류로 알립니다. 파일이 없을 때만 빈 파일에서 시작합니다.
- 저장은 `config.ini.part`에 쓴 뒤 원래 파일을 `config.ini.bak`으로 옮기고 새 파일을 넣습니다. 중간에 실패하면 원래 파일을 되돌리고, 앱을 다시 열 때 남은 `.bak`을 정리합니다.

## 연결 방식과 ldn_mitm

- PotLuck은 리모트(중계 서버)와 로컬(같은 공유기, ldn_mitm과 호환)을 함께 갖고 있습니다. 둘 중 하나는 오버레이에서 고르고, 재부팅이나 모듈 교체는 없습니다.
- ldn_mitm(`atmosphere/contents/4200000000000010/`, `toolbox.json` 이름이 `ryu_ldn_nx`가 아닌 것)의 `flags/boot2.flag`가 켜져 있으면 설치할 때 그 파일 하나만 지웁니다. 둘 다 `ldn:u`를 가로채면 부팅 때 멈추기 때문입니다. ldn_mitm 폴더의 다른 파일은 건드리지 않습니다.
- 이 앱이 그 플래그를 지웠다는 사실은 `config/potluck_installer/installed.txt`에 남깁니다. 삭제할 때는 이 앱이 지운 경우에만 플래그를 되돌립니다.

## 인터넷 연결 전 검사

점검 항목마다 한 줄로 보여 줍니다. 확인하지 못한 항목은 안전하지 않은 것으로 칩니다.

- Nintendo 도메인 5개가 막혀 있는지(DNS). conntest와 ctest는 90DNS가 일부러 돌려 두므로 판정에서 뺍니다.
- 시리얼 번호가 드러나는지
- 실행 중인 시스템의 `blank_prodinfo`(exosphere에 직접 묻습니다)
- 실행 중인 시스템이 sysMMC인지 emuMMC인지(exosphere에 직접 묻습니다)

exosphere에 묻지 못하면 결과는 "확인하지 못함"으로 남습니다. `exosphere.ini`와 `emummc/emummc.ini`는 다음 부팅에 쓰일 설정이라 판정에 쓰지 않고 따로 보여 주기만 합니다.

## 설치가 실패할 때

- 받은 zip은 20 MiB까지만 받습니다. 넘으면 받는 도중에 멈춥니다.
- zip은 먼저 `config/potluck_installer/stage/`에 풀고 확인합니다. 항목이 256개를 넘거나, 파일 하나가 16 MiB, 모두 합쳐 64 MiB를 넘거나, CRC가 맞지 않거나, 대소문자만 다른 같은 이름이 있으면 아무것도 바꾸지 않고 멈춥니다.
- zip 안의 `flags/` 아래 파일(대소문자 무관), `.bak`과 `.part`로 끝나는 파일, 모듈 폴더에서 세 단계 넘게 들어간 파일은 풀지 않습니다.
- 확인이 끝나면 PotLuck의 플래그를 잠시 끄고, 바꿀 파일 목록을 `config/potluck_installer/commit.txt`에 적은 뒤 원래 파일을 `.bak`으로 옮기며 새 파일을 넣습니다. 하나라도 실패하면 모든 파일과 플래그를 되돌립니다. 전원이 꺼져 중간에 멈췄으면 앱을 다시 열 때 그 목록으로 되돌립니다.
- 파일이 모두 들어간 뒤에 옛 빌드를 옮기고, ldn_mitm의 플래그를 끄고, PotLuck의 플래그를 마지막에 켭니다. 새 파일을 넣다가 실패하면 옛 빌드는 원래 자리에서 그대로 켜져 있습니다.
- 삭제는 PotLuck의 플래그를 먼저 끄고, 장부(`installed.txt`)를 지운 뒤 `toolbox.json`을 마지막에 지웁니다. 지우지 못한 파일이 있으면 오류로 알리고, 다시 실행하면 이어서 지웁니다. 빈 폴더만 남았을 때도 삭제를 다시 고를 수 있습니다.

## 이전 버전에서 옮기기

옛 빌드(`ryu_ldn_nx`)는 ldn_mitm과 같은 타이틀 ID를 썼습니다. 설치할 때 그 폴더를 새 ID 폴더로 옮깁니다.

- 새 파일이 모두 들어간 뒤에 옮기며, 옛 플래그를 가장 먼저 지우고 `toolbox.json`을 가장 나중에 지웁니다.
- 파일마다 따집니다. 옛 빌드의 실행 파일(`exefs.nsp`, `main.npdm`, `toolbox.json`, 플래그, 릴리스가 새로 넣는 파일)과 새 폴더의 것과 똑같은 파일만 지우고, 새 폴더에 없는 파일(`config.ini` 포함)은 옮깁니다. 내용이 다른 파일이 하나라도 있으면 아무것도 지우거나 옮기지 않고 멈춥니다.
- 옛 오버레이는 PotLuck이 설치된 뒤에 지웁니다.
- `config/ryu_ldn_nx/`는 모듈이 읽는 옛 설정 키가 모두 같은 값으로 새 `config.ini`에 들어간 뒤에만 지웁니다. 새 `config.ini`가 없을 때 이 앱에서 설정을 저장하면 옛 설정에서 시작하므로 비밀번호 같은 값이 사라지지 않습니다.

## 화면

CookingFoil 클라이언트의 디자인을 따릅니다(1280x720 좌표, 밝은/어두운 팔레트는 콘솔 테마를 따름, 둥근 카드, 버터색 포커스 링, 하단 60px 푸터의 키 배지).
폰트는 CookingFoil Sans(Pretendard 서브셋, OFL 1.1, 라이선스는 `romfs/fonts/OFL.txt`)입니다.
위쪽 로고는 CookingFoil 워드마크(Baloo 2 ExtraBold, 소문자, 끝의 점만 버터색)로 `potluck.`을 씁니다. Baloo 2는 OFL이며 `romfs/fonts/OFL-Baloo2.txt`에 있습니다. NRO 아이콘은 버터색 공유기와 같은 워드마크입니다.
메뉴 항목의 선 아이콘은 CookingFoil 클라이언트와 같은 세트이며 대부분 [Lucide](https://lucide.dev)(ISC 라이선스, Copyright (c) Lucide Contributors; Feather에서 온 부분은 MIT, Copyright (c) Cole Bemis)에서 다시 그렸습니다. 라이선스 전문은 `romfs/LICENSE-Lucide.txt`에 있습니다.
"부팅이 안 될 때" 메뉴에 무엇을 지우면 되는지 한글로 적어 두었습니다.

## 보안

- 내려받는 주소는 `https://github.com/jshsakura/oc-potluck/releases/download/` 아래만 받습니다.
- 삭제는 `toolbox.json`의 이름이 `potluck`일 때만 합니다.
- zip에서 쓰는 경로는 위 두 곳뿐이고, 풀린 크기도 위의 한도를 넘지 못합니다.

## 상태

- 호스트 테스트 통과(경로 규칙, 설치/이전/삭제, 실패 주입, DNS와 설정 판정, 폰트 글자 범위).
- 실기 확인 전: 네트워크(TLS와 CA, 내려받기 한도), 화면과 입력, DNS 점검, `spl` 설정 항목 번호(65005, 65007), 실제 SD 카드에서의 이름 바꾸기 실패.
- `potluck-v*.zip` 릴리스가 아직 없습니다.
- 빌드법은 `BUILD.md`에 있습니다.
