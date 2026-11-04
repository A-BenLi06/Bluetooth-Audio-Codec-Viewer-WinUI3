# Microsoft Store MSIX package

This branch packages **Bluetooth Audio Codec Viewer** as a WinUI 3 MSIX bundle
for x64 and ARM64. The GUI uses native C++20 / C++/WinRT and does not require .NET.
Windows App SDK is provided by the
shared `Microsoft.WindowsAppRuntime.2` framework package (minimum 2.2.0.0),
which the Store resolves during installation. The normal UI runs without elevation. Selecting
**Detect codec** launches the same packaged executable in the fixed,
short-lived `--elevated-helper` mode.

## Required Partner Center identity

Before creating the upload package, open:

`Partner Center > Bluetooth Audio Codec Viewer > Product management > Product identity`

Copy these values exactly into `BluetoothAudioCodec.WinUI/Package.appxmanifest`:

- **Package/Identity/Name** -> `<Identity Name="...">`
- **Package/Identity/Publisher** -> `<Identity Publisher="...">`

Do not use the Store product ID `9P8G2CQW77JT` as the package identity name.
The build script rejects the checked-in placeholders for a production build.

## Build the Partner Center package

```powershell
.\build-store-msix.ps1 -Version 1.0.3.0
```

For an isolated build without overwriting the default package directory:

```powershell
.\build-store-msix.ps1 -Version 1.0.3.0 -OutputDirectory artifacts\size-reduction\shared-runtime\msix
```

The script verifies the generated Microsoft framework dependency and rejects
packages that accidentally embed the Windows App SDK native runtime. It also
rejects .NET runtime/managed projection/TraceEvent files and verifies both architecture symbol packages.
The application only uses Framework APIs, so Deployment Manager startup is
disabled; Main/Singleton package deployment is not required by either the UI
or the codec helper. The offline MSI carries the native WinUI runtime files.

A device without the shared Windows App Runtime must download it on first
installation. A smaller application package does not imply a smaller total
first download; see `APP_SIZE.md` for measurements and the tradeoff.

The script creates an unsigned `.msixupload` containing x64 and ARM64 package
slices plus a non-empty `.appxsym` symbols package. The build machine must have
the Windows SDK Debugging Tools (`mspdbcmf.exe`) installed; the script rejects
an upload that cannot be symbolicated in Partner Center. Upload the
`.msixupload` file to Partner Center; Microsoft signs Store packages during
ingestion.

For a packaging-only test before entering the real identity:

```powershell
.\build-store-msix.ps1 -Version 1.0.3.0 -AllowPlaceholderIdentity
```

That artifact is deliberately not eligible for submission.

## Manifest capabilities

The package declares:

```xml
<rescap:Capability Name="runFullTrust" />
<rescap:Capability Name="allowElevation" />
<DeviceCapability Name="bluetooth" />
```

`allowElevation` is restricted. After the package is uploaded, Partner Center
will require the detailed justification under **Submission options > Restricted
capabilities**. Approval is decided during certification. The `bluetooth`
device capability is used by the standard-integrity UI only to watch the
connection state and display name of paired Bluetooth audio devices; it does
not pair, connect, disconnect, or reconfigure devices.

## Local validation

The Store build embeds a unique diagnostic build ID and explicitly declares its
intended Store distribution; package identity alone is not proof of installation
source. Retain matching EXE/PDB and upload symbols privately. The report ZIP is
user-submitted through GitHub; Partner Center Health is a separate source of
crash/hang evidence. At publication, verify the support URL points to this
repository's support/issue entry and publish the updated privacy policy. See
`diagnostics/README.md` for intake and report tests.

The StoreUpload artifact is unsigned and is intended for Partner Center. For a
local install, build Debug after changing the manifest publisher to match a
local test certificate, then install the generated test certificate and MSIX.
Do not submit a locally signed test package to Partner Center.

Use a separate package identity for local smoke tests. Launch a loose developer
package through the Start menu or AppModel activation so Windows supplies its
package dependency graph; direct execution from an unpacked test directory is
not equivalent to launching an installed MSIX.

Validate these behaviors on x64 and, before claiming ARM64 support, on an ARM64
device or VM:

1. Installation and normal launch do not prompt for UAC.
2. UAC appears only after **Detect codec** is selected.
3. Approving UAC returns the codec result to the normal-integrity UI.
4. Canceling detection stops the helper and its ETW session.
5. Declining UAC keeps the UI open and restores its prior state.
6. Uninstall leaves no service, driver, task, helper, or ETW session.
