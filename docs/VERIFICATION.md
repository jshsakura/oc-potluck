# Documentation verification

Verified locally on 2026-10-28 under an aggregate 4 GiB memory limit.

## Delivered

- Astro + Starlight with the CookingFoil paper/butter palette, licensed local Baloo 2 font, original PotLuck icon, light/dark themes and mobile language/theme controls.
- English and Korean home pages and 11 matching guide pairs: introduction, installation, play/overlay, configuration, maintenance, troubleshooting, relay hosting, Synology/NAS, building, architecture and contributing. Existing developer API/IPC/protocol references remain available; untranslated reference pages use Starlight's fallback notice.
- Complete standalone `deploy/compose.nas.yml`, also served at `/oc-potluck/downloads/compose.nas.yml` from the same source. Includes relay/Redis, private Redis networking, non-root identities, resource caps, persistent ban-list volume and rotated logs.
- README navigation, repository-readable guide sources, and a workflow that validates PRs and deploys main documentation changes through GitHub Actions.
- Standard builds need no Doxygen output. Optional API generation fails clearly when XML is absent and preserves the hand-written API index.

## Evidence

Build, browser automation and deployment checks ran inside `scripts/ci/run-limited.sh`. It verified the shared parent cgroup `memory.max=4294967296` and `memory.swap.max=0`, covering descendant processes. No relay containers or NAS services were started for this documentation change.

| Check | Result |
| --- | --- |
| Production build / Pagefind | 31 HTML pages, bilingual search index and sitemap generated |
| Internal links, anchors and assets | 1,746 checks, zero broken targets; 404 canonical/alternate URLs use the host's 404 handler |
| Responsive capture matrix | All 31 pages at 390 CSS px / DPR 3, 1440 / DPR 1 and 1920 / DPR 1, light and dark, top and end: 372 PNGs |
| Render checks | No body overflow, missing images, raw Markdown tables or theme mismatch |
| Compose | Valid YAML/interpolation; changing host port to 30457 retains container port 30456; Redis has no host-published port |
| Download consistency | Built Compose download matches the repository file byte for byte |
| API missing-XML behavior | Expected nonzero result, clear error, API index hash unchanged |
| Workflow | YAML parsed; PR build and main-only deployment conditions checked locally |
| Source hygiene | `git diff --check` and frontend draft guard clean |

Browser interactions covered search results/dismissal, same-page language switching, theme selection, mobile navigation and keyboard focus. Representative home, configuration and NAS captures were visually inspected. Capture metadata was checked against actual PNG dimensions. The first capture attempt was invalidated because managed navigation reset emulation; final captures reapply it after navigation and assert the actual viewport/DPR.

Local evidence (`build.log`, `links.json`, `screens.json`, `screenshots/`, `interactions.json`, and `deployment.json`) stayed on the verification host and is not committed. Temporary profiles are removed and task-owned preview processes are stopped after verification.

## Publication and practical limits

The Pages workflow is prepared, not dispatched by this change. The repository must use **GitHub Actions** as its Pages source, and the documentation changes must be integrated into `main` before the intended URL `https://jshsakura.github.io/oc-potluck/` is published. A local build is not evidence of a successful live deployment.

No Synology hardware, WAN route, real-console gameplay or full Doxygen-generated API build was exercised here. The NAS instructions distinguish LAN/public addresses, TCP forwarding, CGNAT, volume ACLs and plain-TCP transport; verify those on the actual deployment. Switch app, module and relay runtime code were not changed.
