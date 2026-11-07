# Current approved Store artwork

Product: **Bluetooth Audio Codec Viewer**.

The user approved the information-card, inspection-lens and wireless-earbuds logo, then explicitly required a truly transparent background and retention of the existing Bluetooth function glyph inside the app. `../Icon/AppIcon-master.svg` is the clean, scalable production master; PNG and ICO derivatives have real alpha transparency. The pale-background raster is an archived approval rendering, not a production input.

## Assets

- Nine locales: en, zh-cn, zh-tw, ja, fr, de, it, pt-br, es.
- Four localized 1920×1080 gallery images per locale, using real application captures.
- Localized 1080×1080 box art, 720×1080 poster and 1920×1080 hero art per locale.
- Shared transparent 300×300, 150×150 and 71×71 Store icon overrides.
- English 30-second 1920×1080, 30 fps Fluent motion trailer, H.264/AAC MP4, thumbnail and WebVTT captions. Audio is a silent stereo track.

The trailer has continuously moving translucent ribbons, eased scene entrances, alpha fades and gently floating real UI. It shows the ready state and empty technical fields honestly; it does not fabricate successful detection results.

## Rebuild

Install Node dependencies in this directory with `npm install`, then run `npm run assets` and `npm run trailer`. FFmpeg must be available on PATH for the trailer. `source/` preserves the actual application captures. The renderer retains English UI inside localized marketing copy.

Uploads update the existing Store submission draft. Certification and public release are separate from uploading these assets.

The existing draft now contains all nine localized image sets, the new English motion trailer and the 1.1.1.0 x64/ARM64 package. Partner Center reports the replacement package as **Validated**. Certification was not submitted. `UPLOAD_STATUS.json` records the saved outcome.

## Verification

`ASSET_VERIFICATION.json` records dimensions of 67 marketing images and true alpha transparency in 61 native PNGs. `PACKAGE_VERIFICATION.json` checks that both x64 and ARM64 packages contain the current transparent icon and have version 1.1.1.0. `STORE_VERIFICATION.json` checks the final exported Store CSV: 90 image fields across nine locales, one English hero trailer, its thumbnail and captions. `verified-store-listing.png` shows the uploaded transparent icon overrides. Run `npm run verify` to check local image exports; use `verify-listings.py` after downloading a fresh Store CSV to check saved listing fields.
