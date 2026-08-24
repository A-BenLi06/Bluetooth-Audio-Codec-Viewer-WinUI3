# Microsoft Store MSIX package

This branch packages **Bluetooth Audio Codec Viewer** as a self-contained WinUI
3 MSIX bundle for x64 and ARM64. The normal UI runs without elevation. Selecting
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
.\build-store-msix.ps1 -Version 1.0.0.0
```

The script creates an unsigned `.msixupload` containing x64 and ARM64 package
slices. Upload the `.msixupload` file to Partner Center; Microsoft signs Store
packages during ingestion.

For a packaging-only test before entering the real identity:

```powershell
.\build-store-msix.ps1 -Version 1.0.0.0 -AllowPlaceholderIdentity
```

That artifact is deliberately not eligible for submission.

## Manifest capabilities

The package declares:

```xml
<rescap:Capability Name="runFullTrust" />
<rescap:Capability Name="allowElevation" />
```

`allowElevation` is restricted. After the package is uploaded, Partner Center
will require the detailed justification under **Submission options > Restricted
capabilities**. Approval is decided during certification.

## Local validation

The StoreUpload artifact is unsigned and is intended for Partner Center. For a
local install, build Debug after changing the manifest publisher to match a
local test certificate, then install the generated test certificate and MSIX.
Do not submit a locally signed test package to Partner Center.

Validate these behaviors on x64 and, before claiming ARM64 support, on an ARM64
device or VM:

1. Installation and normal launch do not prompt for UAC.
2. UAC appears only after **Detect codec** is selected.
3. Approving UAC returns the codec result to the normal-integrity UI.
4. Canceling detection stops the helper and its ETW session.
5. Declining UAC keeps the UI open and restores its prior state.
6. Uninstall leaves no service, driver, task, helper, or ETW session.
