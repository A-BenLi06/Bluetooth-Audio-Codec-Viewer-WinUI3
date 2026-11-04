"""Check protocol examples and intentionally malformed bundles; no uploads or device I/O."""
from pathlib import Path
import copy
import hashlib
import json
import tempfile
import warnings
import sys
import zipfile

from validate_bundle import validate_bundle, InvalidBundle

ROOT = Path(__file__).resolve().parents[1]


def main():
    source = ROOT / "fixtures/public-v1-synthetic.zip"
    with zipfile.ZipFile(source) as archive:
        baseline = {name: archive.read(name) for name in archive.namelist()}
    results = []

    def rewrite(entries, name, change):
        value = json.loads(entries[name])
        change(value)
        entries[name] = (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8")

    def rewrite_events(entries, change):
        values = [json.loads(line) for line in entries["events.jsonl"].splitlines()]
        change(values)
        entries["events.jsonl"] = "".join(json.dumps(v) + "\n" for v in values).encode()

    def inventory(entries):
        def update(manifest):
            for item in manifest["files"]:
                if item["path"] in entries:
                    data = entries[item["path"]]
                    item["bytes"], item["sha256"] = len(data), hashlib.sha256(data).hexdigest()
        rewrite(entries, "manifest.json", update)

    def case(name, mutate=None, fix_inventory=True, expected=True, duplicate=False):
        entries = copy.deepcopy(baseline)
        if mutate:
            mutate(entries)
        if fix_inventory:
            inventory(entries)
        with tempfile.TemporaryDirectory(prefix="bac-protocol-test-") as directory:
            path = Path(directory) / "case.zip"
            with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                for filename, data in entries.items():
                    archive.writestr(filename, data)
                if duplicate:
                    with warnings.catch_warnings():
                        warnings.simplefilter("ignore", UserWarning)
                        archive.writestr("summary.txt", entries["summary.txt"])
            try:
                result = validate_bundle(path)
                accepted, detail = True, result["status"]
            except (InvalidBundle, ValueError, zipfile.BadZipFile) as error:
                accepted, detail = False, str(error)[:120]
        passed = accepted == expected
        results.append({"case": name, "expected": "accept" if expected else "reject",
                        "actual": "accept" if accepted else "reject", "passed": passed, "detail": detail})

    case("valid synthetic partial report")
    case("modified summary without hash update", lambda e: e.__setitem__("summary.txt", e["summary.txt"] + b"changed"),
         fix_inventory=False, expected=False)
    case("directory traversal extra entry", lambda e: e.__setitem__("../payload.txt", b"x"), expected=False)
    case("duplicate ZIP filename", duplicate=True, expected=False)
    case("restricted audience rejected", lambda e: rewrite(e, "manifest.json", lambda v: v.__setitem__("audience", "restricted")), expected=False)
    case("unknown structured field rejected", lambda e: rewrite(e, "environment.json", lambda v: v.__setitem__("raw_device_id", "synthetic")), expected=False)
    case("cross-file report mismatch", lambda e: rewrite(e, "environment.json", lambda v: v.__setitem__("report_id", "00000000-0000-4000-8000-000000000004")), expected=False)
    case("unknown attempt in event", lambda e: rewrite_events(e, lambda v: v[0].__setitem__("attempt_id", "00000000-0000-4000-8000-000000000004")), expected=False)
    case("duplicate seq", lambda e: rewrite_events(e, lambda v: v[1].__setitem__("seq", 1)), expected=False)
    case("missing terminal event", lambda e: rewrite_events(e, lambda v: v.pop()), expected=False)
    case("terminal mismatch", lambda e: rewrite_events(e, lambda v: v[-1]["data"].__setitem__("outcome", "success")), expected=False)
    case("ETW absent with zero substituted for unknown", lambda e: rewrite(e, "collection-status.json", lambda v: v["loss"].__setitem__("etw_events_lost", 0)), expected=False)
    case("unbounded message field", lambda e: rewrite_events(e, lambda v: v[0]["data"].__setitem__("message", "arbitrary exception text")), expected=False)
    def playback_event(entries, change=None):
        def insert(values):
            event = copy.deepcopy(values[0])
            event["event"] = "playback.control"
            event["data"] = {"control_action": "pause", "reason": "request-accepted", "control_session": 1, "control_error": None}
            if change:
                change(event["data"])
            values.insert(-1, event)
            for i, item in enumerate(values):
                item["seq"] = i + 1
        rewrite_events(entries, insert)
    case("playback control extension accepted", playback_event, expected=True)
    case("playback raw app ID rejected", lambda e: playback_event(e, lambda v: v.__setitem__("app_id", "private-player")), expected=False)
    case("playback unknown action rejected", lambda e: playback_event(e, lambda v: v.__setitem__("control_action", "kill")), expected=False)
    case("playback missing error status rejected", lambda e: playback_event(e, lambda v: v.pop("control_error")), expected=False)
    case("playback session bound enforced", lambda e: playback_event(e, lambda v: v.__setitem__("control_session", 129)), expected=False)
    case("playback unknown reason rejected", lambda e: playback_event(e, lambda v: v.__setitem__("reason", "private-player")), expected=False)
    case("playback package-family match accepted", lambda e: playback_event(e, lambda v: v.update(control_action="match", reason="package-family-matched")), expected=True)
    case("playback desktop window identity match accepted", lambda e: playback_event(e, lambda v: v.update(control_action="match", reason="window-app-matched")), expected=True)
    case("playback desktop identity guard accepted", lambda e: playback_event(e, lambda v: v.update(control_action="restore", reason="desktop-identity-changed")), expected=True)
    for reason in ("all-system-media-sessions", "system-media-selected", "not-playing", "audio-streams-active", "audio-streams-idle", "audio-inventory-incomplete"):
        case(f"playback global policy reason {reason} accepted", lambda e, r=reason: playback_event(e, lambda v: v.update(reason=r)), expected=True)
    case("playback desktop root PID is never exported", lambda e: playback_event(e, lambda v: v.__setitem__("root_pid", 1234)), expected=False)
    case("playback unavailable identity error accepted", lambda e: playback_event(e, lambda v: v.update(control_action="scan", reason="identity-unavailable", control_error=15703)), expected=True)
    case("playback restore media guard accepted", lambda e: playback_event(e, lambda v: v.update(control_action="restore", reason="media-or-playback-changed")), expected=True)
    case("playback restore position guard accepted", lambda e: playback_event(e, lambda v: v.update(control_action="restore", reason="position-changed")), expected=True)
    case("playback process name is UI-only", lambda e: playback_event(e, lambda v: v.__setitem__("process_name", "GameViewer.exe")), expected=False)
    case("playback refreshed timeline accepted", lambda e: playback_event(e, lambda v: v.update(reason="timeline-refreshed")), expected=True)
    case("playback stale timeline accepted", lambda e: playback_event(e, lambda v: v.update(reason="timeline-stale")), expected=True)
    case("playback precise range guard accepted", lambda e: playback_event(e, lambda v: v.update(control_action="restore", reason="media-range-changed")), expected=True)
    case("playback precise state guard accepted", lambda e: playback_event(e, lambda v: v.update(control_action="restore", reason="playback-playing-observed")), expected=True)
    for field in ("title", "artist", "album", "track"):
        case(f"playback changed {field} guard accepted", lambda e, f=field: playback_event(e, lambda v: v.update(control_action="restore", reason=f"media-{f}-changed")), expected=True)
    case("playback media fingerprint is never exported", lambda e: playback_event(e, lambda v: v.__setitem__("media_fingerprint", "0" * 64)), expected=False)
    case("playback media title is never exported", lambda e: playback_event(e, lambda v: v.__setitem__("media_title", "PRIVATE TEST MEDIA")), expected=False)
    case("playback unrelated data rejected", lambda e: playback_event(e, lambda v: v.__setitem__("codec_standard", 2)), expected=False)
    case("unknown graph reference", lambda e: rewrite(e, "snapshots.json", lambda v: v["snapshots"][0]["edges"][0].__setitem__("to", "device-9")), expected=False)
    case("invalid UTC date", lambda e: rewrite(e, "manifest.json", lambda v: v.__setitem__("created_utc", "2026-02-30T00:00:00.000Z")), expected=False)
    case("empty event line", lambda e: e.__setitem__("events.jsonl", b"\n" + e["events.jsonl"]), expected=False)
    case("non-UTF8 summary", lambda e: e.__setitem__("summary.txt", b"\xff"), expected=False)
    case("oversize/expansion budget", lambda e: e.__setitem__("summary.txt", b"0" * (2 * 1024 * 1024 + 1)), expected=False)
    case("synthetic event in observed report", lambda e: rewrite(e, "manifest.json", lambda v: v.__setitem__("origin", "observed")), expected=False)
    case("duplicate JSON property", lambda e: e.__setitem__("manifest.json", e["manifest.json"].replace(b'"audience": "public"', b'"audience": "public", "audience": "public"')), fix_inventory=False, expected=False)

    def profile_extension(entries, change=None):
        def update(document):
            snapshot = document["snapshots"][0]
            snapshot["output"]["connected_audio_device_count"] = 1
            field = {"value": "HD815", "source": "windows-device-container", "status": "reported", "user_confirmed": False}
            device = {"device_tag": "device-1", "container_tag": "container-1", "connection_observation": "connected",
                      "default_output_match": None, "metadata_status": "collected", "model": field,
                      "manufacturer": {"value": None, "source": "none", "status": "unavailable", "user_confirmed": False}}
            snapshot["device_profiles"] = {"scope": "connected-classic-audio-and-default-output", "status": "collected",
                                           "connected_classic_audio_count": 1, "omitted_count": 0,
                                           "default_device_tag": None, "devices": [device]}
            if change:
                change(snapshot["device_profiles"])
        rewrite(entries, "snapshots.json", update)

    case("valid product profile extension", profile_extension)
    case("unrecognized model source", lambda e: profile_extension(e, lambda p: p["devices"][0]["model"].__setitem__("source", "nickname")), expected=False)
    case("unavailable model has value", lambda e: profile_extension(e, lambda p: p["devices"][0]["model"].__setitem__("status", "unavailable")), expected=False)
    case("Windows model cannot claim user confirmation", lambda e: profile_extension(e, lambda p: p["devices"][0]["model"].__setitem__("user_confirmed", True)), expected=False)
    case("duplicate product profile", lambda e: profile_extension(e, lambda p: p["devices"].append(copy.deepcopy(p["devices"][0]))), expected=False)
    case("unresolved default output cannot match device", lambda e: profile_extension(e, lambda p: (p["devices"][0].__setitem__("default_output_match", True), p.__setitem__("default_device_tag", "device-1"))), expected=False)
    case("default profile tag absent", lambda e: profile_extension(e, lambda p: p.__setitem__("default_device_tag", "device-9")), expected=False)
    case("profile graph container mismatch", lambda e: profile_extension(e, lambda p: p["devices"][0].__setitem__("container_tag", "container-9")), expected=False)
    case("profile connection count mismatch", lambda e: profile_extension(e, lambda p: p.__setitem__("connected_classic_audio_count", 2)), expected=False)
    case("profile omissions cannot claim collected", lambda e: profile_extension(e, lambda p: p.__setitem__("omitted_count", 1)), expected=False)
    case("overlong product model", lambda e: profile_extension(e, lambda p: p["devices"][0]["model"].__setitem__("value", "a" * 121)), expected=False)
    case("product model with control character", lambda e: profile_extension(e, lambda p: p["devices"][0]["model"].__setitem__("value", "HD815\nInjected")), expected=False)
    case("raw profile address rejected", lambda e: profile_extension(e, lambda p: p["devices"][0].__setitem__("address", "112233AABBCC")), expected=False)
    def ambiguous_profile(profiles):
        device = profiles["devices"][0]
        device["metadata_status"], device["container_tag"] = "ambiguous", None
        device["model"] = copy.deepcopy(device["manufacturer"])
    case("valid unknown metadata on ambiguous container", lambda e: profile_extension(e, ambiguous_profile))
    case("ambiguous container cannot attribute model", lambda e: profile_extension(e, lambda p: p["devices"][0].__setitem__("metadata_status", "ambiguous")), expected=False)

    def unified(entries, change=None, include_log=False, session=False):
        for filename in list(entries):
            if filename.endswith(".json"):
                rewrite(entries, filename, lambda v: v.__setitem__("schema_version", "1.1"))
        rewrite_events(entries, lambda v: [event.__setitem__("schema_version", "1.1") for event in v])
        def manifest_update(value):
            value["scope"] = "session" if session else "attempt"
            value["runtime_log"] = {"status": "included" if include_log else "not_requested", "scope": "export-time-recent-history",
                                    "captured_start_utc": "2026-10-27T00:00:00.000Z" if include_log else None,
                                    "captured_end_utc": "2026-10-27T00:00:00.010Z" if include_log else None,
                                    "truncated": False, "error": None}
            if include_log:
                value["redaction_policy"] = "public-fields-v1-with-filtered-runtime-log"
                value["files"].append({"path": "runtime-log.txt", "bytes": 0, "sha256": "0" * 64})
        rewrite(entries, "manifest.json", manifest_update)
        if include_log:
            entries["runtime-log.txt"] = b"SYNTHETIC export-time recent runtime history\n"
        if session:
            rewrite(entries, "attempts.json", lambda v: v.__setitem__("attempts", []))
            rewrite(entries, "snapshots.json", lambda v: v.__setitem__("snapshots", []))
            entries["events.jsonl"] = b""
            def collection_update(value):
                value["completeness"] = "partial"
                for component in value["components"]:
                    if component["name"] in ("events", "snapshots"):
                        component["status"], component["reason"] = "not_requested", "no-detection-record"
                value["capture"] = {"started": False, **{key: None for key in
                                    ("providers_enabled", "events", "metadata_failures", "streaming_events", "configuration_events", "parse_failures", "rejected", "stop_status")}}
            rewrite(entries, "collection-status.json", collection_update)
        if change:
            change(entries)

    case("valid v1.1 attempt without runtime log", unified)
    case("valid v1.1 attempt with runtime log", lambda e: unified(e, include_log=True))
    case("valid application report with no detection", lambda e: unified(e, include_log=True, session=True))
    case("session report must not claim an attempt", lambda e: unified(e, lambda x: rewrite(x, "manifest.json", lambda v: v.__setitem__("scope", "session"))), expected=False)
    case("runtime log marked included but missing", lambda e: unified(e, lambda x: x.pop("runtime-log.txt"), include_log=True), expected=False)
    case("unrequested runtime log file present", lambda e: unified(e, lambda x: x.__setitem__("runtime-log.txt", b"x")), expected=False)
    case("runtime log without filtered-log policy", lambda e: unified(e, lambda x: rewrite(x, "manifest.json", lambda v: v.__setitem__("redaction_policy", "public-fields-v1")), include_log=True), expected=False)
    case("runtime log invalid UTF8", lambda e: unified(e, lambda x: x.__setitem__("runtime-log.txt", b"\xff"), include_log=True), expected=False)
    case("runtime log exceeds budget", lambda e: unified(e, lambda x: x.__setitem__("runtime-log.txt", b"x" * (512 * 1024 + 1)), include_log=True), expected=False)
    case("unrequested runtime log claims truncation", lambda e: unified(e, lambda x: rewrite(x, "manifest.json", lambda v: v["runtime_log"].__setitem__("truncated", True))), expected=False)
    case("included runtime log missing collection time", lambda e: unified(e, lambda x: rewrite(x, "manifest.json", lambda v: v["runtime_log"].__setitem__("captured_start_utc", None)), include_log=True), expected=False)
    case("session report cannot claim ETW capture", lambda e: unified(e, lambda x: rewrite(x, "collection-status.json", lambda v: v["capture"].__setitem__("started", True)), session=True), expected=False)
    case("mixed file schema versions", lambda e: unified(e, lambda x: rewrite(x, "environment.json", lambda v: v.__setitem__("schema_version", "1.0"))), expected=False)
    case("old version cannot silently accept new log", lambda e: unified(e, lambda x: rewrite(x, "manifest.json", lambda v: v.__setitem__("schema_version", "1.0")), include_log=True), expected=False)
    payload = {"scope": "offline schema and cross-file validation; synthetic inputs only",
               "passed": all(item["passed"] for item in results), "case_count": len(results), "cases": results,
               "baseline": validate_bundle(source), "uploads_performed": False,
               "real_device_tested": False, "store_backend_tested": False,
               "github_browser_submission_tested": False}
    Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "intake-verification.json").write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": payload["passed"], "case_count": len(results)}, ensure_ascii=False))
    if not payload["passed"]:
        for result in results:
            if not result["passed"]:
                print(result)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
