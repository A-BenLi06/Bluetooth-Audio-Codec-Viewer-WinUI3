# Microsoft Store MSI submission runbook

This project uses the Microsoft Store's traditional unpackaged Win32 MSI/EXE
submission path. The MSI is offline and per-machine; the normal WinUI process
runs without elevation, and only the short-lived codec helper requests UAC.

Official references:

- [Create an MSI/EXE submission](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msi/create-app-submission)
- [MSI/EXE package requirements](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msi/app-package-requirements)
- [Upload MSI/EXE packages](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msi/upload-app-packages)
- [Manual package validation](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msi/manual-package-validation)
- [MSI/EXE screenshots and images](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msi/screenshots-and-images)

## Current readiness

Completed in the repository:

- x64 and ARM64 self-contained single-file applications;
- offline WiX MSI with embedded payload, major upgrades, Start menu shortcut,
  and a single Add or Remove Programs entry;
- Store-compatible silent install using the MSI default `/qn` switch;
- application, shortcut, and Add or Remove Programs icon integration;
- required 1:1 Store logo, recommended 2:3 poster, 300 px tile icon, and two
  1366 x 768 desktop screenshots in `StoreAssets`;
- copy-ready en-US, zh-CN, and zh-TW listing text;
- certification notes and a privacy-policy draft.

Release blockers that require publisher-owned information or credentials:

- a trusted-CA code-signing certificate with its private key;
- the legal publisher name that should appear in the MSI;
- a reserved Partner Center product name;
- public HTTPS package URLs that will never be overwritten;
- a working support contact and, if used, public privacy-policy URL.

The currently installed `CN=BluetoothAudioCodec` certificate is self-signed and
is for local development only. It must not be used for the Store release.

## 1. Choose the release identity

Before building, decide and keep these values stable:

- Product name: `Bluetooth Audio Codec`
- MSI version: three-part version such as `1.0.0`
- Manufacturer: the publisher's legal name, matching the public publisher
  identity and not a placeholder
- Architectures: `x64` and `arm64`
- UpgradeCode: `DFA46F66-61B3-46D0-B4B0-0A326CDB46AA` (do not change)

For every update, increment the MSI version and use new package URLs. Never
replace an installer behind a URL already submitted to Partner Center.

## 2. Build and sign the final packages

Install the trusted-CA code-signing certificate in the Windows certificate
store, then run:

```powershell
.\build-store-installer.ps1 `
    -Architecture all `
    -Version 1.0.0 `
    -Manufacturer "YOUR LEGAL PUBLISHER NAME" `
    -CertificateThumbprint YOUR_CERTIFICATE_THUMBPRINT `
    -RequireSigning
```

The script signs and timestamps the self-contained EXE before it is embedded,
builds the MSI, signs and timestamps the MSI, and fails if either final
signature is invalid.

Expected outputs:

- `artifacts\installer\x64\BluetoothAudioCodec-1.0.0-x64.msi`
- `artifacts\installer\arm64\BluetoothAudioCodec-1.0.0-arm64.msi`
- `artifacts\installer\BluetoothAudioCodec-1.0.0-release.json` with SHA-256
  hashes, signing state, and silent-install commands

Unsigned packages created without `-RequireSigning` are local-test artifacts
only and are not eligible for Store submission.

## 3. Validate the signed release on clean Windows systems

Test at least Windows 10 build 19041 x64 and Windows 11 x64. Test ARM64 on an
ARM64 device or VM before submitting the ARM64 package.

For each architecture:

1. Verify both Authenticode signatures report `Valid`:

   ```powershell
   Get-AuthenticodeSignature .\BluetoothAudioCodec.WinUI.exe
   Get-AuthenticodeSignature .\BluetoothAudioCodec-1.0.0-x64.msi
   ```

2. Install silently. UAC is allowed, but the installer must show no other UI:

   ```powershell
   msiexec.exe /i .\BluetoothAudioCodec-1.0.0-x64.msi /qn /norestart
   ```

3. Verify there is exactly one Installed apps entry with the correct product
   name, publisher, version, and icon.
4. Launch from the Start menu and verify ready, detected, canceled, UAC-canceled,
   timeout, and no-headset states.
5. Confirm detection changes no Bluetooth or audio setting and leaves no helper
   process or `BluetoothAudioCodec-WinUI-*` ETW session.
6. Uninstall silently and verify the application files and shortcut are removed:

   ```powershell
   msiexec.exe /x .\BluetoothAudioCodec-1.0.0-x64.msi /qn /norestart
   ```

## 4. Host immutable installer URLs

Upload each signed MSI to a reliable public HTTPS origin or CDN. Use a path that
contains the version and architecture, for example:

```text
https://downloads.example.com/bluetooth-audio-codec/1.0.0/BluetoothAudioCodec-1.0.0-x64.msi
https://downloads.example.com/bluetooth-audio-codec/1.0.0/BluetoothAudioCodec-1.0.0-arm64.msi
```

The URLs must:

- be direct HTTPS downloads with no login, cookie, or interactive page;
- remain globally available and performant in every selected market;
- return the exact signed binary tested locally;
- remain immutable after submission;
- continue working while the release is available in the Store.

Record SHA-256 hashes before upload, download each URL again, and verify the
downloaded hashes match before entering the URLs in Partner Center.

## 5. Create the Partner Center submission

### Availability

- Markets: select the markets where the package URLs are reliably available.
- Discoverability: `Available in Microsoft Store` for a public release.
- Pricing: choose `Free`, `Paid`, `Freemium`, or `Subscription`. `Free` is the
  simplest choice if the application has no commerce implementation.

### Properties

- Recommended category: `Utilities & tools`.
- Answer the personal-information question according to the final binary. The
  current implementation processes the audio endpoint name and codec IDs
  locally and sends no data off-device.
- Driver or NT service declaration: `No`.
- Pen and ink: `No`.
- Privacy policy: optional if Partner Center does not require it after the data
  declaration, but publishing the prepared policy is recommended.
- Provide a working website/support contact. Business accounts must also provide
  the required contact details.
- Paste `StoreAssets/CERTIFICATION_NOTES.md` into Notes for certification.

### Age ratings

Complete every required IARC question. This utility contains no violence,
sexual content, gambling, controlled substances, user-generated content, or
online communication; answer based on the final product rather than choosing a
rating manually.

### Packages

Add both packages separately:

| Field | x64 package | ARM64 package |
| --- | --- | --- |
| App type | MSI | MSI |
| Architecture | x64 | arm64 |
| Package URL | versioned x64 HTTPS URL | versioned ARM64 HTTPS URL |
| Installer parameter | Store default `/qn` | Store default `/qn` |

Select the languages actually supported by the application: en-US, zh-CN,
zh-TW, ja, fr, es, pt-BR, de, and it.

### Store listings

At least one listing language is required. Description, applicable license
terms, a 1:1 Store logo, and one screenshot are required for an MSI/EXE listing;
four or more screenshots are recommended. Use:

- `StoreAssets/Listing/en-US.md`
- `StoreAssets/Listing/zh-CN.md`
- `StoreAssets/Listing/zh-TW.md`
- `StoreAssets/Icon/StoreLogo-1080.png` as required 1:1 box art
- `StoreAssets/Icon/StorePoster-720x1080.png` as recommended 2:3 poster art
- the files under `StoreAssets/Screenshots/en-US`

For a first submission, leave **What's new in this version** blank. When adding
multiple listing languages, Partner Center requires the images to be associated
with each language even when the same image is reused.

## 6. Final pre-submit check

- Reserved Store name matches the MSI ProductName.
- Legal publisher name is not `BenLi06` unless that is the verified publisher
  identity intended for the release.
- EXE and MSI signatures are valid and chain to a trusted public CA.
- x64 and ARM64 URLs download the correct, immutable packages.
- `/qn` install and uninstall pass on clean systems.
- One Installed apps entry appears with correct name, publisher, version, icon.
- App launches without elevation; UAC appears only after **Detect codec**.
- Cancel and UAC cancel restore the UI and leave no helper or ETW session.
- Store logo and at least one 1366 x 768 screenshot are uploaded.
- Description and applicable license terms are filled for each listing language.
- Support contact works and privacy-policy placeholder has been replaced.
- Certification notes explain the user-initiated elevated ETW helper.

Only after every item above is true should the submission be sent for
certification.

## 7. Updating after publication

1. Increment the three-part MSI version.
2. Build and sign new x64 and ARM64 packages.
3. Validate clean install, upgrade, uninstall, signatures, and hashes.
4. Upload to new versioned HTTPS URLs.
5. Create an Update submission and enter the new URLs.
6. Add release notes under **What's new in this version**.
7. Never alter or delete binaries behind URLs used by an existing submission.
