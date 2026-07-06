# Walkthrough

## 2026-07-06 13:45:43 +08:00

Implemented `BluetoothAudioCodec.cs` as a .NET 10 file-based application, so the
utility remains a single source file while still declaring its TraceEvent
dependency in-file.

The program subscribes to the Windows Bluetooth A2DP ETW provider and reads the
codec IDs from the actual `A2dpStreaming` negotiation event. It resolves standard
codecs and known vendor codec tuples, including SBC, AAC, aptX Classic, aptX HD,
aptX Low Latency, LDAC, and LHDC. Unknown values are returned with their raw IDs
instead of being guessed.

The implementation also:

- prints the current default render endpoint;
- plays an optional quiet diagnostic tone to create an audio stream;
- supports bounded capture, continuous watch, no-tone, and JSON modes;
- explains the distinction between A2DP, HFP, and Bluetooth LE Audio when no
  A2DP event is observed;
- validates arguments and ETW elevation requirements without changing Bluetooth
  or audio configuration.

Design rationale: ETW exposes the negotiated codec, while device capability lists
only show what a headset could support. Reading fields by event name first and
using the documented event layout only as a compatibility fallback makes schema
handling more robust.

## 2026-07-06 13:48:29 +08:00

Adjusted Core Audio COM activation after the first compile check. The default
endpoint enumerator is now instantiated through its registered CLSID and cast to
the documented interface, which avoids an invalid compile-time coclass conversion.
The Windows-only annotation also makes the platform contract explicit to static
analysis.

## 2026-07-06 13:49:05 +08:00

Narrowed the Windows platform annotation to the COM entry method. This preserves
platform analysis for the native Core Audio call without incorrectly marking the
plain endpoint result model and its `FriendlyName` property as Windows-only.

## 2026-07-06 13:49:39 +08:00

Marked the private COM release helper as Windows-only as well. This removes the
last platform analyzer warning while keeping the native-call boundary precise.

## 2026-07-06 13:51:39 +08:00

Verification completed:

- `dotnet run --file .\BluetoothAudioCodec.cs -- --help` compiled and ran
  without warnings;
- invalid timeout input returned exit code 2 with a specific validation error;
- a non-elevated capture attempt returned exit code 2 with the required
  administrator guidance;
- framework-dependent single-file publishing succeeded for `win-x64`, and the
  published executable's help path returned exit code 0;
- `git diff --check` reported no whitespace errors.

The live ETW callback cannot be exercised from the current non-elevated session;
the program checks this prerequisite before creating a trace session.
