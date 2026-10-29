# Overlay design contract

## 1. Direction
Preserve the existing libultrahand component system. The overlay title is potluck., in the overlay metadata and the main frame. This is a controller-operated Nintendo Switch overlay, not a web page. Keep connection checks prominent and group configuration beneath 설정.

## 2. Colors
Use the library's existing theme and status colors. Add no custom palette or decoration.

## 3. Typography
Use OverlayFrame, CategoryHeader and ListItem typography, including the existing Korean font support. Keep labels short; show game names first, with title IDs only as a fallback for missing SD metadata.

## 4. Layout and spacing
One OverlayFrame and one scrolling List per screen. Inherit row heights, padding and focus geometry from libultrahand. Main navigation: status/check, 랜플레이 가능, 설정. Settings contains the existing four submenus and persistence controls.

## 5. Primitives and states
Reuse ListItem navigation with a > value, CategoryHeader grouping and existing setting controls. A activates, B returns. A ListItem is only for something the user can act on or a live value. Explanations, empty states and errors are drawn notices (AddNotice), never rows. Buttons carry no "Press A" value. Text entry uses the shared drawn KeypadGui grid, which validates on + and stays open with an inline error instead of silently discarding input. The games screen queries installed application records on the Switch first, matching and displaying supported titles installed on this console (with names resolved from SD files). Show empty and unavailable states explicitly. Keep settings locked during an active game; fail closed if the active-state query fails.

## 6. Motion
Keep library navigation and focus behavior. Add no animations or timers for the catalog.

## 7. Accessibility constraints
Retain controller focus and standard back behavior. Do not convey status by color alone. Label the game catalog as routing eligibility, not a guarantee of tested compatibility. Actual CJK clipping, focus, and theme checks require a Switch capture; compilation cannot establish visual correctness.

## 8. Validation and limits
Compile the real overlay and sysmodule together. Test pagination boundaries and malformed input using production code under the aggregate 4 GiB ceiling. Native device screenshots are unavailable in this session; record visual QA as unverified, not passed. Web breakpoints, React tooling and Lighthouse do not apply to this native surface.

## Runtime and language budget
Use Korean when the system language code is ko, otherwise English. Select static literals directly; no resident translation catalog. Main view exposes the enable switch and one server check. Changes to enable interception apply on the next game launch and are saved; an active intercepted game blocks the switch. The existing enable flag gates both new LDN and BSD interception.

Server checking runs in a single on-demand overlay task with an owned stack. No GUI pointer enters the worker. All screens reap completion, and overlay exit joins the worker before services close. Free the task and stack after completion. There is no permanent checking thread, no probe timer, and no extra sysmodule buffer. Display never checked, checking, success or a specific failure in the check row only; a check must not overwrite live game status or RTT.
