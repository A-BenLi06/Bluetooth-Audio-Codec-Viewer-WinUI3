# Microsoft Store MSI/EXE distribution

This repository now builds a traditional unpackaged Win32 application. The UI
runs at normal integrity. Codec detection starts the same signed executable in a
hidden `--elevated-helper` mode and exchanges the result over a random,
current-user-only named pipe. The UI itself is never restarted or elevated.

## Build local test installers

```powershell
.\build-store-installer.ps1 -Architecture all -Version 1.0.0
```

This creates unsigned local-test installers at:

- `artifacts\installer\x64\BluetoothAudioCodec-1.0.0-x64.msi`
- `artifacts\installer\arm64\BluetoothAudioCodec-1.0.0-arm64.msi`

The MSI is offline, embeds its CAB payload, installs one application, creates
one Start menu shortcut, supports major upgrades, and supports the Store's
standard silent command:

```text
msiexec.exe /i BluetoothAudioCodec-1.0.0-x64.msi /qn /norestart
```

## Build Store-eligible signed installers

Install a code-signing certificate issued by a CA in the Microsoft Trusted Root
Program, then run:

```powershell
.\build-store-installer.ps1 `
    -Architecture all `
    -Version 1.0.0 `
    -Manufacturer "YOUR LEGAL PUBLISHER NAME" `
    -CertificateThumbprint YOUR_CERTIFICATE_THUMBPRINT `
    -RequireSigning
```

The script signs the self-contained application EXE before embedding it, builds
the MSI, signs the MSI, timestamps both signatures, and fails if either final
signature is not valid. A self-signed development certificate is not acceptable
for Microsoft Store submission.

## Partner Center package fields

- App type: `MSI`
- Architecture: submit the x64 and ARM64 installers separately
- Installer parameters: MSI uses the Store default `/qn`
- Package URL: a direct, immutable, versioned HTTPS URL, for example
  `https://downloads.example.com/bluetooth-audio-codec/1.0.0/BluetoothAudioCodec-1.0.0-x64.msi`
- Installer behavior: offline; no downloader or additional bundled products

Never replace the binary behind a submitted URL. For an update, increment the
three-part MSI version, build and sign new installers, upload them to new
versioned URLs, and update the Partner Center submission.
