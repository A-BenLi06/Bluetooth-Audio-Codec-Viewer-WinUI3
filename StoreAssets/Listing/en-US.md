# Store listing — en-US

## Product name

Bluetooth Audio Codec

## Short description

See the Bluetooth Classic A2DP codec Windows negotiated with your headphones,
including SBC, AAC, aptX variants, LDAC, and selected vendor codecs.

## Description

Bluetooth Audio Codec is a focused Windows utility that shows the active
Bluetooth Classic A2DP codec negotiated between Windows and your headphones or
speakers.

Start a detection from the modern desktop interface and the app listens for the
Bluetooth A2DP streaming event exposed by Windows. When an event is observed,
the app displays the codec name, output device, standard codec ID, vendor ID,
vendor codec ID, and observation time.

Recognized codecs include SBC, AAC, aptX Classic, aptX HD, aptX Low Latency,
LDAC, Samsung Scalable Codec, LHDC, and other known vendor codecs. Unknown
values remain visible with their numeric identifiers for troubleshooting.

Detection runs locally. The app does not record audio, change Bluetooth
settings, install a driver or service, modify the registry or Windows
configuration, or transmit device information. Each time you select **Detect
codec**, Windows displays the standard User Account Control (UAC) consent
prompt. Administrator approval is required only for the short-lived helper that
observes the Bluetooth codec ETW event; the main app remains at standard user
integrity. The helper exits as soon as detection succeeds, is canceled, times
out, or fails. A short quiet tone helps Windows open a fresh A2DP stream.

This utility inspects Bluetooth Classic A2DP playback. Hands-free calls (HFP)
and Bluetooth LE Audio use different codec paths and are outside its scope.

Optional Ko-fi and Afdian links open in your default browser. Supporting
development is voluntary and does not unlock app features or digital content.

## Product features

1. Detects the active Bluetooth Classic A2DP codec
2. Recognizes SBC, AAC, aptX variants, LDAC, and selected vendor codecs
3. Shows codec IDs, vendor IDs, output device, and observation time
4. Runs locally without changing Bluetooth settings
5. Uses standard Windows UAC only when detection starts
6. Uses a focused, modern Windows interface

## Additional system requirements

1. Windows 10 version 2004 (build 19041) or later
2. x64 or ARM64 processor
3. Bluetooth audio device using Bluetooth Classic A2DP
4. Administrator approval through the standard Windows UAC prompt each time
   codec detection is started

## Search terms

1. Bluetooth codec
2. A2DP
3. aptX
4. LDAC
5. SBC
6. headphones
7. audio diagnostics

## Screenshot captions

1. View the active codec and output device negotiated by Windows.
2. Start a fresh local A2DP trace from the focused main screen.

## What's new in this version

Leave blank for the first submission.

## Applicable license terms

MIT License. Copyright 2026 BenLi06. See the project's LICENSE file for the
complete terms.

## Copyright and trademark info

Copyright 2026 BenLi06. Bluetooth trademarks belong to Bluetooth SIG, Inc.
Other product names and trademarks belong to their respective owners.

## Developed by

BenLi06
