# PotLuck public website

## Purpose

Explain the LDN MITM module and show people currently playing through this relay. Preserve the cream/butter CookingFoil family. No game covers, photographs, manufacturer/platform references or hardware illustrations. Existing real game titles remain useful room metadata.

## Tokens

Shared colors come from style.css: ground #f2f1ee, surface #fff, ink #16161a, butter #ffc23d, secondary #55555e, line #e2e0da, sunk #ebe9e4; dark values use the existing theme. Public-board additions use those variables only. Spacing: 4, 8, 12, 16, 20, 24, 32, 40, 48px. Type: 11/12px metadata, 13/14px secondary, 15/16px body, 24/28px game names, 36px occupancy, responsive existing page heading. Neutral 1px rules, 12/16px corners, no decorative shadows or colored side borders. Controls: minimum 44px. Static layout, no ornamental animation.

## Layout

Layout: connection board - group players by game with room capacity beside them -> feed (https://github.com/changeroa/StyleGallery/blob/main/patterns/stacking/feed.md) + badge-list (https://github.com/changeroa/StyleGallery/blob/main/patterns/in-line-grouping/badge-list.md); scroll owner: document; anti-patterns: no internal scrolling, fixed-height rows or game-name truncation.

At wide widths the board stays within 1120px and each game has a strong heading above its rooms. Each room places players first and capacity beside them. On phones metadata wraps and the capacity stays aligned with its room. Long names wrap inside their own track. The About view stays the default. Its illustration becomes a code-native network diagram with generic peers and relay, not hardware artwork. Body type uses the system fallback stack; the licensed wordmark font is local, so rendering needs no font CDN.

## Board brief

Primary action: refresh the current list. Priorities: game name, player names, occupancy/joinability. Required: game/title ID, room distinction, public/private status, participant count/capacity, available names, age, last update, totals, loading/empty/error states, refresh, Korean/English and light/dark. Title IDs sit in an accessible disclosure. Private-room names never render even if returned by an API. Left out: cover art, images, decorative badges, fake avatars and extra statistics tiles.

Refresh is a 44px icon button immediately beside the connection heading, with a localized accessible name and tooltip. A manual click rotates the icon for at least one 650ms turn and until the request finishes, without delaying the list update; duplicate requests are blocked. Reduced motion uses the disabled/busy state without rotation. Channel presence uses only `No channel` or `Channel ***` from the existing public/private boolean; actual channel values never enter the view. About leads with playing local multiplayer with friends; LDN MITM and TCP/UDP game traffic are supporting details. The remote-mode diagram labels peer traffic TCP/UDP and the relay's job as room discovery and relay, rather than identifying all traffic with a single transport.

## Data and performance

Use the server's existing title-ID name lookup. A generated same-origin text catalog provides an ID lookup fallback only when a server returns an unknown name; download it at most once, never every refresh. Korean aliases are explicit. No third-party metadata/image calls. Poll only in the visible connection view, one request at a time. Keep unchanged room DOM, open disclosures and focus through refreshes. API input is parsed once before rendering with textContent.

## Verification

Capture before/after at 390, 1440 and 1920 in both themes. Exercise populated, multiple-game, private, full, empty, loading, failure and malformed data; keyboard, language/theme, refresh and long names. Verify no image requests originate from the board and no body overflow. Real games and relay traffic remain outside this UI change.

Regenerate the fallback text catalog with `node scripts/generate-web-game-titles.mjs`. Its source is `server/src/Utils/gamelist.json`; the generated file contains names and IDs only. The relay already resolves known titles, so this fallback is not downloaded for normal named rooms.

The header logo uses a crisp approximately 1px white (#fff) silhouette outline only in dark mode, including automatic dark appearance. Four zero-blur one-pixel directional drop shadows form the edge; this is a legibility treatment, not decorative depth. The original SVG colors, light appearance, dimensions and logo motion remain unchanged.
