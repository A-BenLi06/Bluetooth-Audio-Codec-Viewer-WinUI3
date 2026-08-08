# Microsoft Store assets

These files are prepared for the Microsoft Store MSI/EXE submission flow.

## Partner Center mapping

| Partner Center field | File | Status |
| --- | --- | --- |
| Store logo, required 1:1 box art | `Icon/StoreLogo-1080.png` | 1080 x 1080 PNG |
| App tile icon, if offered | `Icon/StoreLogo-300.png` | 300 x 300 PNG |
| Store logo, recommended 2:3 poster art | `Icon/StorePoster-720x1080.png` | 720 x 1080 PNG |
| Desktop screenshot 1 | `Screenshots/en-US/01-codec-detected.png` | 1366 x 768 PNG |
| Desktop screenshot 2 | `Screenshots/en-US/02-ready-to-detect.png` | 1366 x 768 PNG |

The Store requires at least one screenshot and one 1:1 Store logo for an
MSI/EXE listing. Four or more screenshots are recommended, but the two included
screenshots satisfy the submission minimum and show the primary ready and
detected states.

## Application and installer icons

- `Icon/AppIcon-master.png` is the transparent high-resolution master.
- `Icon/source-concept.png` preserves the original supplied design.
- `Icon/IMAGEGEN_PROMPT.md` records the final built-in imagegen prompt and
  transparency workflow.
- `../BluetoothAudioCodec.WinUI/Assets/AppIcon.ico` contains 16, 20, 24, 32,
  40, 48, 64, 96, 128, and 256 pixel icon entries.
- `../BluetoothAudioCodec.WinUI/Assets/AppIcon.png` is the 256 pixel PNG copy.

The `.ico` is embedded in the application executable, Start menu shortcut, and
the MSI Add or Remove Programs entry.

## Listing copy

Copy-ready text is available for:

- `Listing/en-US.md`
- `Listing/zh-CN.md`
- `Listing/zh-TW.md`

`CERTIFICATION_NOTES.md` is written for the Partner Center certification-notes
field. `PRIVACY_POLICY.md` can be published on the support website after its
contact placeholder is replaced.
