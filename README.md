# WD rewrite

C++17, x64. Open `wd.vcxproj` in Visual Studio 2022, or run `./build.ps1 -Test`. The workspace project outputs `build\wd.dll`; the Desktop project's configured output remains `C:\xampp\htdocs\downloads\dll.dll`. Uses the shared MSVC runtime, `/O1`, link-time optimization and dead-code removal.

## Loop and overlay

Start Echo before loading the DLL. The existing startup thread finds `FindWindowA("GLFW30", "Echo Overlay")` once and owns menu input and rendering. A second thread runs entity scans and game features. Both loops are uncapped. There are no sleeps, frame-rate gates, focus checks, game-window searches, input-message filtering, reconnect/resize handling or periodic profiling. The overlay's HWND, client dimensions, position and presentation backbuffer are cached at initialization and assumed stable until shutdown. Drawing continues regardless of focus. Press Escape in the game to release its mouse, then use the menu.

The original Release project defines `OVERLAY` and runs updates outside the game thread. This rewrite also leaves the game's message loop untouched. No PeekMessageW hook or MinHook source is built. Game-function calls and entity caches stay on the update thread. All graphics resources and menu settings stay on the render thread. Uncapped loops can continuously occupy CPU cores; no FPS improvement is claimed without measurement in the game.

`frame_exchange.h` is the entire handoff: one SRW lock used as a mutex, one completed snapshot, and copied input settings. There are three owned buffers: the updater's build buffer, the published buffer and the renderer's current buffer. The updater builds outside the lock, then swaps the finished snapshot into the published buffer. The renderer tries the lock and swaps in a new snapshot if available; otherwise it keeps drawing its existing one. Vector storage changes owners without copying entity arrays. Neither scanning nor drawing holds the lock. Intermediate clears and partially built lists are never published. A completed invalid-world update is published to clear the old scene.

The same lock carries menu settings and visibility to the updater. Each update uses its own settings copy; it never reads the menu's mutable globals. Stop sets the shared stop flag, waits for the update thread to exit, closes its handle, then clears and releases rendering resources. An in-progress update completes before shutdown; it cannot publish again after Stop.

The updater reads the local world, level, game instance, controller, pawn, camera and PlayerState links before resolving or calling engine functions. Gameplay requires a live pawn with a mesh and a matching PlayerState ownership link. The menu keeps rendering while that context is absent. Losing or changing the context resets actor, bone, projectile, targeting, mortar and native-function caches. The scanner rechecks the context every 16 actors and before targeting/publication. Native ProcessEvent calls reject object headers marked as loading, destroyed or garbage; the flag values follow [Epic's EObjectFlags definitions](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/CoreUObject/EObjectFlags). These prechecks do not synchronize the worker with engine garbage collection.

One high-resolution clock sample per update supplies elapsed time for the existing aim interpolation, cooldown, cache and velocity calculations. `QueryPerformanceFrequency` runs once on the update thread; `QueryPerformanceCounter` runs once per update for feature timing, with no frame cap or profiling. Rendering does not sample that clock. The former per-feature `GetTickCount64` calls share the update timestamp. Stop alone uses `GetTickCount64` for a one-second deadline to drain a transparent frame before releasing graphics resources.

The DX9/DX11/Direct2D renderer keeps two reusable shared surfaces. It checks completion when beginning a frame and skips drawing when both surfaces are busy. These are GPU ownership checks, not window checks: they prevent overwriting a surface while another device still reads it. They never spin or sleep inside begin/end. Presentation uses `D3DPRESENT_DONOTWAIT`; an actual graphics error stops the renderer. There are no recurring window queries or resource recreation. Relative to one surface, the second uses roughly width * height * 4 bytes of additional GPU texture memory.

The Desktop renderer's existing presentation interval of `1` was preserved, so presentation currently uses VSync. The CPU loops have no software frame cap. Tests use this same renderer setting.

## Controls and settings

- **Insert:** show/hide the plain menu. Drag its title bar to move it.
- **End / Stop:** join the updater, clear the drawing and end the render thread. The DLL stays loaded.
- **Aim key:** click the button and press a key/mouse button. Escape cancels.
- **Page Up / Page Down:** cycle mortar targets while the menu is closed.
- **Settings:** save, load or reset. Save is explicit.

The menu has tabs, labels, buttons, toggles, numeric step buttons and drag sliders. Tahoma is loaded once through DirectWrite from Windows; there are no embedded fonts, extra font files, images, animations or ImGui dependencies. Cursor position is read only while the menu is open and translated using the cached overlay origin. Input and drawing have no focus gate.

Mouse presses retain their original coordinates until a menu frame processes them, so skipped rendering iterations do not erase pending clicks. The frame clears any unhandled click afterward. Buttons respond once per press with no timer or cooldown; slider taps also work when the mouse is released before the next frame.

Text layouts use a bounded 1024-entry cache with eight entries per hash bucket. Colliding names and distance labels can coexist; a full bucket replaces its least recently used entry. The cache uses a simple access counter, not a clock or Windows API. Its records occupy about 288 KiB of zero-initialized runtime storage, plus the retained DirectWrite layouts; font data is still loaded from Windows. This replaces the 128-slot cache that repeatedly rebuilt unchanged labels in crowded scenes.

The round radar defaults to the top right with a 12-pixel margin. World > Radar X/Y are sliders for its center coordinates, limited so the circle stays on-screen. Reset radar position restores the top-right default. Its grid and range rings are static, and markers outside its world range are omitted.

The visual styling follows `ArcRaiders-main/Overlay/render.h` and `drawing.h`: box height comes from the projected head and mesh root bone at the feet, width is half the height, and corner length is the larger dimension divided by 3.5. Corners have dark outlines and a pale outer edge; full boxes and skeletons use the reference's layered lines. Skeleton thickness and alpha decrease with distance. A distance estimate is retained when either projected bone is unavailable. Box and label backgrounds use the reference's static tints. The mesh root's name is cached with the other bone names; boxes require one additional socket read and projection per visible player. The head and mesh root are projected even when both skeleton drawing and aiming are disabled. The actor root component is not used as the box's lower edge.

Colors uses the reference's startup rainbow palette frozen at its initial values: green visible boxes, red hidden boxes, yellow values and selected targets, magenta warnings, and a dark panel. There is no palette cycling or theme system. RGBA sliders cover players, world actors, effects, radar and menu colors; box and label shading can be disabled by setting their alpha to zero. Tahoma was selected in place of the reference's default Proggy Clean font to use an installed Windows font without bundling a file.

Settings use a versioned binary value under `HKCU\Software\WDGS\Rewrite`. Version 2 adds the palette. Version 1 saves are migrated in memory: existing options, radar coordinates and custom colors are preserved; unchanged old color defaults adopt the new defaults. Save writes version 2. Old JSON settings are separate. The supplied source's disabled SPH-2 behavior and omitted unimplemented controls remain unchanged.

All debug output goes through the fixed-stack-buffer `log()` helper, which automatically prefixes every message with `WD_xetal: `. Only startup, stop, match-context changes and error messages are emitted; the periodic perf report was removed.

## Verification

`tests\main.cpp` covers guarded memory access, settings persistence, prediction, menu actions, transparent pixels, fixed initialization dimensions, ordered GPU completion, two-buffer backpressure, clearing on stop and drawing to a hidden window owned by another process. Hidden-window drawing does not require focus. The tests use a temporary `HKCU\Software\WDRewriteTests` key and delete it afterward. Captures, test waits and GPU readback are excluded from the DLL.

`tests\frame_exchange.cpp` pauses a producer halfway through a scan, holds the handoff lock to check nonblocking rendering, exchanges 3000 snapshots while settings change, and verifies stop/join and invalid-world clearing. It checks that player, bone, vehicle, loot and mortar data belong to the same completed publication and remain stable when the producer reuses its old buffer.

Visual tests cover radar slider dragging, release and reset, circular transparency, boundary markers, moving without stale pixels, both box styles without skeleton drawing, color editing, palette/position persistence and version 1 migration. Captures are written to `build/menu-*.bmp`, `build/scene.bmp`, `build/radar-*.bmp` and `build/box-*.bmp` for inspection. This visual update adds no imported functions.

Menu input tests cover a tab press followed by 1000 mouse polls without drawing, cursor movement and release before rendering, consecutive tab/control actions, held-button suppression, outside clicks and a released slider tap.

After building the tests, run `build\wd-tests.exe --crowd-bench` for a 1920x1080 synthetic scene with 16, 64 and 128 players, full skeletons, corner boxes, health, Tahoma names and distance labels. It reports draw/submission timings and text-layout creations, checks that unchanged labels stay cached after warmup, and saves `build\crowded.bmp`. `build\crowd-before.txt` and `build\crowd-after.txt` record this cache change; the corrected 128-player static scene went from 222 new layouts per frame and 7.685 ms mean drawing time to zero and 6.688 ms. The captures matched byte for byte. The old renderer intentionally fails the three static-label reuse regressions; the current renderer passes. These timings exclude game scanning and do not establish in-game FPS or snapshot update frequency. Benchmark timers and counters are excluded from the DLL.

`tests\transitions.cpp` runs the real game update against synthetic world/player objects and mock native entry points. It verifies no native calls in a frontend context, invalidation on leaving/unpossession, reacquisition on rejoining or world replacement, and rejection of readable objects marked for destruction. It verifies entry/exit gating, not a full simulated match. The two latest PACKER dumps inspected for the reported menu/exit crashes were zero bytes; see `build\transition-notes.txt`. In-game transition stability still needs confirmation.

Pointer validation retains the original app's `IsBadReadPtr` / `IsBadWritePtr` prechecks and guarded copies. Removing these previously exposed the crash recorded in `build\crash-analysis.txt`; they were not removed for performance. Prechecks cannot eliminate races with the game changing memory.

The October 7 mid-match dump traces an invalid class name through the updater's stationary-vehicle classification into native `Conv_NameToString`. Name conversion now checks the existing name-pool bounds, entry and readable string before calling the engine. `tests\name_conversion.cpp` covers the exact dump value, missing blocks, empty and unreadable entries, and a valid conversion. See `build\midmatch-analysis.txt` for the evidence. This guards that failure path; it does not establish object lifetime or thread safety for every native engine call.

See `verification.txt`, `build\imports.txt`, `build\size-and-imports.txt` and `build\two-thread-notes.txt` for this build. `build\standalone-notes.txt` and `build\performance-notes.txt` describe earlier versions. The supplied `third_party\minhook` files remain as reference only and are excluded from both projects.

The game, Echo's concurrent drawing and the custom loader have not been exercised here. Standalone tests do not establish gameplay parity, worker-thread compatibility with every engine call, or an FPS loss of three or less. The DLL uses the normal PE/CRT entry point and exception metadata; do not free its image while either worker is running.
