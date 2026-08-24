# Microsoft Store `allowElevation` pre-approval request

## Send to

- Recipient: `reportapp@microsoft.com`
- Subject: `Pre-approval request for allowElevation — Bluetooth Audio Codec by BenLi06`
- From: `ben_li06@outlook.com`

Before sending, replace only the bracketed Partner Center fields below. If a
product has not yet been created, use `Not yet assigned` for its product ID.

## Email body

Hello Microsoft Store certification team,

I am requesting pre-approval to use the restricted `allowElevation` capability
in an MSIX version of **Bluetooth Audio Codec**, a focused Windows desktop
diagnostic utility published by **BenLi06**.

Publisher details:

- Publisher display name: BenLi06
- Partner Center account type: [Individual or Company]
- Partner Center product ID: [Product ID or Not yet assigned]
- Support email: ben_li06@outlook.com
- Product website: https://bt-codec-viewer-winui.benli06.site
- Source repository: https://github.com/A-BenLi06/Bluetooth-Audio-Codec-Viewer-WinUI3

### Product purpose

The app reports the Bluetooth Classic A2DP codec currently negotiated between
Windows and the user's active Bluetooth audio device. It observes the local
`Microsoft.Windows.Bluetooth.BthA2dp` ETW provider and displays the negotiated
codec identifiers and the friendly name of the current default audio output.
All processing is local.

### Why elevation is necessary

Windows permits elevated administrators, designated performance-log users, and
specified service accounts to create and control ETW trace sessions. This
consumer utility cannot assume that an end user belongs to the Performance Log
Users group, and it does not modify local group membership. The app therefore
requests UAC approval only when the user explicitly starts codec detection.

The main WinUI 3 process starts and remains at standard user integrity. After
the user selects **Detect codec**, it starts the same signed packaged desktop
executable with the Windows Shell `runas` verb and a fixed
`--elevated-helper` mode. The elevated mode creates one short-lived real-time
ETW session, reads one A2DP codec event or waits until the timeout, returns the
result to the normal process, disposes the ETW session, and exits.

Elevation is never requested at application startup. There is no automatic,
scheduled, or background elevation.

### Privileged-operation boundary

The elevated helper can only:

1. create and stop the app's uniquely named real-time ETW session;
2. enable the Bluetooth A2DP ETW provider for that session;
3. optionally play one locally generated, quiet 600 ms diagnostic tone to
   cause Windows to establish a fresh A2DP stream;
4. parse the codec identifiers from the resulting event;
5. return the result or an error to the invoking UI; and
6. accept a single fixed cancellation command.

It cannot receive or execute arbitrary commands, scripts, executable paths,
registry operations, file operations, or configuration changes.

### IPC and process safeguards

- A new unpredictable named pipe is created for every detection request.
- The pipe is limited to one connection and the invoking Windows user.
- Each request uses a cryptographically random 256-bit authentication token.
- Response tokens are compared in constant time.
- Pipe connection and detection operations have bounded timeouts.
- Cancellation disposes the ETW session and terminates the helper path.
- The helper is not installed or registered as a service and does not remain
  running after a result, cancellation, timeout, or error.

### The app does not

- install or depend on a driver, kernel component, or Windows/NT service;
- run as LocalSystem, LocalService, or NetworkService;
- modify Bluetooth, audio, registry, security, group-membership, firewall, or
  other Windows settings;
- record, inspect, save, or transmit audio content;
- write an ETL trace file to disk;
- collect telemetry, analytics, advertising identifiers, or personal data;
- download or execute code; or
- install bundled or secondary software.

The MSIX manifest would declare only the full-trust desktop permission required
for the WinUI 3 desktop process and the elevation capability required for this
user-initiated helper:

```xml
<Capabilities>
  <rescap:Capability Name="runFullTrust" />
  <rescap:Capability Name="allowElevation" />
</Capabilities>
```

No packaged-service, LocalSystem-service, driver, broad file-system, Bluetooth
device, microphone, or background-execution capability is requested.

### Suggested certification test

1. Install and launch the MSIX normally. The application opens without a UAC
   prompt and the UI runs at standard integrity.
2. Connect a Bluetooth Classic A2DP headset or speaker and make it the default
   audio output.
3. Select **Detect codec**. Verify that UAC appears only after this explicit
   action.
4. Approve UAC. Verify that the codec result is returned to the normal UI and
   the elevated helper exits.
5. Start detection again and select **Cancel**. Verify that the helper and its
   uniquely named ETW session are removed.
6. Decline UAC. Verify that the application remains open at standard integrity
   and clearly reports that elevation was canceled.
7. Uninstall the package. Verify that no service, driver, scheduled task,
   registry configuration, trace file, or helper process remains.

Could you please confirm whether this narrowly scoped, user-initiated use of
`allowElevation` is eligible for Microsoft Store approval before I complete the
MSIX conversion? I can provide a test package, screenshots, a demonstration
video, or further implementation details if required.

Thank you,

BenLi06  
ben_li06@outlook.com  
https://bt-codec-viewer-winui.benli06.site

## Optional attachments or follow-up material

Do not attach binaries to the initial email unless Microsoft asks for them.
Keep the following ready for a follow-up:

- a short screen recording showing launch without UAC, user-triggered UAC,
  successful detection, helper exit, cancellation, and UAC cancellation;
- an x64 test package and exact installation instructions;
- a process trace or Task Manager recording demonstrating that the helper is
  short-lived;
- the privacy policy URL;
- the relevant source files:
  - `BluetoothAudioCodec.WinUI/Services/ElevatedCodecBridge.cs`
  - `BluetoothAudioCodec.WinUI/Services/BluetoothCodecDetector.cs`
  - `BluetoothAudioCodec.WinUI/app.manifest`
- the Partner Center product identity and submission ID, once assigned.

## Response classification

Treat Microsoft's response as follows:

- **Approved:** the response explicitly authorizes `allowElevation` for this
  product or instructs you to proceed and cite the correspondence in the Store
  submission.
- **Conditional:** Microsoft asks for a separate helper, narrower operations,
  test package, company account, additional security controls, or updated
  certification notes. Satisfy those conditions before beginning final MSIX
  submission work.
- **Needs clarification:** Microsoft asks general questions without confirming
  eligibility. Reply with the requested evidence; do not assume approval.
- **Not approved/no viable approval path:** retain the signed MSI/EXE Store
  submission route and do not declare `allowElevation` in an MSIX submission.

