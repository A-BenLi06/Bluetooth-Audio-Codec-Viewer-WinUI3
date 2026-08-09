# App icon generation record

Mode: built-in `image_gen` edit flow using the previous production icon as the
edit target. The generated flat magenta background was removed locally with the
imagegen skill's `remove_chroma_key.py` helper. Windows and Store sizes were
derived from the transparent 1254 x 1254 master with high-quality Lanczos
resampling.

## Final prompt

```text
Use case: logo-brand
Asset type: Windows desktop application icon, square 1:1 master
Input image: edit target; preserve its recognizable concept and overall composition.
Primary request: Redesign this Bluetooth Audio Codec icon into a restrained, genuinely flat modern app icon. Keep the central Bluetooth rune, symmetrical audio waveform bars on both sides, and the small verification check badge at the lower right. Remove every neon glow, bloom, lens effect, glass effect, bevel, embossing, glossy highlight, reflection, blur, texture, and 3D depth.
Style/medium: crisp 2D vector-like geometry, minimal Fluent-inspired but not glossy, designed by a professional human brand designer. Use a dark navy solid rounded-square tile, one vivid Windows-blue solid color for the Bluetooth and waveform motif, and a simple cyan/blue check badge with strong contrast. Flat color fields only; no gradients. Consistent stroke weight, clean optical spacing, balanced proportions, fewer details, legible at 16–32 px.
Composition: centered Bluetooth symbol, matching four-bar waveform groups left and right, check badge overlapping the lower-right area without crowding. Generous safe padding. Rounded-square silhouette must stay fully inside the canvas.
Background-removal requirement: outside the rounded-square icon tile, use a perfectly flat solid #FF00FF chroma-key background. The outside background must be uniform with no shadows, gradients, texture, or antialias spill. Do not use magenta anywhere in the icon itself.
Constraints: no text, no letters, no watermark; preserve Bluetooth + audio + verified meaning; no added objects; no glow of any kind; no cast shadow; no metallic or glass materials; no photorealism.
```
