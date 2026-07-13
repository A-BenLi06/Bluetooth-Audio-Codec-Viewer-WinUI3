# Walkthrough

## 2026-07-06 13:45:43 +08:00

Implemented `BluetoothAudioCodec.cs` as a .NET 10 file-based application, so the
utility remains a single source file while still declaring its TraceEvent
dependency in-file.

The program subscribes to the Windows Bluetooth A2DP ETW provider and reads the
codec IDs from the actual `A2dpStreaming` negotiation event. It resolves standard
codecs and known vendor codec tuples, including SBC, AAC, aptX Classic, aptX HD,
aptX Low Latency, LDAC, and LHDC. Unknown values are returned with their raw IDs
instead of being guessed.

The implementation also:

- prints the current default render endpoint;
- plays an optional quiet diagnostic tone to create an audio stream;
- supports bounded capture, continuous watch, no-tone, and JSON modes;
- explains the distinction between A2DP, HFP, and Bluetooth LE Audio when no
  A2DP event is observed;
- validates arguments and ETW elevation requirements without changing Bluetooth
  or audio configuration.

Design rationale: ETW exposes the negotiated codec, while device capability lists
only show what a headset could support. Reading fields by event name first and
using the documented event layout only as a compatibility fallback makes schema
handling more robust.

## 2026-07-06 13:48:29 +08:00

Adjusted Core Audio COM activation after the first compile check. The default
endpoint enumerator is now instantiated through its registered CLSID and cast to
the documented interface, which avoids an invalid compile-time coclass conversion.
The Windows-only annotation also makes the platform contract explicit to static
analysis.

## 2026-07-06 13:49:05 +08:00

Narrowed the Windows platform annotation to the COM entry method. This preserves
platform analysis for the native Core Audio call without incorrectly marking the
plain endpoint result model and its `FriendlyName` property as Windows-only.

## 2026-07-06 13:49:39 +08:00

Marked the private COM release helper as Windows-only as well. This removes the
last platform analyzer warning while keeping the native-call boundary precise.

## 2026-07-06 13:51:39 +08:00

Verification completed:

- `dotnet run --file .\BluetoothAudioCodec.cs -- --help` compiled and ran
  without warnings;
- invalid timeout input returned exit code 2 with a specific validation error;
- a non-elevated capture attempt returned exit code 2 with the required
  administrator guidance;
- framework-dependent single-file publishing succeeded for `win-x64`, and the
  published executable's help path returned exit code 0;
- `git diff --check` reported no whitespace errors.

The live ETW callback cannot be exercised from the current non-elevated session;
the program checks this prerequisite before creating a trace session.

## 2026-07-13 20:03:36 +08:00

Added a dedicated unpackaged WinUI 3 application in
`BluetoothAudioCodec.WinUI`. The original single-file command-line utility is
preserved unchanged, while the desktop application reuses the same ETW event
schema, codec catalog, Core Audio endpoint lookup, and diagnostic tone behavior.

The interface uses a Mica backdrop, system theme resources, a single primary
action, a prominent codec result card, a compact status badge, and a collapsible
technical-details section. The layout supports both light and dark Windows
themes and remains usable at its 680 × 600 minimum size.

The app starts without a UAC prompt so its state can be viewed normally. When
the process is not elevated, the primary action explicitly restarts the app as
administrator; this is required only when beginning the ETW capture. Detection
runs on a worker thread, can be canceled from the UI, and distinguishes a
timeout from user cancellation or a trace error.

## 2026-07-13 20:09:23 +08:00

Corrected the initial and minimum window dimensions after visually inspecting
the running app on a high-DPI display. `AppWindow` dimensions are physical
pixels, so the requested design size is now multiplied by the WinUI XAML root's
rasterization scale after the root visual loads. This preserves the intended
860 × 760 device-independent layout at 125%, 150%, and other Windows display
scales instead of opening a cramped, prematurely scrolling window.

## 2026-07-13 20:11:15 +08:00

Set the main scroll viewer's horizontal content alignment to stretch. This
keeps the centered, maximum-width content column and its primary action inside
the intended right margin instead of letting the direct child use an
unconstrained scroll-content width.

## 2026-07-13 20:13:23 +08:00

Reverted the scroll viewer content-alignment change after runtime verification
showed that WinUI 3 collapses the star-sized sections of a constrained direct
`StackPanel` child in this configuration. The original direct-child measurement
behavior is restored, which brings back the full header, result card, endpoint,
message, details, and footer layout.

## 2026-07-13 20:15:45 +08:00

Verified the repaired XAML with a clean build directed to an unlocked temporary
output folder because an earlier elevated preview still held the normal Debug
executable open. The repaired project compiled with zero warnings and zero
errors, the unsupported horizontal content-alignment property is absent, and
`git diff --check` reports no whitespace errors.

## 2026-07-13 20:16:23 +08:00

Removed redundant blank lines at the end of newly added source and project
files after checking the completed commit itself. This keeps commit-level
whitespace validation clean without changing runtime behavior.

## 2026-07-13 23:25:24 +08:00

Moved initial window sizing out of the root visual's `Loaded` handler. Resizing
the native `AppWindow` while WinUI was already inside its first XAML layout pass
could leave star-sized content measured against the stale client area until the
user manually resized the window.

The window now obtains its monitor DPI directly from the HWND with
`GetDpiForWindow` and calls `MoveAndResize` in the constructor, before the window
is activated. The first XAML measure therefore starts with the final DPI-aware
client size, while the `Loaded` handler is limited to populating device and
elevation state.

Verification at 2026-07-13 23:26:19 +08:00: the x64 Debug project rebuilt in
the normal output directory with zero warnings and zero errors, and
`git diff --check` completed without whitespace errors. Per user request, no UI
automation or automated window launch was used for this verification.
