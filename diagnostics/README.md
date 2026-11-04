# Diagnostic report pipeline

The native app records each detection, including a blocked preflight, in a bounded local store. **Report a problem** is the single UI entry: choose a detection or application/startup context, optionally include recent runtime logs, preview every file and save one ZIP. The runtime-log option starts unchecked for a detection and checked for application/startup context, where the user can uncheck it. No detection is required for that context. There is no separate legacy TXT button. After saving, **Show file**, **Copy summary** and **Open GitHub** are available; the app does not upload or submit an issue.

The browser URL includes only the report ID, build ID and stable reason, plus an editable issue prompt. It uses the generic new-issue page so it works before the repository's Issue Form has been published. After templates are live-tested, a future release can switch to field-specific prefills. A user without an account or network access can keep the ZIP locally.

## Files and association

Format `bac-report/1.1`: seven core files (manifest.json, summary.txt, environment.json, attempts.json, events.jsonl, snapshots.json, collection-status.json), plus optional runtime-log.txt. JSON uses UTF-8 and UTC; JSONL uses one event per line. The ZIP uses stored entries, CRC-32, fixed allowlisted filenames, per-file SHA-256 and atomic destination replacement. The receiver also accepts the older seven-file 1.0 format.

Detect uses the global system-media scope at the user's request. After ETW readiness, the ordinary process enumerates GSMTC sessions and requests pause for every currently playing session that supports it, including sessions on other outputs and multiple sessions from one application. It does not require application identity, target-endpoint matching or a unique per-application controller. Each real session object is handled independently. Initially paused/stopped sessions are skipped and never started. The five-second pause budget, 128-session bound and cancellation checks remain; unsupported, failed and timed-out requests are explicit rather than silently treated as success.

On success, failure, cancellation and normal close, recovery is attempted only for requests that actually paused a session. It checks original-session presence, current Paused state, subsequent playback-state changes and confirmed changes to known media-identity fields. Output-device changes no longer independently suppress recovery because control scope is global. Late pause completions still enter cleanup; pending cleanup blocks another Detect and normal close waits. Force termination cannot guarantee recovery.

Timeline position/range updates and metadata-refresh notifications alone never block recovery. Bounded metadata reads use per-attempt randomly keyed HMAC-SHA256 comparisons in memory; missing/cleared/backfilled fields are not proof of a media change. Raw metadata, digests and keys are not saved/exported; the key is cleared at state destruction. All user actions, including same-item seeking, cannot be reliably detected.

The additive playback.control event records only control_action, a fixed reason, attempt-local control_session and nullable numeric control_error. Intake-reference-1.9 accepts the global-scope and audio-activity reasons while preserving older matching reasons for historical reports. all-system-media-sessions declares scope, system-media-selected identifies a selected session, and not-playing records exclusions. audio-streams-active / audio-streams-idle / audio-inventory-incomplete describe a separate, bounded post-pause observation of target Core Audio sessions. No app ID, root/process ID, process path, window title, song title, URL or fingerprint is exported. Events are checkpointed after pause and at finalization. cleanup-pending is the state at finalization; later completion writes only a generic runtime-log entry.

Target Core Audio inspection does not decide who receives media commands. Its locally displayed executable basenames can identify remaining activity such as Chrome.exe or GameViewer.exe; shared sessions may expose only a representative process. Paused media can still leave active streams, and API sources do not cover every audio graph. The app neither terminates applications nor mutes or disconnects devices to force a codec event.

--inspect-playback OUTPUT.json is read-only: it exports the global control scope, media/playing/controllable counts and target-audio activity, without identifiers or commands. --exercise-playback-recovery OUTPUT.json actually pauses all supported playing system-media sessions, plays the normal diagnostic tone, waits 10 seconds and requests recovery. It does not capture ETW or prove a complete Detect succeeds.

The 1.1 manifest declares `scope=attempt|session` and `runtime_log` with status, collection window, truncation and a typed error if collection failed. A session report contains app/system environment read at preparation time, empty attempts/snapshots/events, no fabricated attempt ID or terminal, and no ETW claim. It does not enter the saved detection store. An optional log read failure leaves the other report files exportable and marks the bundle partial.

`runtime-log.txt` reuses the legacy log collector and filtering without writing a temporary TXT. It contains recent history from the rotated logs and environment/default-output state read **at export preparation**, which is explicitly distinct from saved failure-time snapshots. It is previewed before saving, hashed in the manifest and bounded to 512 KiB; if needed, the middle is omitted on UTF-8 boundaries while retaining the beginning and newest tail. The saved preview is the exact content later written to the ZIP. Text filtering is not a guarantee of anonymity; review the optional log manually. Legacy text tags use their own process salts and are not guaranteed to match structured report tags.

Each export gets a fresh report_id; each detection has an attempt_id and a UI run_id. The authenticated helper pipe carries the attempt_id, typed errors and a bounded capture summary. UI receipt/phase events have role=ui and their actual receive time, not a fabricated helper thread/time. ETW raw events are not included. Device IDs are tagged before structured persistence. Runtime logs do not populate or overwrite structured detection fields; the optional text supplement is collected separately.

The start and end/failure snapshots include the target endpoint, bounded PnP ancestry, container-related A2DP/remote nodes, the audio topology path and available target driver versions. Snapshot windows are explicit; these API reads are not atomic. Microphone state is null when it cannot be confirmed. Default-output changes are visible as distinct tagged outputs in the snapshots; the report is not recomputed from the later export state.

Connection observations explicitly identify their scope: a confirmed target match, or the same Audio/Video-class connected-device set used by the connection badge. A nonzero aggregate count does not prove the default output belongs to a connected headset. This preserves the evidence behind “Connected but not-bluetooth” without asserting an unresolved identity.

`snapshots.json.snapshots[].device_profiles` adds a bounded product profile for each connected **Classic Audio/Video-class** device and the resolved default Bluetooth target, with exact-address matching before report-local tagging. The profile's `device_tag` shares the remote node tag in the graph when present; `container_tag` shares the container tag. `default_output_match` is true/false only when the output identity is resolved, otherwise null. Start and end/failure snapshots keep their own matches, so changing the default output does not reassign the original target. The list is not a full inventory of LE devices; absence from it does not prove a LE device is disconnected.

Model/manufacturer fields contain `value`, `source`, `status`, and `user_confirmed=false`. Product metadata comes from cached Windows `DeviceContainer` properties (`System.Devices.ModelName` / `System.Devices.Manufacturer`), with `DEVPKEY_Device_Manufacturer` on the exact remote devnode as a manufacturer-only fallback. A PnP manufacturer may be a generic Windows vendor, not the headset brand. Friendly names, bus descriptions, driver providers, MAC/OUI guesses, serials and raw hardware IDs are not model sources. Empty values stay null/unavailable; fields containing common address/path/email patterns, control characters or more than 120 characters are omitted with redacted status. Filtering does not certify arbitrary vendor metadata as anonymous: preview the product fields before sharing.

Metadata reads use no inquiry, pairing, SDP/GATT request or connection. Cached container queries have a 300 ms per-device wait and 1.2 s total wait budget per snapshot; missing/ambiguous containers remain unknown. At most eight profiles are retained (the resolved target first); omitted counts, timeout/failure and partial status are explicit. Manufacturer/model are not guaranteed on every headset; firmware/battery are not collected. Driver/environment versions stay in `environment.json`, and observed codec identifiers remain separate from product metadata. Manually confirmed product identity can be supplied in the GitHub form; the app does not silently mark Windows metadata as user-confirmed.

Device profiles were an additive extension to the pre-release 1.0 format and remain present in 1.1. Older saved attempts and the initial synthetic fixture remain valid. Exporting an old detection never queries today's devices to fill missing historical profiles. Use the updated receiver for 1.1 scope/log metadata and cross-file checks.

`collection-status.json.capture` adds nullable event counters, provider count and stop status. This is an optional extension to the initial pre-release schema; the application always emits it. Unknown loss counters remain null. “interrupted_unknown” means no normal terminal record was saved; it is not proof of a crash. Schema validation proves structure, not authenticity, privacy or root cause.

## Storage, limits and versions

Local store: `%LOCALAPPDATA%\BluetoothAudioCodecViewer\Reports`. Retention keeps up to five failed/interrupted or playback-recovery-failed attempts plus the latest ordinary success/cancellation, no longer than seven days, with a 10 MiB disk budget. Files saved to a user-selected location are not deleted. Pruning runs when reports are written/read, not by a background service. A failed internal write is counted; in-memory state can still be exported during that run. Old on-disk files after an unsuccessful final write may appear as interrupted on restart.

Per-record limit: 2 MiB. Standard ZIP limit: 5 MiB; generated file budget: 2 MiB each, total payload at most 4 MiB. Current collection has hard bounds (8 snapshots, 32 nodes per snapshot, 64 edges, 32 driver entries, fixed phase events), comfortably below those limits. Export failure preserves the previously selected destination. This version rejects an unexpectedly oversized bundle rather than silently truncating it or fabricating a replacement report.

The build writes a unique build ID into DiagnosticBuild.h, with commit, dirty state, architecture, packaging and explicit distribution metadata. MSIX identity does not prove Store installation. The Store MSIX build declares microsoft-store. For an MSI use `build-store-installer.ps1 -Distribution microsoft-store` or `-Distribution github-release`; default is unknown. A copied binary retains its build-declared source, which is not an attestation of how it was installed. Match EXE SHA-256 and PDBs in the maintainer's private release archive.

## Maintainer intake

Save the public standard ZIP in an isolated folder, then:

```powershell
python -m pip install -r diagnostics/requirements.txt
python diagnostics/tools/validate_bundle.py PATH_TO_REPORT.zip
```

The validator is an offline intake reference tool: it reads bounded entries in memory, checks inventory/CRC/hash/Schema/IDs/terminal records, report scope and optional-log status, and does not extract, execute or download attachments. `format_valid` does not certify anonymity. Runtime text and older TXT require manual review. Public automation/download isolation is not deployed by this change.

Record `(issue, report_id, bundle_sha256)` before confirming receipt. A repeat ID and hash is a duplicate; the same ID with another hash needs investigation. Review partial/missing data explicitly. Store Health information is a separate evidence source: correlate versions/architecture/stack carefully, do not equate Store failureHash with report_id, and do not put original CAB/health exports into public Issues. Store account setup and symbols must be verified in Partner Center at release time.

Raw ETL/dump collection and private upload remain separate, maintainer-guided work. The app does not start raw file tracing, install a dump service, change Windows diagnostic settings or provide a generic private upload endpoint.

## Validation

```powershell
.\test-native.ps1 -ExecutablePath PATH_TO_NATIVE_EXE
.\test-diagnostic-reports.ps1 -ExecutablePath PATH_TO_NATIVE_EXE
.\test-device-profiles.ps1
```

The second command generates twenty synthetic cases through the actual C++ writer, covering detection phases, product profiles, application context without a detection, optional filtered logs, UTF-8 log truncation and log-read failure. Python independently verifies them, including unchanged historical attempts/snapshots when logs are added. Native checks cover blocked destination replacement, preservation of the old file, Unicode paths, temporary cleanup and the internal-folder guard. Test commands use an isolated temporary log/report store removed on exit. Synthetic supplemental log tests use injected text rather than the user's real logs.

Optional local command: `BluetoothAudioCodec.WinUI.exe --export-standard-report OUTPUT.zip` exports the most recent stored attempt without opening GitHub (exit 3 if none). `--export-diagnostics OUTPUT.txt` retains the legacy text export. No CLI command uploads a report.

`BluetoothAudioCodec.WinUI.exe --export-report OUTPUT.zip [--include-runtime-log]` uses the unified bundle writer and falls back to application context if no detection exists. The runtime-log flag is opt-in. The older `--export-standard-report` alias keeps its no-record exit code and also accepts the optional flag. None of these commands open GitHub or upload a file.

`test-device-profiles.ps1` compiles the actual collector in a headless test host. It checks empty state, target-first bounding and unavailable-connection behavior with injected connection inputs, then reads the real local connection/PnP cache without scanning or connecting. Its output contains counts/status only, not device names or identifiers. A passing run without a connected headset does not verify a particular headset's manufacturer/model properties.

Recovery failures and cleanup-pending attempts use the five-failure local retention allowance even when codec detection succeeded or the user canceled. The ordinary-success allowance remains one; the seven-day and 10 MiB bounds still apply. Native regression checks retain four consecutive recovery failures, then enforce the five-failure cap.
