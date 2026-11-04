"""Validate native-generated synthetic reports with an independent ZIP/JSON reader."""
from pathlib import Path
import argparse
import json
import zipfile

from validate_bundle import validate_bundle


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    outcomes = {"preflight": "preflight_blocked", "canceled": "user_canceled",
                "elevation-canceled": "elevation_canceled", "helper-error": "helper_failed",
                "success": "success", "interrupted": "interrupted_unknown",
                "listening-canceled": "user_canceled", "events-lost": "success", "helper-timeout": "timeout",
                "profile-output-change": "preflight_blocked", "profile-unknown": "preflight_blocked",
                "profile-redacted": "preflight_blocked", "profile-failed": "preflight_blocked", "profile-bounded": "preflight_blocked",
                "playback-control": "success"}
    results = []
    ids = set()
    for name, expected in outcomes.items():
        path = args.directory / (name + ".zip")
        result = validate_bundle(path)
        assert result["report_id"] not in ids
        ids.add(result["report_id"])
        with zipfile.ZipFile(path) as archive:
            assert archive.testzip() is None
            attempt = json.loads(archive.read("attempts.json"))["attempts"][0]
            collection = json.loads(archive.read("collection-status.json"))
            events = [json.loads(line) for line in archive.read("events.jsonl").splitlines()]
            snapshots = json.loads(archive.read("snapshots.json"))["snapshots"]
            manifest = json.loads(archive.read("manifest.json"))
            assert manifest["schema_version"] == "1.1" and manifest["scope"] == "attempt"
            assert manifest["runtime_log"]["status"] == "not_requested" and "runtime-log.txt" not in archive.namelist()
            for entry in archive.infolist():
                text = archive.read(entry).decode("utf-8")
                for forbidden in ["112233AABBCC", "223344BBCCDD", "SYNTHETIC-ENDPOINT", "Alice", "synthetic-container", "second-container", "INTELAUDIO", "owner@example.com", "PRIVATE-UI-ONLY-PROCESS.exe"]:
                    assert forbidden not in text
            assert manifest["origin"] == "synthetic"
            assert not manifest["fixture_reuse_allowed"]
            assert attempt["outcome"] == expected
            if name == "playback-control":
                controls = [e["data"] for e in events if e["event"] == "playback.control"]
                assert len(controls) == 12
                assert controls[0]["control_action"] == "scan"
                assert controls[2]["reason"] == "request-accepted"
                assert controls[4]["control_error"] == 0x80070005
                assert controls[5]["reason"] == "restored"
                assert controls[6]["reason"] == "cleanup-pending"
                assert controls[7]["reason"] == "identity-unavailable" and controls[7]["control_error"] == 15703
                assert controls[8]["reason"] == "package-family-matched"
                assert [e["reason"] for e in controls[9:]] == ["all-system-media-sessions", "system-media-selected", "audio-streams-active"]
                assert all(set(e) == {"control_action", "control_session", "control_error", "reason"} for e in controls)
            assert snapshots[0]["nodes"][1]["container_tag"] == snapshots[0]["nodes"][2]["container_tag"]
            assert any(e["relation"] == "audio_topology" for e in snapshots[0]["edges"])
            profiles = snapshots[0]["device_profiles"]
            first, second = profiles["devices"][:2]
            assert first["device_tag"] == snapshots[0]["nodes"][2]["tag"]
            assert first["container_tag"] == snapshots[0]["nodes"][2]["container_tag"]
            assert first["connection_observation"] == second["connection_observation"] == "connected"
            assert not first["model"]["user_confirmed"]
            assert second["model"]["value"] is None  # Never infer from nickname/PnP driver provider.
            assert second["manufacturer"]["source"] == "windows-pnp"
            if name not in ("profile-unknown", "profile-redacted"):
                assert first["model"]["value"] == "HD815"
                assert first["model"]["source"] == "windows-device-container"
            if name in ("preflight", "interrupted"):
                assert profiles["default_device_tag"] is None
                assert first["default_output_match"] is second["default_output_match"] is None
            else:
                assert profiles["default_device_tag"] == first["device_tag"]
                assert first["default_output_match"] is True and second["default_output_match"] is False
            if name == "profile-output-change":
                end_profiles = snapshots[-1]["device_profiles"]
                assert end_profiles["default_device_tag"] == second["device_tag"]
                assert end_profiles["devices"][0]["default_output_match"] is False
                assert end_profiles["devices"][1]["default_output_match"] is True
                assert attempt["target"]["identity_resolved"]  # The original target stays distinct from the end output.
            if name == "profile-unknown":
                assert first["model"]["status"] == first["manufacturer"]["status"] == "unavailable"
            if name == "profile-redacted":
                assert first["model"]["status"] == first["manufacturer"]["status"] == "redacted"
                assert first["model"]["value"] is first["manufacturer"]["value"] is None
            if name == "profile-failed":
                assert profiles["status"] == collection["completeness"] == "partial"
                assert first["metadata_status"] == "timeout"
            if name == "profile-bounded":
                assert len(profiles["devices"]) == 8 and profiles["omitted_count"] == 4
                assert profiles["status"] == collection["completeness"] == "partial"
            if name == "helper-error":
                assert attempt["error"]["code"] == "0x80070005"
                assert attempt["error"]["api"] == "StartTraceW"
            if name == "success":
                assert collection["capture"]["started"]
                assert collection["capture"]["providers_enabled"] == 2
                assert any(e["event"] == "codec.observed" and e["data"]["codec_standard"] == 2 for e in events)
                assert attempt["target"]["transport"] == "bluetooth-classic"
            if name in ("preflight", "elevation-canceled"):
                assert not collection["capture"]["started"]
                assert collection["loss"]["etw_events_lost"] is None
                assert not any(e["event"] == "etw.started" for e in events)
            if name == "preflight":
                assert attempt["target"]["connection_observation"] == "connected"
                assert attempt["target"]["connection_scope"] == "audio-video-device-set"
                assert attempt["target"]["connected_audio_device_count"] == 2
                assert not attempt["target"]["identity_resolved"]
            if name == "interrupted":
                assert attempt["finished_utc"] is None
                assert not any(e["event"] == "attempt.finished" for e in events)
            if name == "listening-canceled":
                assert collection["completeness"] == "partial"
                assert collection["capture"]["started"]
                assert collection["capture"]["events"] is None
                assert collection["loss"]["ipc_dropped"] is None
            if name == "events-lost":
                assert collection["completeness"] == "partial"
                assert collection["loss"]["etw_events_lost"] == 7
            if name == "helper-timeout":
                assert attempt["reason"] == "helper-timeout"
                assert attempt["error"]["code"] == "0x800705B4"
        results.append({"case": name, "outcome": expected, "format_valid": True,
                        "sha256": result["bundle_sha256"]})
    with zipfile.ZipFile(args.directory / "success.zip") as archive:
        original_snapshots = json.loads(archive.read("snapshots.json"))["snapshots"]
        original_attempts = json.loads(archive.read("attempts.json"))["attempts"]
    for name in ("session", "session-with-log", "attempt-with-log", "runtime-log-truncated", "runtime-log-failed"):
        result = validate_bundle(args.directory / (name + ".zip"))
        assert result["report_id"] not in ids
        ids.add(result["report_id"])
        with zipfile.ZipFile(args.directory / (name + ".zip")) as archive:
            manifest = json.loads(archive.read("manifest.json"))
            attempts = json.loads(archive.read("attempts.json"))["attempts"]
            snapshots = json.loads(archive.read("snapshots.json"))["snapshots"]
            log = manifest["runtime_log"]
            session = name.startswith("session")
            assert manifest["scope"] == ("session" if session else "attempt")
            if session:
                assert not attempts and not snapshots and archive.read("events.jsonl") == b""
                assert result["event_count"] == result["attempt_count"] == 0
                assert b"no detection selected" in archive.read("summary.txt")
            else:
                assert attempts == original_attempts and snapshots == original_snapshots
            if name in ("session", "runtime-log-failed"):
                assert "runtime-log.txt" not in archive.namelist()
                assert log["status"] == ("not_requested" if name == "session" else "failed")
            else:
                data = archive.read("runtime-log.txt")
                assert len(archive.namelist()) == 8 and len(data) <= 512 * 1024
                text = data.decode("utf-8")
                assert "NEWEST-TAIL" in text and "export" in text
                assert "112233AABBCC" not in text and "owner@example.com" not in text
                assert log["status"] == "included" and log["error"] is None
                assert log["captured_start_utc"] <= log["captured_end_utc"]
                assert log["truncated"] == (name == "runtime-log-truncated")
            if name == "runtime-log-failed":
                assert log["error"]["code"] == "0x80070005"
                assert result["completeness"] == "partial"
                assert b"private-error-message-must-not-appear" not in archive.read("manifest.json")
        results.append({"case": name, "scope": manifest["scope"], "runtime_log": log["status"], "format_valid": True,
                        "sha256": result["bundle_sha256"]})
    payload = {"passed": True, "native_generated_cases": results, "scope": "synthetic native report generation and independent receiver validation",
               "real_headset_etw_capture": False, "external_upload": False}
    (args.directory / "contract-results.json").write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": True, "cases": len(results)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
