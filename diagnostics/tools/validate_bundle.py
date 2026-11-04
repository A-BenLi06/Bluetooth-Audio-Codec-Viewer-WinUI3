"""Offline format prototype. Reads bounded ZIP entries in memory; never extracts or fetches.

Not a privacy certifier, signature verifier, downloader, or production sandbox.
Requires Python 3.11+ and the pinned jsonschema dependency.
"""
from datetime import datetime
from pathlib import Path
import argparse
import hashlib
import json
import stat
import zipfile

from jsonschema import Draft202012Validator, FormatChecker

ROOT = Path(__file__).resolve().parents[1]
VERSION = "intake-reference-1.9"
MIB = 1024 * 1024
LIMIT_ZIP = 5 * MIB
LIMIT_TOTAL = 10 * MIB
LIMIT_FILE = 2 * MIB
NAMES = {"manifest.json", "summary.txt", "environment.json", "attempts.json", "events.jsonl",
         "snapshots.json", "collection-status.json"}
RUNTIME_LOG = "runtime-log.txt"


class InvalidBundle(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise InvalidBundle(message)


def unique_pairs(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "duplicate JSON key")
        result[key] = value
    return result


def parse(data):
    require(not data.startswith(b"\xef\xbb\xbf"), "UTF-8 BOM is not allowed")
    return json.loads(data.decode("utf-8"), object_pairs_hook=unique_pairs,
                      parse_constant=lambda _: (_ for _ in ()).throw(InvalidBundle("non-finite JSON number")))


def timestamps(value):
    if isinstance(value, dict):
        for key, child in value.items():
            if child is not None and (key.endswith("_utc") or key == "utc"):
                datetime.strptime(child, "%Y-%m-%dT%H:%M:%S.%fZ")
            timestamps(child)
    elif isinstance(value, list):
        for child in value:
            timestamps(child)


def validate_bundle(path):
    path = Path(path)
    require(path.is_file() and path.stat().st_size <= LIMIT_ZIP, "ZIP missing or exceeds 5 MiB")
    # A bounded local copy avoids reading a different file if another process replaces it.
    with path.open("rb") as source:
        bundle = source.read(LIMIT_ZIP + 1)
    require(len(bundle) <= LIMIT_ZIP, "ZIP exceeds 5 MiB")
    import io
    with zipfile.ZipFile(io.BytesIO(bundle)) as archive:
        infos = archive.infolist()
        names = [entry.filename for entry in infos]
        require(len(names) in (7, 8) and len(set(names)) == len(names), "duplicate/missing/extra ZIP entries")
        require(set(names) in (NAMES, NAMES | {RUNTIME_LOG}), "only core filenames and optional runtime-log.txt are allowed")
        require(sum(i.file_size for i in infos) <= LIMIT_TOTAL, "uncompressed total exceeds 10 MiB")
        entries = {}
        for entry in infos:
            require(not entry.is_dir() and not entry.flag_bits & 1, "directory/encrypted ZIP entry")
            require(not stat.S_ISLNK(entry.external_attr >> 16), "symlink ZIP entry")
            require(entry.compress_type in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED), "unsupported compression")
            require(0 <= entry.file_size <= LIMIT_FILE, "file exceeds 2 MiB")
            require(entry.file_size <= max(1, entry.compress_size) * 300, "suspicious expansion ratio")
            with archive.open(entry) as stream:
                data = stream.read(LIMIT_FILE + 1)
            require(len(data) == entry.file_size and len(data) <= LIMIT_FILE, "ZIP length mismatch")
            entries[entry.filename] = data

    require(len(entries["summary.txt"]) <= 64 * 1024, "summary exceeds 64 KiB")
    summary = entries["summary.txt"].decode("utf-8")
    require(not summary.startswith("\ufeff"), "summary must not contain UTF-8 BOM")
    schema = json.loads((ROOT / "schemas/public-report.schema.json").read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)

    def checked(name, value):
        selected = {**schema, "anyOf": [{"$ref": "#/$defs/" + name}]}
        validator = Draft202012Validator(selected, format_checker=FormatChecker())
        errors = sorted(validator.iter_errors(value), key=lambda e: str(list(e.path)))
        require(not errors, "schema invalid: " + name + (" at " + str(list(errors[0].path)) if errors else ""))
        timestamps(value)
        return value

    docs = {name: checked(name.removesuffix(".json"), parse(entries[name])) for name in NAMES
            if name.endswith(".json")}
    manifest = docs["manifest.json"]
    report_id = manifest["report_id"]
    version = manifest["schema_version"]
    session = manifest.get("scope", "attempt") == "session"
    require(all(doc["schema_version"] == version for doc in docs.values()), "cross-file schema_version mismatch")
    require(all(doc["report_id"] == report_id for doc in docs.values()), "cross-file report_id mismatch")
    require(report_id in summary, "summary does not identify this report")
    inventory = manifest["files"]
    expected = set(entries) - {"manifest.json"}
    require(len({i["path"] for i in inventory}) == len(expected) and {i["path"] for i in inventory} == expected,
            "manifest inventory incomplete/duplicate")
    for item in inventory:
        payload = entries[item["path"]]
        require(len(payload) == item["bytes"] and hashlib.sha256(payload).hexdigest() == item["sha256"],
                "inventory hash/size mismatch")

    raw_lines = entries["events.jsonl"].splitlines()
    require((len(raw_lines) == 0 if session else 0 < len(raw_lines) <= 10000), "event count outside scope/budget")
    require(all(0 < len(line) <= 16 * 1024 for line in raw_lines), "empty/oversize event line")
    events = [checked("event", parse(line)) for line in raw_lines]
    require(all(event["schema_version"] == version for event in events), "event schema_version mismatch")
    seq = [event["seq"] for event in events]
    require(seq == sorted(set(seq)), "seq must be unique and increasing")
    require(all(e["report_id"] == report_id for e in events), "event report_id mismatch")
    require(all(e["origin"] == manifest["origin"] for e in events), "mixed observed/synthetic events")
    attempts = docs["attempts.json"]["attempts"]
    require(len(attempts) == 0 if session else len(attempts) > 0, "attempt inventory contradicts report scope")
    by_attempt = {a["attempt_id"]: a for a in attempts}
    require(len(by_attempt) == len(attempts), "duplicate attempt_id")
    by_event = {key: [] for key in by_attempt}
    for event in events:
        require(event["attempt_id"] in by_attempt, "event has unknown attempt_id")
        attempt = by_attempt[event["attempt_id"]]
        require(event["run_id"] == attempt["run_id"], "event run_id mismatch")
        by_event[event["attempt_id"]].append(event)
        required_data = {"attempt.finished": {"outcome", "reason"},
                         "preflight.completed": {"reason", "output"},
                         "snapshot.captured": {"snapshot_id"},
                         "helper.started": {"helper_pid"},
                         "codec.observed": {"codec_standard", "codec_vendor", "codec_id"},
                         "playback.control": {"control_action", "control_session", "control_error", "reason"}}
        require(required_data.get(event["event"], set()).issubset(event["data"]), "event-specific fields missing")
    for attempt_id, attempt in by_attempt.items():
        own_events = by_event[attempt_id]
        starts = [e for e in own_events if e["event"] == "attempt.started"]
        terminal = [e for e in own_events if e["event"] == "attempt.finished"]
        require(len(starts) == 1, "attempt must have one start event")
        if attempt["outcome"] == "interrupted_unknown":
            require(attempt["finished_utc"] is None and not terminal, "interrupted attempt cannot have a terminal")
        else:
            require(attempt["finished_utc"] is not None and len(terminal) == 1, "terminal missing or duplicate")
            require(terminal[0]["data"]["outcome"] == attempt["outcome"] and
                    terminal[0]["data"]["reason"] == attempt["reason"], "terminal outcome/reason mismatch")
            require(terminal[0]["seq"] == max(e["seq"] for e in own_events), "attempt events after terminal")
            require(attempt["started_utc"] <= attempt["finished_utc"], "attempt time reversed")

    snapshots = docs["snapshots.json"]["snapshots"]
    require(not snapshots if session else True, "session report cannot claim detection snapshots")
    snapshot_ids = {s["snapshot_id"] for s in snapshots}
    require(len(snapshot_ids) == len(snapshots), "duplicate snapshot_id")
    for snapshot in snapshots:
        require(snapshot["attempt_id"] in by_attempt, "snapshot has unknown attempt_id")
        require(snapshot["captured_start_utc"] <= snapshot["captured_end_utc"], "snapshot time reversed")
        tags = {node["tag"] for node in snapshot["nodes"]}
        require(len(tags) == len(snapshot["nodes"]), "duplicate snapshot node")
        for edge in snapshot["edges"]:
            require(edge["from"] in tags and edge["to"] in tags, "edge refers to unknown node")
            require(edge["origin"] == manifest["origin"], "mixed observed/synthetic graph")
        profiles = snapshot.get("device_profiles")
        if profiles is not None:
            devices = profiles["devices"]
            profile_tags = {device["device_tag"] for device in devices}
            require(len(profile_tags) == len(devices), "duplicate device profile")
            matched = [device for device in devices if device["default_output_match"] is True]
            require(len(matched) <= 1, "multiple devices match default output")
            require(profiles["default_device_tag"] == (matched[0]["device_tag"] if matched else None),
                    "default device profile mismatch")
            if snapshot["output"]["identity_resolved"] is not True:
                require(all(device["default_output_match"] is None for device in devices),
                        "unresolved output cannot identify a profile")
            require(profiles["status"] == "partial" or profiles["omitted_count"] == 0,
                    "collected profile list has omitted devices")
            require(profiles["connected_classic_audio_count"] == snapshot["output"].get("connected_audio_device_count"),
                    "profile connection count mismatch")
            for device in devices:
                for node in snapshot["nodes"]:
                    if node["tag"] == device["device_tag"]:
                        require(node["kind"] == "remote-device" and
                                (device["container_tag"] is None or node["container_tag"] == device["container_tag"]),
                                "device profile graph mismatch")
                if device["metadata_status"] == "ambiguous":
                    require(device["container_tag"] is None and all(device[k]["value"] is None for k in ("model", "manufacturer")),
                            "ambiguous device cannot have attributed product metadata")
    snapshot_attempts = {s["snapshot_id"]: s["attempt_id"] for s in snapshots}
    for event in events:
        if "snapshot_id" in event["data"]:
            require(snapshot_attempts.get(event["data"]["snapshot_id"]) == event["attempt_id"], "snapshot reference mismatch")
    collection = docs["collection-status.json"]
    components = {c["name"]: c for c in collection["components"]}
    require(set(components) == {"events", "snapshots", "drivers", "etw"}, "component inventory incomplete/duplicate")
    if session:
        require(components["events"]["status"] == components["snapshots"]["status"] == "not_requested" and
                collection["completeness"] == "partial" and not collection.get("capture", {}).get("started", True),
                "session report cannot claim a detection or ETW capture")
    log = manifest.get("runtime_log")
    if version == "1.0":
        require(RUNTIME_LOG not in entries, "version 1.0 cannot include a runtime-log supplement")
    else:
        included = log["status"] == "included"
        require(included == (RUNTIME_LOG in entries), "runtime log attachment/status mismatch")
        require(manifest["redaction_policy"] == ("public-fields-v1-with-filtered-runtime-log" if included else "public-fields-v1"),
                "runtime log redaction policy mismatch")
        require(included or not log["truncated"], "absent runtime log cannot be truncated")
        if log["status"] == "not_requested":
            require(log["captured_start_utc"] is log["captured_end_utc"] is log["error"] is None,
                    "unrequested runtime log cannot claim a collection")
        else:
            require(log["captured_start_utc"] is not None and log["captured_end_utc"] is not None and
                    log["captured_start_utc"] <= log["captured_end_utc"], "runtime log collection time missing/reversed")
            require((log["error"] is not None) == (log["status"] == "failed"), "runtime log error/status mismatch")
        if included:
            require(len(entries[RUNTIME_LOG]) <= 512 * 1024, "runtime log exceeds 512 KiB")
            require(not entries[RUNTIME_LOG].decode("utf-8").startswith("\ufeff"), "runtime log BOM is not allowed")
        if log["status"] == "failed":
            require(collection["completeness"] == "partial", "failed runtime log cannot claim completeness")
    if components["etw"]["status"] in ("not_started", "not_requested"):
        require(not any(e["event"] == "etw.started" for e in events), "ETW status contradicts events")
        require(all(collection["loss"][k] is None for k in collection["loss"] if k.startswith("etw_")),
                "ETW not started must use null loss counters")
    if collection["completeness"] == "complete":
        require(all(components[k]["status"] == "collected" for k in ("events", "snapshots", "drivers")),
                "complete report with missing components")
        require(all(collection["loss"][k] == 0 for k in collection["loss"] if not k.startswith("etw_")),
                "complete report with unknown/nonzero writer loss")
    return {"validator_version": VERSION, "status": "format_valid", "report_id": report_id,
            "bundle_sha256": hashlib.sha256(bundle).hexdigest(), "origin": manifest["origin"],
            "completeness": collection["completeness"], "attempt_count": len(attempts), "event_count": len(events),
            "report_scope": "session" if session else "attempt", "runtime_log_status": log["status"] if log else "not_supported",
            "privacy_certified": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("zip", type=Path)
    args = parser.parse_args()
    try:
        result = validate_bundle(args.zip)
    except (ValueError, OSError, zipfile.BadZipFile, RuntimeError, RecursionError, NotImplementedError) as error:
        # Do not echo untrusted strings / contents into logs. Details are deliberately bounded.
        print(json.dumps({"status": "rejected_bundle", "error_class": type(error).__name__}))
        return 1
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
