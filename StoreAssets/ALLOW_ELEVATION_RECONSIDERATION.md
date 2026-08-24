# `allowElevation` reconsideration — Product ID 9P8G2CQW77JT

Use this material for the new submission following the certification report
completed August 19, 2026. The package version containing the additional helper
hardening is `1.0.2.0`.

## Partner Center justification (paste into the `allowElevation` field)

This is an expanded reconsideration request responding to the August 19, 2026
report for Product ID 9P8G2CQW77JT.

Bluetooth Audio Codec Viewer uses allowElevation only after the user selects
Detect codec. The signed packaged EXE is launched with ShellExecute `runas`, so
the standard Windows UAC consent prompt is always used; there is no silent
elevation or UAC bypass. The main UI always remains at standard integrity.

The elevated helper has one purpose: create, observe, stop, and dispose one
short-lived Microsoft.Windows.Bluetooth.BthA2dp ETW session required to read the
codec negotiated for the active A2DP stream. Normal Bluetooth/audio APIs do not
expose this negotiated codec.

The helper cannot run arbitrary commands, PowerShell, scripts, or user-supplied
executables. It accepts only an exact fixed argument schema: a current-user pipe
name, random 256-bit token, 1–60 second timeout, and optional fixed play-tone
flag. Unknown, extra, reordered, or duplicate arguments are rejected before ETW
starts. The current-user-only pipe serves one authenticated request; its only
control command is cancel.

No elevated process persists. On result, cancel, timeout, or error, the ETW
session is disposed, the pipe closes, and the helper exits. The app installs no
service, driver, scheduled task, startup entry, or background agent. It does not
modify Bluetooth settings, drivers, registry values, audio settings, or Windows
configuration. The Store description explicitly states that standard Windows
UAC administrator approval is required each time codec detection is started.

Support: ben_li06@outlook.com

## Shorter fallback (if the field limit is lower)

Expanded reconsideration for the Aug. 19, 2026 report, Product ID 9P8G2CQW77JT.
allowElevation is used only after Detect codec is clicked. ShellExecute `runas`
always shows standard Windows UAC; the UI remains at standard integrity. The
helper only creates/controls one short-lived Microsoft.Windows.Bluetooth.BthA2dp
ETW session. It cannot run commands, PowerShell, scripts, or user executables:
only an exact pipe/token/timeout/optional-tone argument schema is allowed; all
extra, unknown, reordered, or duplicate arguments are rejected. The only
authenticated IPC control action is cancel. On result, cancel, timeout, or
error, ETW is disposed and the helper exits. No service, driver, task, startup
entry, or background process is installed. No Bluetooth setting, driver,
registry value, audio setting, or Windows configuration is modified. The Store
description discloses UAC/admin approval on every detection. Support:
ben_li06@outlook.com

## 500-character Partner Center version

Reconsideration for Product ID 9P8G2CQW77JT. Windows UAC is always shown after
Detect codec is clicked; the UI stays unelevated. The temporary helper only
controls one Bluetooth A2DP ETW session and cannot run commands, PowerShell,
scripts, or user executables. It disposes ETW and exits after result, cancel,
timeout, or error; no elevated process persists. It changes no Bluetooth
settings, drivers, registry values, or Windows configuration. Admin approval is
disclosed in the Store description.

## Store description disclosure

Each time you select **Detect codec**, Windows displays the standard User
Account Control (UAC) consent prompt. Administrator approval is required only
for the short-lived helper that observes the Bluetooth codec ETW event; the main
app remains at standard user integrity. The helper exits as soon as detection
succeeds, is canceled, times out, or fails. The app does not change Bluetooth
settings, install a driver or service, or modify the registry or Windows
configuration.

## Certification test procedure

1. Install and launch the app. Confirm installation and launch show no UAC and
   the main UI runs at standard integrity.
2. Connect a Bluetooth Classic A2DP headset and begin playback.
3. Select **Detect codec** and confirm the standard Windows UAC prompt appears
   only after this action.
4. Approve UAC. Confirm a codec result is returned to the normal UI, then verify
   in Task Manager that the elevated helper exits immediately.
5. Start detection again and select **Cancel**. Confirm the helper and ETW
   session stop and the UI returns to Ready.
6. Start detection and decline UAC. Confirm the standard-integrity UI remains
   open and reports cancellation.
7. Confirm the app has installed no service, driver, scheduled task, startup
   entry, or persistent helper and has not changed Bluetooth settings, registry
   values, audio settings, or Windows configuration.

## Implementation evidence

- UAC launch: `Services/ElevatedCodecBridge.cs`, `StartElevatedHelper`, uses
  `UseShellExecute = true` and `Verb = "runas"`.
- Fixed helper command surface: `ParseHelperArguments` requires the exact
  pipe/token/timeout/optional-tone schema and rejects every other form.
- IPC isolation: `NamedPipeServerStream` uses `PipeOptions.CurrentUserOnly`, a
  random 256-bit token, constant-time token comparison, and one connection.
- ETW scope and cleanup: `Services/BluetoothCodecDetector.cs` creates one
  `TraceEventSession`, enables only `Microsoft.Windows.Bluetooth.BthA2dp`, sets
  `StopOnDispose = true`, and disposes the session on every exit path.
- Process lifetime: `Program.cs` routes helper mode directly to
  `RunElevatedHelperAsync` and returns its exit code without creating the UI or
  any resident background component.
