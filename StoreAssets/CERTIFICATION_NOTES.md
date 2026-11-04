# Certification notes

Bluetooth Audio Codec Viewer is a WinUI 3 desktop application distributed as an
MSIX package for x64 and ARM64. It uses native C++/WinRT, requires no .NET, and depends on the shared
Microsoft Windows App Runtime 2.2 or later, resolved by the Store at installation.
It does not need deployment of the Windows App Runtime Main/Singleton packages.

The main WinUI 3 process always runs at standard user integrity. When the user
explicitly selects **Detect codec**, the application starts the same signed EXE
with the Windows `runas` verb in a hidden `--elevated-helper` mode. Elevation is
required only because the `Microsoft.Windows.Bluetooth.BthA2dp` ETW provider is
restricted to elevated processes. The helper returns the result to the normal
UI through a random current-user-only named pipe authenticated by a random
256-bit token, then exits. Canceling detection stops and disposes the ETW
session.

The package declares `uap7:Capability Name="globalMediaControl"` to pause and
restore all supported currently playing system-media sessions during a
user-initiated Detect, including players on other outputs. Commands run in the
ordinary process after ETW readiness. Initially paused sessions are never started;
recovery covers only owned pauses and checks the original session, paused status
and subsequent playback/media changes. Routing and application identity are not
control gates. The helper accepts only authenticated begin-probe and cancel for
the current attempt, and cannot control other applications. No process/app IDs,
paths, titles, URLs or media fingerprints enter the structured report. Target
Core Audio activity is inspected only for local process-name hints. Transient
keyed metadata comparisons guard recovery; timeline refreshes alone do not block it.

In direct response to the certification report completed August 19, 2026, for
Product ID `9P8G2CQW77JT`, the publisher confirms all of the following:

1. The standard Windows UAC consent prompt is always used. The app invokes the
   signed packaged executable with `UseShellExecute=true` and `Verb="runas"`;
   it contains no UAC bypass or silent-elevation mechanism.
2. The elevated helper only creates, observes, stops, and disposes one
   short-lived ETW session for the
   `Microsoft.Windows.Bluetooth.BthA2dp` provider needed for codec detection.
3. The helper cannot execute arbitrary commands, PowerShell, scripts, or
   user-supplied executables. Its command line accepts only an exact fixed
   schema containing a pipe name, a 256-bit authentication token, a timeout of
   1–60 seconds, an optional validated detection UUID, and an optional fixed play-tone flag. Unknown, extra,
   reordered, or duplicate arguments are rejected before ETW starts. After the
   pipe is authenticated, only `begin-probe` and `cancel` are accepted, with the
   token and current attempt UUID checked on each message. Duplicate probe
   commands are rejected.
4. No persistent elevated process remains after detection. The helper serves a
   single request, disposes the ETW session on success, timeout, cancellation,
   or error, closes the pipe, and exits. No service, scheduled task, startup
   entry, or background agent is installed.
5. The app does not modify Bluetooth settings, drivers, registry values, audio
   settings, or any Windows configuration setting.
6. The Store description and Additional system requirements clearly disclose
   that Windows displays the standard UAC prompt and administrator approval is
   required each time the user starts codec detection.
7. At standard user integrity, the UI uses the `bluetooth` device capability
   only to observe the connection state and display name of paired Bluetooth
   audio devices while the window is open. It does not pair, connect,
   disconnect, or reconfigure devices.

The application:

- does not install a driver or Windows service;
- does not modify Bluetooth, audio, registry, or system settings;
- does not record or inspect audio content;
- uses connection state and display names for the UI and retains bounded local
  diagnostics with tagged device relationships, technical versions and errors;
- offers one problem-report ZIP preview/export, with optional filtered recent
  runtime logs and an application/startup context that requires no detection;
  it never uploads report files automatically;
- opens a public GitHub issue page only when the user chooses it and acknowledges
  the public attachment notice; the browser URL includes report/build IDs and
  a stable reason, and the user attaches files and submits the issue in the browser;
- does not contain advertising, telemetry, a downloader, or bundled software;
- includes optional Ko-fi and Afdian support links that open in the user's
  default browser; support unlocks no app feature or digital content, and the
  application never handles payment or financial information;
- plays one quiet 600 ms tone to ask Windows to create a fresh A2DP stream.

Suggested certification test:

1. Install the MSIX from Microsoft Store. Installation and launch must not
   display a UAC prompt.
2. Launch **Bluetooth Audio Codec Viewer** from the Start menu.
3. Connect a Bluetooth Classic A2DP headset and begin media playback.
4. Select **Detect codec**, approve the user-initiated UAC prompt, and verify the
   codec result appears in the normal-permission UI.
5. In Task Manager, verify the elevated helper exits immediately after the
   result is returned and only the standard-integrity UI remains.
6. Select **Report a problem**, choose a detection and inspect each file before
   saving the standard ZIP. Export must not show UAC or upload anything.
7. Select **Open GitHub** and verify the public/immediate-attachment-upload
   notice appears first. Canceling must not open a browser. Opening the browser
   must not be presented as a submitted/received report.
6. Start another detection and select **Cancel**; the UI returns to Ready and no
   elevated helper or ETW session remains.
7. Decline the UAC prompt; the main UI remains open at standard integrity and
   displays a cancellation message.
8. Uninstall **Bluetooth Audio Codec Viewer** from Installed apps and verify no
   helper, ETW session, service, driver, task, or configuration remains.
