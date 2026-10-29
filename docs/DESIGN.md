# PotLuck documentation and introduction

CookingFoil supplies the visual family. MFA ServiceNow MCP supplies the Astro + Starlight structure, English root and Korean locale, searchable sidebar and source links. Guides in src/content/docs remain canonical; API generation stays opt-in so ordinary builds need no C++ toolchain. App and overlay behavior stay unchanged.

## Purpose and content

GitHub Pages explains PotLuck and takes visitors from installation to playing with friends. It does not show connected players, room counts, live status, or call the relay status API. The public relay site at https://relay.example.com/ supplies the visual family; this is a documentation site, not a pixel clone of its connection board.

Primary action: open the installation guide. Information priorities: playing with friends; choosing local or remote; installing and reaching the first room. Required: LDN MITM and TCP/UDP support, installer/module/overlay/optional Docker relay roles, same-game-version prerequisite, installation/restart/mode/game steps, matching remote server and channel, NAS/Synology/Compose links, configuration and troubleshooting, Korean/English, light/dark, documentation search. Left out: live data, room lists, photos, game art, manufacturer/platform marketing, decorative statistics, tracking, new client frameworks.

## Tokens

Reuse the public site's cream/butter palette: light ground #f2f1ee, surface #fff, sunk #ebe9e4, ink #16161a, secondary #55555e, line #e2e0da; dark ground #16161a, surface #1f1f24, sunk #26262c, ink #f2f1ee, secondary #b4b3ae, line #303037. Butter #ffc23d with ink #16161a. Documentation link accents retain the Starlight accessible theme mappings in custom.css. Local Baloo 2 is for the wordmark only; body and headings use the system stack. Spacing in rem: .25, .5, .75, 1, 1.25, 1.5, 2, 2.5, 3, 4, 5.5. Body 1rem (1.0625rem on phones), secondary .875rem, labels .75rem, headings clamp(2.25rem,4vw,3.375rem), section headings clamp(1.75rem,3vw,2.5rem), item headings 1.1875rem. Radius .5rem for actions, 1rem for diagram, pills only for labels/wordmark accents. Neutral 1px dividers. No shadows or ornamental motion.

## Layout

Layout: home hero - explain the purpose beside a generic peer/relay diagram -> split-screen (https://github.com/changeroa/StyleGallery/blob/main/patterns/split-sidebar/split-screen.md); scroll owner: document; anti-patterns: no fixed-height panes, no reordered keyboard flow, no hardware illustration.

Layout: home installation - keep three sequential instructions in reading order beside the section title -> step-nav (https://github.com/changeroa/StyleGallery/blob/main/patterns/stacking/step-nav.md); scroll owner: document; anti-patterns: no internal scroller, no arbitrary cards per sentence, no hidden required steps.

Sections share a 1120px limiter; two columns reflow to one before content becomes narrow. The phone page uses 20px side insets. Articles retain Starlight's existing documentation shell and sidebar. The document owns page scroll; code blocks, sidebar navigation and search results may scroll for their own bounded content.

## Components and states

SiteTitle uses the existing router SVG and local wordmark with butter punctuation. Hero presents the same friends-first purpose as the relay site, one yellow installation link, a neutral usage link, and a live HTML/CSS diagram with generic peers and relay. Peer markers use the Lucide User Round glyph, centered at 28px inside a 56px circular neutral surface with a 1.75px stroke; the circle gives both peers the same visual footprint. Korean landing copy uses direct installation and play instructions instead of translated slogans. The diagram describes TCP/UDP game traffic, with no claim that the relay opens a public UDP service. Landing sections share a heading column and content column. Mode rows explain local/remote, numbered setup rows explain install/restart/select mode/start game, and relay setup links to NAS and Compose guides. Guide navigation is a list, not a statistics grid.

Links have normal, hover, pressed and visible keyboard-focus states; touch controls are at least 44px. Theme/language/search use Starlight's maintained components. No loading/error states are needed for the static landing content. Search retains Starlight's empty-results state. Reduced motion disables transitions through the existing site rule.

## Verification

Capture the old English/Korean home and installation pages before changes. Verify the production build, crawl internal links and local assets, ensure no room/status API calls, then inspect Korean/English at 390/1440/1920 in light/dark including page ends, installation, NAS, configuration, search, language and theme navigation. Preserve real technical paths and API identifiers in guides while removing manufacturer/platform prose. All build and browser processes share the verified 4 GiB cgroup.

The header logo uses a crisp approximately 1px white (#fff) silhouette outline only in dark mode, including automatic dark appearance. Four zero-blur one-pixel directional drop shadows form the edge; this is a legibility treatment, not decorative depth. The original SVG colors, light appearance, dimensions and logo motion remain unchanged.

Relay hosting offers both Docker/NAS and direct .NET source-build paths. The native guides document the current net10.0 target, required Redis JSON support, framework-dependent and Native AOT publishing, environment variables, paths, privileges, and service operation. The landing links to both without implying Docker is mandatory.
