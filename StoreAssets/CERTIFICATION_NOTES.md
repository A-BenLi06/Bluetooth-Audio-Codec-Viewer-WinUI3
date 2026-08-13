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
5. Start another detection and select **Cancel**; the UI returns to Ready and no
   elevated helper or ETW session remains.
6. Uninstall **Bluetooth Audio Codec Viewer** from Installed apps and verify no
   helper, ETW session, service, driver, task, or configuration remains.
