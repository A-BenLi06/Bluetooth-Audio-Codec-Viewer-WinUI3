# App icon generation record

Mode: built-in `image_gen` edit flow with the supplied 1024 x 1024 concept as
the edit target and design reference. The generated chroma-key background was
removed locally with the imagegen skill's `remove_chroma_key.py` helper.

## Final prompt

```text
Use case: logo-brand
Asset type: production master icon for a modern Windows desktop application and Microsoft Store listing
Input image: edit target and design reference. Preserve the recognizable composition and meaning of the user's design: a large central Bluetooth rune, symmetrical audio-wave bars on both sides, and a circular check badge overlapping the lower-right of the rune.
Primary request: adapt this exact concept into a crisp, high-contrast app icon that remains recognizable at 16–32 px. Tighten the composition so the mark fills roughly 72–78% of the canvas, strengthen the silhouettes and edge contrast, and reduce the excessive empty gray space while retaining the luminous blue/cyan glass-like character.
Scene/backdrop: place the finished dark navy rounded-square icon tile on a perfectly flat solid #ff00ff chroma-key background for later transparency removal. Outside the rounded-square tile, the background must be one uniform #ff00ff color with no gradient, texture, shadow, reflection, glow, or lighting variation.
Style/medium: polished Windows 11 Fluent-inspired application icon; clean vector-like geometry with subtle dimensional glass and restrained bloom, not a photograph.
Composition: centered, front-facing, perfectly square, no tilt or perspective. The Bluetooth symbol is dominant; waveform bars are balanced and symmetrical; the check badge is distinct but secondary and does not obscure the Bluetooth identity. Generous consistent padding inside the tile.
Color palette: near-black/navy tile, saturated Azure blue and cyan highlights, small deep-blue accents. Do not use magenta anywhere in the icon tile.
Constraints: preserve the three core symbols and their relative arrangement; crisp strong forms at thumbnail size; no letters, no words, no extra symbols, no watermark, no mockup frame, no drop shadow outside the rounded tile. Keep all glow contained inside the tile. Output one 1024x1024 icon master.
```
