# Certification notes

Bluetooth Audio Codec Viewer is a self-contained WinUI 3 desktop application
distributed as an MSIX package for x64 and ARM64.

The main WinUI 3 process always runs at standard user integrity. When the user
explicitly selects **Detect codec**, the application starts the same signed EXE
with the Windows `runas` verb in a hidden `--elevated-helper` mode. Elevation is
required only because the `Microsoft.Windows.Bluetooth.BthA2dp` ETW provider is
restricted to elevated processes. The helper returns the result to the normal
UI through a random current-user-only named pipe authenticated by a random
256-bit token, then exits. Canceling detection stops and disposes the ETW
session.

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
   1–60 seconds, and an optional fixed play-tone flag. Unknown, extra,
   reordered, or duplicate arguments are rejected before ETW starts. After the
   pipe is authenticated, the only accepted control message is `cancel`.
4. No persistent elevated process remains after detection. The helper serves a
   single request, disposes the ETW session on success, timeout, cancellation,
   or error, closes the pipe, and exits. No service, scheduled task, startup
   entry, or background agent is installed.
5. The app does not modify Bluetooth settings, drivers, registry values, audio
   settings, or any Windows configuration setting.
6. The Store description and Additional system requirements clearly disclose
   that Windows displays the standard UAC prompt and administrator approval is
   required each time the user starts codec detection.

The application:

- does not install a driver or Windows service;
- does not modify Bluetooth, audio, registry, or system settings;
- does not record or inspect audio content;
- does not collect, store, or transmit personal or device information;
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
6. Start another detection and select **Cancel**; the UI returns to Ready and no
   elevated helper or ETW session remains.
7. Decline the UAC prompt; the main UI remains open at standard integrity and
   displays a cancellation message.
8. Uninstall **Bluetooth Audio Codec Viewer** from Installed apps and verify no
   helper, ETW session, service, driver, task, or configuration remains.
