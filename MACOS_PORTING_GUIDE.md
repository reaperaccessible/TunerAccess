# TunerAccess — macOS Porting Guide (VoiceOver: VST3 + AU + Standalone)

## 1. Overview

TunerAccess is a JUCE 8 accessible instrument tuner. It detects the pitch of a guitar or bass via a YIN pitch detector, announces the result through a screen reader, and offers a continuous 880 Hz "in-tune lock tone." The Windows build (NVDA screen reader, VST3 + Standalone) is **complete and shipping as version 1.00**.

This guide is for the **macOS port** driven by collaborator **math65 (Mathieu)**. The macOS target ships **three formats — VST3, Audio Unit (AU), and Standalone — with VoiceOver** as the screen reader. The codebase is already cross-platform: Windows-only code is gated behind `#ifdef _WIN32` / `#if JUCE_WINDOWS`, and the non-Windows path routes screen-reader speech through `juce::AccessibilityHandler::postAnnouncement` (the official JUCE → NSAccessibility/VoiceOver bridge). **Most of the porting work is verification, not new code.** This document tells you exactly what already works, what is gated and may need a macOS equivalent, and how to verify each piece under VoiceOver / `auval`.

Project root on the Mac: the repository clone (referred to below as the repo root). All source is under `Source/`.

---

## 2. Current cross-platform state

### 2.1 How Windows code is gated

- **Compiler gate:** Windows-only blocks are wrapped in `#if JUCE_WINDOWS` (inside `.cpp`/`.h`) or `#ifdef _WIN32`. On macOS these blocks are **not compiled** — they simply vanish.
- **CMake gate:** ASIO (`JUCE_ASIO=1`), the ASIO SDK include dir, MSVC flags, and Windows defines (`NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `JUCE_WINDOWS_ALT_KEY_TRIGGERS_MENU=0`) are all inside `if(WIN32)` blocks (`CMakeLists.txt:86-99`, `101-112`, `129-134`). The AU format and the macOS deployment target are inside `if(APPLE)` blocks (`CMakeLists.txt:10-12`, `29-33`, `119-121`).
- **Screen-reader gate:** All `screenReaderAnnounce*()` wrappers have a `#if JUCE_WINDOWS` (NVDA / UIA) branch and a `#else` (macOS `postAnnouncement`) branch. See `Source/PluginEditor.cpp:148-213`.

### 2.2 What Mathieu already did in commit 273d51c — DO NOT REDO

1. **`announceValue()` non-Windows path** (`Source/PluginEditor.cpp:215-233`). On macOS, VoiceOver does **not** re-speak the value of an already-focused component on `NSAccessibilityValueChangedNotification`. The fix explicitly calls `juce::AccessibilityHandler::postAnnouncement(valueIface->getCurrentValueAsString(), high)` on non-Windows (`PluginEditor.cpp:226-232`). This is **already in place** — do not duplicate it.
2. **Microphone permission for macOS TCC** (`CMakeLists.txt:51-54`). `juce_add_plugin` sets `MICROPHONE_PERMISSION_ENABLED TRUE` and `MICROPHONE_PERMISSION_TEXT "TunerAccess needs microphone access to detect the pitch of your instrument."` JUCE injects this as `NSMicrophoneUsageDescription` into the Info.plist of all three formats, so macOS shows the system mic prompt on first launch. **Already in place** — do not duplicate.

Build on top of these. Your job is to verify they work and to handle the remaining gated/untested areas below.

---

## 3. Building on macOS

Requirements: Xcode 14+ (Command Line Tools installed), CMake 3.22+. JUCE 8.0.12 is fetched automatically via `FetchContent` (`CMakeLists.txt:17-24`) — no manual JUCE checkout needed.

```bash
# From the repo root
cmake -B build -G Xcode
cmake --build build --config Release
```

- **Deployment target:** `CMAKE_OSX_DEPLOYMENT_TARGET` defaults to `11.0` (Big Sur) — `CMakeLists.txt:10-12`. Leave as-is unless you have a reason to change it.
- **Format targets generated:** `TunerAccess_VST3`, `TunerAccess_AU`, `TunerAccess_Standalone` (AU only on APPLE — `CMakeLists.txt:30`, `119-121`).
- **Artefacts land in:**
  - `build/TunerAccess_artefacts/Release/Standalone/TunerAccess.app`
  - `build/TunerAccess_artefacts/Release/VST3/TunerAccess.vst3`
  - `build/TunerAccess_artefacts/Release/AU/TunerAccess.component`
- **`COPY_PLUGIN_AFTER_BUILD=FALSE`** (`CMakeLists.txt:47`) — CMake does NOT auto-install. Copy manually for local testing:

```bash
cp -R build/TunerAccess_artefacts/Release/VST3/TunerAccess.vst3   ~/Library/Audio/Plug-Ins/VST3/
cp -R build/TunerAccess_artefacts/Release/AU/TunerAccess.component ~/Library/Audio/Plug-Ins/Components/
# Standalone: run in place
open build/TunerAccess_artefacts/Release/Standalone/TunerAccess.app
```

If macOS Gatekeeper blocks an unsigned bundle, see §7.3.

---

## 4. Prioritized task checklist

Work top-down. Do not start P1 until P0 passes.

### P0 — Build & run

**P0.1 — Build all three formats.**
- Run the commands in §3.
- **Verify:** `ls build/TunerAccess_artefacts/Release/` shows `Standalone/`, `VST3/`, `AU/`. `TunerAccess.app/Contents/MacOS/TunerAccess` exists. `file build/TunerAccess_artefacts/Release/AU/TunerAccess.component/Contents/MacOS/TunerAccess` reports a Mach-O binary.

**P0.2 — Mic permission prompt (TCC).** Already wired (`CMakeLists.txt:51-54`).
- **Verify:** Launch `TunerAccess.app`. macOS prompts *"TunerAccess would like to access your microphone."* Click **Allow**. Confirm the key exists: right-click the `.app` → Show Package Contents → `Contents/Info.plist` contains `NSMicrophoneUsageDescription`. If no prompt appears, the key is missing — rebuild and re-check.

**P0.3 — Input is unmuted at startup.** `CustomStandaloneApp.cpp` forces `shouldMuteInput=false` at startup (`getMuteInputValue().setValue(false)`), because JUCE's `StandalonePluginHolder` defaults input to muted. Platform-agnostic.
- **Verify:** Launch standalone, start tuner (Enter), play a note → pitch is detected. If silent, check System Settings → Privacy & Security → Microphone → TunerAccess is ON.

### P1 — VoiceOver speech + focus

**P1.1 — Announcement pipeline.** All `screenReaderAnnounce*()` route to `postAnnouncement` on macOS (`PluginEditor.cpp:148-213`): `screenReaderAnnounce` → medium (`:157-158`), `screenReaderAnnounceNow` → high (`:171-172`), `screenReaderAnnounceInterrupt` → high (`:196-197`), `screenReaderAnnounceNext` → medium (`:210-211`). No new code expected — **verify only**.
- **Verify (VoiceOver, Cmd+F5):** Launch standalone → Tab to Tuner → hear the focus announcement ("Guitar Tuner … Press H for help"). Press **H** → full help is spoken (`announceHelp`, full text via `screenReaderAnnounceNow`). Press Enter to start → hear "Tuner started"-style announcement. Open Audio Settings (F10) → hear "Audio settings opened" queued after focus. If anything is silent, confirm the window is key/frontmost and that `postAnnouncement` is firing on the main thread.

**P1.2 — Value-changed re-speech (commit 273d51c).** Already implemented (`PluginEditor.cpp:226-232`). Exercised on Left/Right field nav, Up/Down value change, Home/End, Ctrl+Up/Down string change in `TunerComponent`, and on input slot / gain / channel changes via `refreshAccessibility()` in `InputComponent`.
- **Verify:** Tab to Tuner. Press Right → hear the new field/value without re-tabbing. Press Up/Down → hear the new value. Press Ctrl+Down → hear the new string. Tab to Input → Up/Down switches slot and re-speaks; Alt+Up/Down adjusts gain/channel and re-speaks.

**P1.3 — `AccessibleComboDropdown` announce stubs (NEW CODE LIKELY NEEDED).** The two static helpers `screenReaderAnnounce` / `screenReaderAnnounceNow` in `Source/AccessibleComboDropdown.cpp` (~lines 164-188) are **empty `juce::ignoreUnused` no-ops on non-Windows**. They are used for dropdown open/close/item-change/selection announcements. Without them, VoiceOver users cannot hear combo navigation in the audio-settings panel — **a blocking accessibility defect.**
- **Action:** Replace the non-Windows stubs with `juce::AccessibilityHandler::postAnnouncement(text, medium)` for `screenReaderAnnounce` and `…, high)` for `screenReaderAnnounceNow`, mirroring `PluginEditor.cpp:148-173`.
- **Verify:** F10 → Tab to "Audio Driver" → hear name + "Press Alt+Down to open list". Alt+Down → hear first item. Down/Up → hear each item ("1 of N, CoreAudio"). Enter → hear "… selected". Repeat for all 5 combos (Driver, Device, Output Pair, Sample Rate, Buffer Size).

**P1.4 — Initial focus on launch.** The Win32 focus kickstart (Stage B: `SetFocus` + `PostMessage(VK_TAB)`) is `#if JUCE_WINDOWS` gated (`PluginEditor.cpp:~1069-1083`) and **does not run on Mac**. Stage C (`tunerComp.grabKeyboardFocus()`) and Stage D (`handler->grabFocus()`) are cross-platform.
- **Action:** Test first; add macOS code only if needed.
- **Verify:** Launch standalone with VoiceOver. Within ~1-2 s, VoiceOver should announce the Guitar Tuner focus without manual tabbing. If silent, add a non-Windows-guarded `topLevel->toFront(true)` (or `[NSApp activateIgnoringOtherApps:YES]` via an Obj-C++ bridge) after Stage A, guarded with `#if JUCE_MAC` to avoid breaking other platforms.

**P1.5 — Title prefix suppression.** The parent-chain `setName("")` + `setTitle(" ")` (Stage A, ~`PluginEditor.cpp:1043-1047`) suppresses the NVDA "TunerAccess fenêtre" prefix on Windows. This code is cross-platform (no gate) but its **effect on NSAccessibility is untested.**
- **Action:** Test first.
- **Verify:** Tab through Tuner → Input → settings combos. If VoiceOver prefixes the app/window name on every Tab ("TunerAccess Instrument…"), investigate suppressing `NSAccessibilityTitleAttribute` on the native window (likely a JUCE/NSWindow concern). If no prefix is heard, leave as-is.

**P1.6 — Focus recovery after app switch.** `componentBroughtToFront()` has a Win32 `SetFocus(hwnd)` block (`PluginEditor.cpp:~1144-1155`, gated) plus a cross-platform focus-restore block (~`:1160-1192`).
- **Verify:** Tab to Input → Cmd+Tab away → Cmd+Tab back. VoiceOver should re-announce the focused field. If broken, rely on the cross-platform restore; add native code only if necessary.

**P1.7 — F2 inline rename.** `InputComponent` F2 creates a JUCE `TextEditor` overlay, announces the rename prompt, commits on Enter / cancels on Escape, then `refreshAccessibility()`. Cross-platform.
- **Verify:** Tab to Input → F2 → hear "Rename Input 1, Current name: …, Enter to confirm, Escape to cancel". Type, Enter → hear "Renamed to …". F2 again, Escape → hear "Rename cancelled".

### P2 — Lock tone + pluck-to-hear (cross-platform; verify only)

**P2.1 — In-tune lock tone (880 Hz).** `PluginProcessor::renderLockTone()` (`Source/PluginProcessor.cpp:78-167`) synthesizes a persistent-phase 880 Hz sine on the **audio thread**, **NOT gated** by platform or `isStandalone`. Gate logic uses hysteresis (latch ON ≤2.5 cents, release >3.0 cents — `:123-129`), cents smoothing (tau ~0.25 s — `:119-120`), and a click-free ~8 ms envelope (`:152, :156-166`). Phase increment is `2π·880/sr` (`:154`); amplitude −15 dB (`:153`); mixed into all output channels (`:164-165`). It should already work in VST3/AU/Standalone on CoreAudio. Toggle = **T** key in `TunerComponent::keyPressed` (~`PluginEditor.cpp:891-898`): toggles `lockToneEnabled` atomic, calls `pushStateToProcessor()`, announces via `screenReaderAnnounceNow`.
- **Verify (listening test):** Start tuner. Play an in-tune note. Press **T** → hear "Lock tone on…" AND a steady, faint 880 Hz sine. Detune ~3 cents → tone gates off smoothly (no clicks). Return in-tune → tone fades back in. Press **T** → hear "Lock tone off", tone stops. Repeat in a DAW (VST3 in Reaper, AU in Logic) with monitoring on. Optionally record output and FFT — peak at 880 Hz ±5 Hz, clean envelope edges.

**P2.2 — Lock-tone target tracking.** `TunerComponent::pushStateToProcessor()` publishes the target via `p.setLockTarget(midiNote, true)` for guided presets, `setLockTarget(-1, false)` for Free Chromatic. The audio thread reads `lockTargetMidi` / `lockGuided` atomics in `renderLockTone` (`PluginProcessor.cpp:96-107`).
- **Verify:** Change tuning preset / string and confirm the lock tone latches at the new target's in-tune window (tone pitch stays 880 Hz; the gate threshold tracks the new target).

**P2.3 — Pluck-to-hear announcement.** `TunerComponent::timerCallback` runs at 15 Hz and emits **one** announcement per pluck via `screenReaderAnnounceInterrupt`. Pure logic, platform-agnostic.
- **Verify:** Start tuner, pluck once → exactly one announcement ("Target E, tuned" / "Target E, sharp 15 cents, tune down"). Pluck again → re-arms and announces once. No stale/repeated speech.

### P3 — Audio Unit validation

**P3.1 — `auval`.** AU metadata: `PLUGIN_CODE=TuAc`, `PLUGIN_MANUFACTURER_CODE=RpAc`, `AU_MAIN_TYPE=kAudioUnitType_Effect` (`CMakeLists.txt:38-39, 49`). Type code for an effect is `aufx`.
- **Action:** Install the AU, then validate.

```bash
cp -R build/TunerAccess_artefacts/Release/AU/TunerAccess.component ~/Library/Audio/Plug-Ins/Components/
killall -9 AudioComponentRegistrar 2>/dev/null   # force re-scan
auval -v aufx TuAc RpAc
```

- **Verify:** Output ends with no red errors ("passed … tests" / "AU validation succeeded"). If you see `-9450` (code/manufacturer collision), change `PLUGIN_CODE` / `PLUGIN_MANUFACTURER_CODE` in `CMakeLists.txt:38-39` (each exactly 4 bytes) and rebuild. Confirm the AU is a native binary for the Mac's architecture (universal or arm64 on Apple Silicon) to avoid Rosetta issues.

### P4 — Packaging

**P4.1 — Installer.** There is no macOS installer yet (Windows uses Inno Setup `installer/TunerAccess.iss`). Recommended: a `.pkg` built with `pkgbuild` + `productbuild`, or a `.dmg` with drag-drop instructions.
- The installer must place:
  - `TunerAccess.vst3` → `~/Library/Audio/Plug-Ins/VST3/`
  - `TunerAccess.component` → `~/Library/Audio/Plug-Ins/Components/`
  - `TunerAccess.app` → `/Applications/`
  - `TunerAccess_Manual.html` → `~/Library/Application Support/TunerAccess/` (see §6.4)
- Use a preinstall script with `mkdir -p` for the plugin folders, and `pkgbuild --ownership preserve` to avoid permission issues that break codesign validation.
- **Verify:** After install, `ls ~/Library/Audio/Plug-Ins/VST3/ ~/Library/Audio/Plug-Ins/Components/` lists both bundles; both load in a DAW. For distribution, sign + notarize (see §7.3).

---

## 5. Reference: every Windows-gated site and its macOS decision

| Site (file:line) | What it does on Windows | macOS decision |
|---|---|---|
| `PluginEditor.cpp:150-159` `screenReaderAnnounce` | NVDA `speakSsml(NORMAL)` / UIA | `#else` → `postAnnouncement(medium)`. **Done.** Verify. |
| `PluginEditor.cpp:162-174` `screenReaderAnnounceNow` | NVDA `speakSsml(NOW)` / UIA | `#else` → `postAnnouncement(high)`. **Done.** Verify. |
| `PluginEditor.cpp:176-199` `screenReaderAnnounceInterrupt` | NVDA `cancelSpeech()` + `speakSsml(NOW)` | `#else` → `postAnnouncement(high)`. **Done.** Verify. |
| `PluginEditor.cpp:201-213` `screenReaderAnnounceNext` | NVDA `speakSsml(NEXT)` / UIA | `#else` → `postAnnouncement(medium)`. **Done.** Verify. |
| `PluginEditor.cpp:224-232` `announceValue` | `notifyAccessibilityEvent(valueChanged)` | `#else` → `postAnnouncement(high)`. **Done in 273d51c.** Verify. |
| `AccessibleComboDropdown.cpp:~164-188` static announce helpers | NVDA / UIA | `#else` currently **empty no-op**. **ACTION: implement `postAnnouncement`** (P1.3). |
| `AccessibleSettingsPanel.cpp/.h notifyToggleStateChanged` | UIA `UiaRaiseAutomationPropertyChangedEvent` | Empty `ignoreUnused` non-Windows. **No shipped toggle buttons** → leave as-is. Future concern only. |
| `PluginEditor.cpp:~1069-1083` Stage B focus kickstart | `SetFocus` + `PostMessage(VK_TAB)` | Gated out on Mac. **Test launch focus (P1.4)**; add Mac code only if needed. |
| `PluginEditor.cpp:~1144-1155` `componentBroughtToFront` | `SetFocus(hwnd)` | Gated out; rely on cross-platform restore (P1.6). |
| `PluginEditor.cpp:~1043-1047` parent-chain `setTitle(" ")` | UIA Name suppression | Cross-platform, untested on NSAccessibility. **Test (P1.5).** |
| `PluginEditor.cpp:~920-933` Alt+F4 → `systemRequestedQuit()` | Windows quit idiom | Harmless on Mac; Cmd+Q quits via JUCE default. No change. |
| `CMakeLists.txt:86-99, 129-134` `JUCE_ASIO=1` + ASIO SDK | Exposes ASIO device type | `if(WIN32)` only — CoreAudio replaces it. Verify no ASIO symbols on Mac. |
| `CMakeLists.txt:101-112` MSVC flags | `/EHa`, `/DEBUG`, etc. | `if(MSVC)` only — skipped. |
| `PluginEditor.cpp` NVDA struct / `initNvda()` / UIA helpers | Load `nvdaControllerClient64.dll`, UIA | `#if JUCE_WINDOWS` only — not compiled on Mac. |

---

## 6. Feature notes for the latest additions

### 6.1 In-tune lock tone — cross-platform, verify only
See P2.1/P2.2. The synthesis (`PluginProcessor.cpp:78-167`) has **no platform gates** and works on CoreAudio. The only Mac-specific risk is audio routing/monitoring, not the code. The T-key announcement depends on `screenReaderAnnounceNow` → `postAnnouncement` (P1.1).

### 6.2 Pluck-to-hear — verify only
15 Hz `timerCallback` state machine, platform-agnostic; emits one `screenReaderAnnounceInterrupt` per pluck (P2.3).

### 6.3 7-string guitar + 4/5/6-string bass
Instruments/tunings live in `Source/GuitarTuner.h` (TunerEngine + tunings) and the YIN detector in `Source/YinPitchDetector.h` — both header-only and platform-agnostic. YIN `kAnalysisSize = 4096` supports bass down to ~25-31 Hz (5-string bass low B ≈ 30.87 Hz). Navigation is via `TunerComponent` `navField` (0=Instrument, 1=Tuning, 2=String), all cross-platform.
- **Verify:** With a multi-channel CoreAudio interface, switch to "5-string bass, Standard B", pluck the low B at small buffer sizes (64/128) and large (512/1024) — pitch must be detected and announced at all buffer sizes. Cycle Instrument/Tuning/String with Up/Down/Left/Right/Home/End and confirm VoiceOver re-speaks.

### 6.4 F1 manual path on Mac
`openManual()` (`Source/PluginEditor.cpp:~971-1000`) searches, in order: (1) `userApplicationDataDirectory/TunerAccess/TunerAccess_Manual.html` → on Mac `~/Library/Application Support/TunerAccess/TunerAccess_Manual.html`; (2) executable dir; (3) executable dir `/Docs`; (4) a hardcoded `C:\Claude\TunerAccess\Docs` Windows dev fallback (harmless on Mac — never matches). Opens via `File::startAsProcess()` → `/usr/bin/open` → default browser.
- **Action:** Ensure the installer (§P4) places `TunerAccess_Manual.html` at `~/Library/Application Support/TunerAccess/` so all three formats find it. Optionally also bundle it in `TunerAccess.app/Contents/Resources/Docs/` for offline standalone use (matches search path 3). Do **not** remove the Windows dev fallback.
- **Verify:** Press **Fn+F1** (see §7.2) → hear "Opening the user manual in your browser" and the browser opens the manual. If absent → "User manual not found".

### 6.5 Input presets / CoreAudio routing
- `processBlock` (`PluginProcessor.cpp:38-76`): selects input channel + applies gain → mono → feeds tuner → copies mono to all output channels (mono→stereo passthrough) → `renderLockTone()`. Platform-agnostic.
- `applyActiveInput(dm)` (`PluginProcessor.cpp:177-214`): standalone-only (nullptr in plugin context). Computes the 2-channel pair containing the desired device channel, builds a `BigInteger` input mask, calls `dm.setAudioDeviceSetup()`. On Mac this drives the CoreAudio HAL transparently — no ASIO/WASAPI needed.
- `AccessibleSettingsPanel` enumerates device types via `dm.getAvailableDeviceTypes()` — on Mac this shows **CoreAudio** (not ASIO/DirectSound/WASAPI). The hint string in the panel still says "(ASIO, DirectSound, WASAPI…)"; consider updating that text for Mac, but it is non-blocking.
- **Verify:** F10 → Audio Driver shows CoreAudio only. Device/Output Pair/Sample Rate/Buffer combos populate from the selected CoreAudio device. With a multi-channel interface, select a non-zero device channel in InputComponent (Right to Device field, Alt+Up/Down) and confirm the tuner reads only that channel.

---

## 7. macOS gotchas

### 7.1 TCC microphone permission
Wired via `CMakeLists.txt:51-54`. On first launch the system prompts; the user must click **Allow**. If denied, mic input is **silently** blocked (no error, just no pitch).
- Re-grant: System Settings → Privacy & Security → Microphone → enable TunerAccess, then relaunch.
- To force the prompt again during testing (reset TCC for this app):
  ```bash
  tccutil reset Microphone   # resets mic consent for all apps
  ```
  (There is no per-bundle-id form that is reliable across versions; the global reset above re-arms the prompt.)

### 7.2 Function-key (Fn) behavior
On Mac laptops, F1–F12 default to media/brightness keys. F1 (manual), F2 (rename), F10 (audio settings) may require **holding Fn** (e.g. Fn+F1). The **T** key and arrow navigation are unaffected.
- Document in the manual/help: "On macOS, hold Fn to use F1/F2/F10, or enable System Settings → Keyboard → 'Use F1, F2, etc. as standard function keys'."
- F10 has a Tab-reachable alternative: since 1.01 there is an **in-editor "Audio Settings" button** added in `PluginEditor` (standalone only, `setExplicitFocusOrder(3)`, after Tuner=1 and Input=2). It is a plain `juce::TextButton` (role button, natively accessible) — VoiceOver should announce "Audio Settings, button" and Space/Enter opens the panel. This is cross-platform shared code; just verify it appears in the Tab order on Cocoa. The older title-bar "Audio Settings" button (from `CustomStandaloneApp.cpp`, renamed from JUCE's stock "Options") is mouse-only chrome and not in the focus container — do NOT rely on it for keyboard/VoiceOver.

### 7.3 Code signing & notarization
Unsigned local builds run but trigger Gatekeeper on first launch.
- Bypass for testing: `xattr -rd com.apple.quarantine build/TunerAccess_artefacts/Release/Standalone/TunerAccess.app` (and the installed plugin bundles).
- For distribution: `codesign --timestamp --options=runtime --sign "Developer ID Application" <bundle>` for each of the `.app`, `.vst3`, `.component`; then sign the `.pkg` with `productbuild --sign "Developer ID Installer"`; then `xcrun notarytool submit … && xcrun stapler staple`.

### 7.4 AU code collisions
`TuAc` / `RpAc` must be unique among installed AUs. If `auval` returns `-9450`, change the codes in `CMakeLists.txt:38-39` and rebuild. Re-run `auval -v aufx <CODE> <MFG>`.

### 7.5 Formats not shipped
No AAX (ProTools-only, separate SDK), no VST2 (deprecated, dropped by JUCE 8). AU + VST3 cover Logic/GarageBand and Reaper/Bitwig respectively. No action needed.

---

## 8. VoiceOver acceptance test script (human-runnable)

Pre-req: Build passes (P0), plugins installed, manual placed at `~/Library/Application Support/TunerAccess/`. Enable VoiceOver with **Cmd+F5**.

1. **Launch & mic.** Open `TunerAccess.app`. Confirm the mic prompt appears → click **Allow**.
2. **Initial focus.** Within ~2 s, VoiceOver announces the Guitar Tuner focus ("…Press H for help") without tabbing. (P1.4)
3. **Help.** Press **H** → the full help text is spoken end-to-end. (P1.1)
4. **Instrument nav.** Tab to Tuner. Up/Down cycles instruments (incl. 7-string guitar, 4/5/6-string bass) — each is re-spoken. Home/End jump to first/last. (P1.2, 6.3)
5. **Tuning & string.** Right → "Tuning" field; Up/Down changes preset (spoken). Right → "String" field; Ctrl+Up/Down changes string (spoken). (P1.2)
6. **Start tuner & pluck.** Press Enter → "Tuner started". Pluck a string once → exactly one pitch announcement. Pluck again → re-arms and announces once. (P1.1, P2.3)
7. **Lock tone.** Tune in-tune, press **T** → "Lock tone on" + audible steady 880 Hz tone. Detune ~3 cents → tone fades out cleanly. Return in-tune → tone fades in. Press **T** → "Lock tone off". (P2.1)
8. **Input slot.** Tab to Input → hear "Input 1 …". Up/Down switches slot (spoken). Alt+Up/Down adjusts gain (dB spoken). Right → Device field; Alt+Up/Down cycles device channels (spoken). (P1.2, 6.5)
9. **Rename.** On Input, press **F2** (or Fn+F2) → rename prompt spoken. Type a name, Enter → "Renamed to …". F2 again, Escape → "Rename cancelled". (P1.7)
10. **Audio settings.** Press **F10** (or Fn+F10) or activate the "Audio Settings" title-bar button → "Audio settings opened". Tab through all 5 combos; for each: hear name + selection, Alt+Down opens list, Down/Up announces each item, Enter announces "… selected". Driver shows **CoreAudio**. Escape closes; focus returns to the editor. (P1.3, 6.5, 7.2)
11. **Manual.** Press **Fn+F1** → "Opening the user manual in your browser" and the browser opens the manual. (6.4)
12. **App-switch recovery.** Cmd+Tab away and back → VoiceOver re-announces the focused field. (P1.6)
13. **Title prefix check.** Tab through several fields → confirm NO redundant "TunerAccess" prefix before each field. If heard, file a follow-up for §P1.5.
14. **DAW formats.** In Logic (AU) and Reaper (VST3): load TunerAccess, confirm mic prompt (first time), pluck → announcement, T → lock tone audible on the track output.
15. **AU validation.** `auval -v aufx TuAc RpAc` → passes with no errors. (P3.1)

A pass on all 15 steps means the macOS port is acceptance-ready for VST3 + AU + Standalone with VoiceOver.
