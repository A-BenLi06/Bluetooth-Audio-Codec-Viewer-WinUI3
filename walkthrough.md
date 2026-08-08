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

## 2026-08-08 19:57:31 +08:00

Diagnosed the remaining administrator restart failure from the Windows
Application and .NET Runtime event logs. Shell elevation created the child
process successfully, but the child crashed in the WinUI `Window` constructor
with exception `0xC06D007E`. The framework-dependent loose package relied on
package activation to establish the Windows App SDK dependency graph; launching
its executable directly through the `runas` verb did not provide that graph.

Enabled self-contained Windows App SDK deployment so the MSIX carries
`Microsoft.ui.xaml.dll`, `Microsoft.WindowsAppRuntime.dll`, and the other native
WinUI runtime files beside the application executable. The original process now
also waits up to ten seconds for the elevated child to enter its UI message loop
and remains open with an error message if the child exits or never becomes
ready.

The updated self-contained Debug package built successfully and was registered
from its complete extracted package layout. A direct executable launch reached
input-idle, stayed alive, and produced no new Application log errors. The
Release StoreUpload build also succeeded for x64 and ARM64; both nested MSIX
packages contain the WinUI runtime, `runFullTrust`, and `allowElevation`. The
resulting `.msixupload` is approximately 108 MB. No UI automation or elevated
launch was used during validation.

## 2026-08-08 23:38:42 +08:00

Replaced the MSIX Store distribution path with a traditional unpackaged Win32
application and offline MSI installers. The package manifest and
`allowElevation` capability are no longer used. The normal WinUI process stays
at standard integrity; codec detection launches the same signed single-file
executable in a hidden administrator-helper mode and exchanges the result over
a random current-user-only named pipe authenticated with a 256-bit token.
Cancellation is forwarded to the elevated ETW session without closing or
elevating the UI.

Added a custom WinUI entry point, x64 and ARM64 self-contained single-file
publishing, and a WiX 6.0.2 per-machine MSI project with an embedded payload,
Start menu shortcut, major-upgrade support, and silent `/qn /norestart`
installation. The app now references the focused Windows App SDK WinUI package,
and generated output directories are excluded from single-file content to
prevent recursive packaging and oversized executables. WiX 7 was not used
because its build required acceptance of an additional OSMF EULA.

The Store build script can sign the application EXE before MSI embedding, then
sign and timestamp the MSI, and can require a valid trusted-CA certificate for
release builds. The locally generated artifacts remain explicitly unsigned and
are therefore test-only until a production certificate and legal publisher name
are supplied. The previous development MSIX registration was removed.

Verification produced x64 and ARM64 installers of 51,445,760 and 48,427,008
bytes. Both MSI packages built with zero warnings and errors and passed silent
administrative extraction with exit code 0. Their embedded executables have the
expected x64 (`0x8664`) and ARM64 (`0xAA64`) PE machine values. The final x64
WinUI executable reached input-idle and stayed running with no Application log
errors; the helper pipe protocol returned an authenticated response in a
non-elevated test. The signing guard correctly rejected a release build without
a certificate. No UI automation, Computer Use, installation, or UAC prompt was
used during validation.

## 2026-08-08 23:59:23 +08:00

Fixed cancellation after observing an orphaned elevated helper and four stale
`BluetoothAudioCodec-WinUI-*` ETW sessions. TraceEvent documents that
`StopProcessing()` cannot interrupt a quiet real-time source until another
event arrives, so both the user-cancellation callback and the 30-second timeout
worker now dispose the `TraceEventSession`. Session disposal is thread-safe and
forces `Process()` to wake promptly; a cancellation race immediately before
`Process()` starts is also handled as a normal canceled result.

The normal-permission UI now waits for the helper response with a linked,
cancellation-aware read. On cancel it makes a bounded one-second attempt to send
the authenticated cancel command, then returns control to the UI; closing the
pipe remains a second cancellation signal to the helper. Both helper-returned
and locally observed cancellation use the same localized ready state and
message.

The stale helper process and all four old ETW sessions were stopped without UAC.
The x64 Debug project rebuilt with zero warnings and errors, the final x64 and
ARM64 single-file applications and MSI packages rebuilt with zero warnings and
errors, and both installers passed administrative extraction with exit code 0
and exact SHA-256 payload matches. A non-elevated helper protocol test connected,
returned an authenticated response, exited, and left zero codec ETW sessions.
No Computer Use, UI automation, installation, or UAC prompt was used.

## 2026-08-09 00:33:25 +08:00

Fixed the remaining visual cancellation state. Detection cancellation had
already returned the status badge and action button to Ready, but the main codec
card was never restored, leaving `Listening...` and the waiting protocol text
visible beside the successful cancellation message.

The window now snapshots the complete detection presentation immediately before
entering the listening state: codec, protocol, output device, standard ID,
vendor ID, vendor codec ID, and observation time. Cancellation restores that
snapshot before showing the localized cancellation message. UAC cancellation
and detection errors use the same restoration path, so a first canceled attempt
returns to the neutral card while a canceled retry preserves the last completed
result without stale or mismatched technical details.

The x64 Release project compiled with zero warnings and errors. The x64 and
ARM64 single-file applications and MSI packages were rebuilt with zero warnings
and errors; both installers passed administrative extraction with exit code 0
and exact SHA-256 payload matches. No codec ETW sessions remained after
validation. The user's open UI was left running, and no Computer Use, UI
automation, installation, or UAC prompt was used.
