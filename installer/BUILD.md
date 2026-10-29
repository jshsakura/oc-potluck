# 빌드

`devkitpro/devkita64` 도커에 `installer/` 폴더를 마운트해서 빌드합니다. 산출물 `potluck-installer.nro`도 이 폴더에 생깁니다.

```sh
cd installer
./fetch-cacert.sh   # romfs/cacert.pem (저장소는 *.pem을 커밋하지 않습니다)
docker run --rm -u "$(id -u):$(id -g)" -v "$PWD":/work -w /work devkitpro/devkita64 make
docker run --rm -v "$PWD":/work -w /work devkitpro/devkita64 make clean   # 지울 때(옛 파일이 root 소유면 -u 없이)
```

필요한 라이브러리(SDL2, SDL2_ttf, curl, mbedtls, minizip, json-c)는 이미지에 들어 있습니다.
`romfs/cacert.pem`은 curl 프로젝트의 CA 묶음이며 `fetch-cacert.sh`가 내려받습니다. 없으면 `make`가 안내와 함께 멈춥니다.

## 테스트

호스트에서 규칙(경로, 장부, 옛 설정 옮기기, DNS와 설정 판정)과 설치/이전/삭제(가짜 `sdmc:` 폴더)를 시험합니다.
`test_failures.cpp`는 `rename`, `unlink`, `fopen`을 `-Wl,--wrap`으로 감싸 SD 쓰기 실패를 일부러 일으키고, 그래도 원래 파일과 설정이 남는지 확인합니다.
시험용 릴리스 zip은 스크립트가 만들므로 네트워크가 필요 없습니다. `fontTools`가 있으면 폰트 글자 범위도 확인합니다.

```sh
tests/run.sh
```

네트워크(`net.cpp`), 점검(`safety.cpp`), 화면(`main.cpp`)은 호스트에서 시험하지 못하며 실기에서 확인합니다.
