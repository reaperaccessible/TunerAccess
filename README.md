# TunerAccess

Accessible guitar tuner — VST3 + AU + Standalone — built with JUCE 8 and designed first-class for blind users (NVDA on Windows, VoiceOver on macOS).

## Features

- 9 tuning presets: Standard E, Drop D, Open G, Open D, DADGAD, Open E, Half Step Down, Full Step Down, Free Chromatic
- YIN pitch detection (~60–400 Hz, optimised for guitar)
- 2 named input slots with independent device channel + gain (−12 dB to +24 dB), F2 to rename
- Mono input → stereo output passthrough with gain applied
- Inactive input physically disconnected from the JUCE bus (no leakage)
- Auto-save to `%APPDATA%\TunerAccess\TunerAccess.settings` (Windows) / `~/Library/Application Support/TunerAccess/TunerAccess.settings` (macOS)
- Accessible settings panel: Audio Driver, Audio Device, Output Pair, Sample Rate, Buffer Size (replaces JUCE's stock inaccessible dialog)

## Build

### Windows (Visual Studio 2022)

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

Artefacts:
- `build\TunerAccess_artefacts\Release\Standalone\TunerAccess.exe`
- `build\TunerAccess_artefacts\Release\VST3\TunerAccess.vst3\Contents\x86_64-win\TunerAccess.vst3`

Optional installer (requires Inno Setup 6 — see `installer/TunerAccess.iss`):
```powershell
"C:\Users\<you>\AppData\Local\Programs\Inno Setup 6\ISCC.exe" installer\TunerAccess.iss
```

### macOS (Xcode 14+)

```bash
cmake -B build -G Xcode
cmake --build build --config Release
```

Artefacts (after first build):
- `build/TunerAccess_artefacts/Release/Standalone/TunerAccess.app`
- `build/TunerAccess_artefacts/Release/VST3/TunerAccess.vst3`
- `build/TunerAccess_artefacts/Release/AU/TunerAccess.component`

Copy the VST3 to `~/Library/Audio/Plug-Ins/VST3/` and the AU to `~/Library/Audio/Plug-Ins/Components/` to make them available to DAWs.

## Project layout

```
Source/
  GuitarTuner.h              DSP — TunerEngine + tunings list (no UI)
  YinPitchDetector.h         YIN algorithm, header-only
  PluginProcessor.h/.cpp     AudioProcessor: input presets, processBlock, state save/restore
  PluginEditor.h/.cpp        Editor + TunerComponent + InputComponent + NVDA wrappers
  AccessibleComboDropdown.h/.cpp   Reusable Alt+Down-list combo, accessible
  AccessibleSettingsPanel.h/.cpp   Custom Audio settings panel (replaces JUCE stock)
  CustomStandaloneApp.cpp    Custom StandaloneFilterApp — overrides "Audio Settings"
                             title-bar button to open our accessible panel, forces
                             shouldMuteInput = false at startup.

asiosdk/                     Vendored ASIO SDK (Windows only, JUCE_ASIO=1)
nvda_2025.3.3_controllerClient/   Vendored NVDA Controller Client (Windows only)
installer/                   Inno Setup 6 bilingual EN/FR installer (Windows only)
```

## Platform notes

| Concern | Windows (implemented) | macOS (to do) |
|---|---|---|
| Screen reader | NVDA Controller Client DLL + UIA fallback | `juce::AccessibilityHandler::postAnnouncement` (cross-platform stub already in place — uses NSAccessibility) |
| Audio backends | ASIO (preferred) + DirectSound + WASAPI | CoreAudio (built-in to JUCE) |
| Standalone wrapper | `CustomStandaloneApp.cpp` — works | Same code compiles, but the Win32 `SetFocus + PostMessage(VK_TAB)` kickstart and the parent-chain `setTitle(" ")` are `#if JUCE_WINDOWS` guarded. VoiceOver focus model is different — see below. |
| Installer | Inno Setup 6 (`installer/TunerAccess.iss`) | Need a `.pkg` or `.dmg` recipe |

## Things that compile on Mac but need wiring

These are **the porting tasks**:

1. **VoiceOver hookup.** `screenReaderAnnounce*()` already calls `juce::AccessibilityHandler::postAnnouncement` on non-Windows, which is the official JUCE NSAccessibility bridge. Validate that VoiceOver actually speaks the announcements, and adjust priorities/timing if it doesn't.

2. **Inline rename (F2 in InputComponent).** Uses JUCE `TextEditor` — should work as-is, but verify the focus + Enter/Escape flow under VoiceOver.

3. **Standalone window focus kickstart.** The Win32 path in `TunerAccessAudioProcessorEditor` ctor (Stage B, ~line 870) is skipped on Mac. VoiceOver normally handles focus correctly without manual kickstart. Test on first launch — if VoiceOver doesn't catch the initial focus, you may need `[NSApp activateIgnoringOtherApps:YES]` somewhere or a JUCE `topLevel->toFront(true)` call.

4. **Parent-chain `setTitle(" ")`.** That's the Windows-specific "TunerAccess fenêtre" prefix workaround. On Mac there's an equivalent issue with NSWindow title being read by VoiceOver — investigate `NSAccessibilityTitleAttribute` if you hear the app name as a prefix on every Tab.

5. **Keyboard shortcuts.** F2 / F10 / Tab should work cross-platform. The `Alt+F4` handler is a no-op on Mac (Cmd+Q quits via JUCE's default).

6. **ASIO.** Skipped on Mac (`JUCE_ASIO=1` is `WIN32`-gated in CMake). CoreAudio replaces it transparently.

7. **AudioUnit format.** Already enabled (`AU` in `juce_add_plugin FORMATS` on `APPLE`). Verify the AU validates with `auval -v aufx TuAc RpAc` — adjust `PLUGIN_CODE`/`PLUGIN_MANUFACTURER_CODE` if there's a collision.

8. **Installer.** A simple `.pkg` script that drops the AU + VST3 + Standalone in the right `~/Library` folders. Or a `.dmg` with drag-drop instructions.

## Accessibility design principles (apply to macOS port)

- **Never** use JUCE's stock `AudioDeviceSelectorComponent` — it's inaccessible by both NVDA and VoiceOver. We always present our own `AccessibleSettingsPanel`.
- **One canonical entry point** for Audio Settings: a button in the title bar AND a keyboard shortcut. On Windows: button renamed "Audio Settings" + F10. On Mac: keep the button labelled "Audio Settings" — F10 should still work, but verify VoiceOver routes it correctly.
- **Mono → stereo passthrough** so the user can hear what's being analysed. The output pair is selected in the settings panel.
- **Single-widget interactions** for the tuner and the input slot — Up/Down navigates inside, Left/Right cycles parameters, Alt+Up/Down adjusts values, F2 renames. No mouse needed.
- **State persistence is automatic** via the JUCE `StandalonePluginHolder` properties file. Don't add extra save buttons.

## License

Proprietary — ReaperAccessible. See `installer/data/EULA.txt`.
