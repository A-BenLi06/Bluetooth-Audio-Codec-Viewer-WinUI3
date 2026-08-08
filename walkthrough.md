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

## 2026-07-14 01:10:56 +08:00

Reworked the scroll-content measurement model after user screenshots showed
that the content column could retain the correct DPI-scaled width but be laid
out from an incorrect horizontal origin, clipping the card, status badge, and
primary action at the right edge on some display scales.

The `ScrollViewer` now explicitly disables horizontal scrolling and stretches a
full-viewport `Grid` host. That finite-width host owns the page padding, while
the inner `StackPanel` only owns the 780-DIP maximum content width. Star-sized
rows are therefore measured against the viewport rather than the scroll
content's unconstrained width, and the maximum-width column remains centered
across DPI and window-size changes.

Verification at 2026-07-14 01:12:21 +08:00: the repaired WinUI project compiled
to an unlocked temporary output directory with zero warnings and zero errors,
and `git diff --check` completed without whitespace errors. The normal Debug
executable was not replaced because the user's currently running instance holds
it open. No UI automation was used.

## 2026-08-08 13:32:06 +08:00

Repaired the optional administrator restart after the project had been changed
from unpackaged deployment to MSIX. The package previously declared only
`runFullTrust`; packaged desktop apps also require the restricted
`allowElevation` capability before Windows permits a runtime `runas` launch.

The restart path now resolves and validates the current executable for both
packaged and unpackaged launches, uses the executable directory as its working
directory, and exits the original process only after ShellExecute returns a
real child process. Canceling UAC remains non-destructive and is reported in the
existing localized information bar.

Generated `AppPackages`, `BundleArtifacts`, and certificate files are now
ignored. Source assets, localization resources, the package manifest, and the
solution file remain part of the project rather than being mistaken for build
output.

## 2026-08-08 17:56:17 +08:00

Adjusted the repaired elevation flow for Microsoft Store distribution. Release
builds now use `StoreUpload`, generate an unsigned `.msixupload` for Partner
Center, and correctly build both x64 and ARM64 package slices. The architecture
list now uses the MSIX toolchain's required pipe separator, and explicit
runtime identifiers keep each recursive bundle build on the matching CPU
architecture.

Restored the detector's stable ETW lifetime and audio endpoint implementation.
An intervening change had treated endpoint form-factor value 8 as Bluetooth
(that value represents S/PDIF) and could disable detection for real Bluetooth
headsets; it also moved ETW processing onto a manually disposed background
session with cancellation races. Localization and the valid packaging work were
preserved.

Validation produced
`BluetoothAudioCodec.WinUI_1.0.0.0_x64_ARM64_bundle.msixupload`. Both nested
architecture packages were inspected in memory and contain `runFullTrust` and
`allowElevation`. The Store package remains unsigned as required for Partner
Center ingestion. Before a production submission, associate the project with
its reserved Partner Center identity to replace `CN=PlaceholderPublisher`, and
request approval for the restricted `allowElevation` capability with a detailed
explanation that elevation is optional and is used only to read the Windows
Bluetooth A2DP ETW trace.
