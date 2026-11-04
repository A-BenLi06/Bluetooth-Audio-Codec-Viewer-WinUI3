#include "pch.h"
#include "DiagnosticsReport.h"
#include "BluetoothDeviceProfiles.h"
#include "BluetoothConnectionQuery.h"
#include "DiagnosticBuild.h"
#include <appmodel.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <array>
#include <fstream>
#include <format>
#include <map>
#include <set>

using namespace winrt;
using namespace Windows::Data::Json;
namespace Native
{
namespace
{
constexpr size_t MiB = 1024 * 1024;
constexpr size_t RecordLimit = 2 * MiB;
std::mutex RecordsGate;
std::map<std::wstring, JsonObject> MemoryRecords;
std::filesystem::path TestDirectory;
std::atomic<uint32_t> RecordWriteFailures{};
std::wstring Id()
{
    GUID id{};
    check_hresult(CoCreateGuid(&id));
    wchar_t value[40]{};
    StringFromGUID2(id, value, 40);
    std::wstring result(value + 1, 36);
    std::transform(result.begin(), result.end(), result.begin(), towlower);
    return result;
}
std::wstring const &RunId() { static auto value = Id(); return value; }
void String(JsonObject const &o, wchar_t const *key, std::wstring_view value)
{ o.SetNamedValue(key, JsonValue::CreateStringValue(hstring(value))); }
void Number(JsonObject const &o, wchar_t const *key, uint64_t value)
{ o.SetNamedValue(key, JsonValue::CreateNumberValue(static_cast<double>(value))); }
void Boolean(JsonObject const &o, wchar_t const *key, bool value)
{ o.SetNamedValue(key, JsonValue::CreateBooleanValue(value)); }
void OptionalNumber(JsonObject const &o, wchar_t const *key, std::optional<uint32_t> value)
{ o.SetNamedValue(key, value ? JsonValue::CreateNumberValue(*value) : JsonValue::CreateNullValue()); }
JsonObject Clone(JsonObject const &o) { return JsonObject::Parse(o.Stringify()); }
std::string Sha256(std::string_view input)
{
    std::array<BYTE, 32> digest{};
    check_nt(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<char *>(input.data())), static_cast<ULONG>(input.size()),
        digest.data(), static_cast<ULONG>(digest.size())));
    std::string result;
    for (auto b : digest) result += std::format("{:02x}", b);
    return result;
}
std::wstring FileVersion(std::filesystem::path const &path)
{
    DWORD ignored{};
    auto size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (!size || size > MiB) return L"unavailable";
    std::vector<BYTE> bytes(size);
    VS_FIXEDFILEINFO *version{};
    UINT length{};
    if (!GetFileVersionInfoW(path.c_str(), 0, size, bytes.data()) ||
        !VerQueryValueW(bytes.data(), L"\\", reinterpret_cast<void **>(&version), &length) || length < sizeof(*version))
        return L"unavailable";
    return std::format(L"{}.{}.{}.{}", HIWORD(version->dwFileVersionMS), LOWORD(version->dwFileVersionMS),
                       HIWORD(version->dwFileVersionLS), LOWORD(version->dwFileVersionLS));
}
std::string ReadBounded(std::filesystem::path const &path, size_t maximum)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw hresult_error(HRESULT_FROM_WIN32(ERROR_READ_FAULT));
    std::string data(maximum + 1, '\0');
    input.read(data.data(), static_cast<std::streamsize>(data.size()));
    if (input.bad()) throw hresult_error(HRESULT_FROM_WIN32(ERROR_READ_FAULT));
    data.resize(static_cast<size_t>(input.gcount()));
    if (data.size() > maximum) throw hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
    return data;
}
void AtomicWrite(std::filesystem::path const &destination, std::string_view bytes)
{
    auto temporary = destination;
    temporary += L"." + Id() + L".tmp";
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{temporary};
    {
        Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file) throw_last_error();
        DWORD written{};
        if (!WriteFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
            written != bytes.size() || !FlushFileBuffers(file.get()))
            throw hresult_error(HRESULT_FROM_WIN32(ERROR_WRITE_FAULT));
    }
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw_last_error();
}
JsonObject Error(std::wstring_view api, uint32_t code, std::wstring_view stage, std::wstring_view reason,
                 std::wstring_view domain = L"hresult")
{
    JsonObject result;
    String(result, L"api", api); String(result, L"domain", domain);
    String(result, L"code", std::format(L"0x{:08X}", code));
    String(result, L"stage", stage); String(result, L"reason", reason);
    return result;
}
std::wstring Reason(std::wstring_view value)
{
    // No arbitrary helper messages can become a machine field or URL parameter.
    static std::set<std::wstring, std::less<>> const reasons{
        L"success", L"ready", L"not-bluetooth", L"le-audio", L"hands-free", L"target-unresolved",
        L"output-changed", L"tone-failed", L"schema-unsupported", L"metadata-unavailable", L"microphone-active",
        L"waiting-for-stream-end", L"no-provider-events", L"no-matching-event", L"user-canceled",
        L"elevation-canceled", L"helper-exited", L"helper-timeout", L"helper-failed", L"internal-error", L"interrupted-unknown"};
    return reasons.contains(value) ? std::wstring(value) : L"internal-error";
}
JsonObject Environment()
{
    JsonObject result, app, windows;
    wchar_t exe[32768]{};
    GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
    String(app, L"version", FileVersion(exe));
    String(app, L"build_id", BAC_BUILD_ID);
    if (std::wstring_view(BAC_GIT_COMMIT).empty()) app.SetNamedValue(L"git_commit", JsonValue::CreateNullValue());
    else String(app, L"git_commit", BAC_GIT_COMMIT);
    Boolean(app, L"dirty", BAC_BUILD_DIRTY != 0);
#if defined(_M_ARM64)
    String(app, L"architecture", L"arm64");
#else
    String(app, L"architecture", L"x64");
#endif
    UINT32 length{};
    auto packaged = GetCurrentPackageFullName(&length, nullptr) == ERROR_INSUFFICIENT_BUFFER;
    String(app, L"packaging", packaged ? L"msix" : BAC_PACKAGING);
    String(app, L"distribution", BAC_DISTRIBUTION);
    String(app, L"distribution_evidence", std::wstring_view(BAC_DISTRIBUTION) == L"unknown" ? L"unknown" : L"release-metadata");
    try { String(app, L"exe_sha256", to_hstring(Sha256(ReadBounded(exe, 32 * MiB)))); }
    catch (...) { app.SetNamedValue(L"exe_sha256", JsonValue::CreateNullValue()); }
    app.SetNamedValue(L"windows_app_runtime", JsonValue::CreateNullValue());
    // File versions are usable for both app-local and packaged WinUI runtimes.
    if (auto module = GetModuleHandleW(L"Microsoft.WindowsAppRuntime.dll"))
    {
        wchar_t path[32768]{};
        if (GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path))))
            String(app, L"windows_app_runtime", FileVersion(path));
    }
    using RtlVersion = LONG(WINAPI *)(OSVERSIONINFOW *);
    OSVERSIONINFOW os{sizeof(os)};
    auto rtl = reinterpret_cast<RtlVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    if (rtl && rtl(&os) == 0) Number(windows, L"build", os.dwBuildNumber);
    else windows.SetNamedValue(L"build", JsonValue::CreateNullValue());
    DWORD ubr{}, size = sizeof(ubr);
    OptionalNumber(windows, L"ubr", RegGetValueW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR", RRF_RT_REG_DWORD, nullptr, &ubr, &size) == ERROR_SUCCESS
        ? std::optional<uint32_t>{ubr} : std::optional<uint32_t>{});
    result.SetNamedValue(L"app", app); result.SetNamedValue(L"windows", windows);
    result.SetNamedValue(L"drivers", JsonArray{});
    return result;
}
bool PlaybackRecoveryFailed(JsonObject const &record)
{
    for (auto const &value : record.GetNamedArray(L"events"))
    {
        auto event = value.GetObject();
        if (event.GetNamedString(L"event") != L"playback.control") continue;
        auto data = event.GetNamedObject(L"data");
        if (data.GetNamedString(L"control_action") == L"restore" && data.GetNamedString(L"reason") != L"restored")
            return true;
    }
    return false;
}
void PruneRecords()
{
    auto directory = DiagnosticDataDirectory() / L"Reports";
    std::vector<std::filesystem::directory_entry> entries;
    auto now = std::filesystem::file_time_type::clock::now();
    for (auto const &item : std::filesystem::directory_iterator(directory))
        if (item.is_regular_file() && item.path().extension() == L".json") entries.push_back(item);
    std::sort(entries.begin(), entries.end(), [](auto const &a, auto const &b) { return a.last_write_time() > b.last_write_time(); });
    uintmax_t total{};
    size_t failures{}, successes{};
    for (auto const &entry : entries)
    {
        bool failed = true;
        try {
            auto record = JsonObject::Parse(to_hstring(ReadBounded(entry.path(), RecordLimit)));
            auto outcome = record.GetNamedObject(L"attempt").GetNamedString(L"outcome");
            failed = (outcome != L"success" && outcome != L"user_canceled" && outcome != L"elevation_canceled") ||
                PlaybackRecoveryFailed(record);
        } catch (...) { }
        total += entry.file_size();
        auto count = failed ? ++failures : ++successes;
        if (count > (failed ? 5u : 1u) || total > 10 * MiB || now - entry.last_write_time() > std::chrono::hours(24 * 7))
        {
            std::error_code ignored;
            std::filesystem::remove(entry.path(), ignored);
        }
    }
}
void Persist(JsonObject const &record)
{
    std::scoped_lock lock(RecordsGate);
    auto id = std::wstring(record.GetNamedObject(L"attempt").GetNamedString(L"attempt_id"));
    MemoryRecords[id] = Clone(record);
    if (MemoryRecords.size() > 6)
    {
        auto oldest = std::min_element(MemoryRecords.begin(), MemoryRecords.end(), [](auto const &a, auto const &b) {
            return a.second.GetNamedObject(L"attempt").GetNamedString(L"started_utc") <
                   b.second.GetNamedObject(L"attempt").GetNamedString(L"started_utc");
        });
        MemoryRecords.erase(oldest);
    }
    try
    {
        auto directory = DiagnosticDataDirectory() / L"Reports";
        std::filesystem::create_directories(directory);
        auto data = to_string(record.Stringify());
        if (data.size() > RecordLimit) throw hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
        AtomicWrite(directory / (id + L".json"), data);
        PruneRecords();
    }
    catch (...) { ++RecordWriteFailures; }
}
std::map<std::wstring, JsonObject> LoadRecords()
{
    std::scoped_lock lock(RecordsGate);
    auto records = MemoryRecords;
    auto directory = DiagnosticDataDirectory() / L"Reports";
    if (std::filesystem::exists(directory))
    {
        PruneRecords();
        size_t count{};
        for (auto const &item : std::filesystem::directory_iterator(directory))
        {
            if (++count > 64) break;
            if (!item.is_regular_file() || item.path().extension() != L".json" || item.file_size() > RecordLimit) continue;
            try
            {
                auto record = JsonObject::Parse(to_hstring(ReadBounded(item.path(), RecordLimit)));
                auto id = std::wstring(record.GetNamedObject(L"attempt").GetNamedString(L"attempt_id"));
                if (item.path().filename() == id + L".json" && !records.contains(id)) records[id] = record;
            }
            catch (...) { }
        }
    }
    return records;
}
} // namespace

std::wstring DiagnosticUtc()
{
    SYSTEMTIME t{}; GetSystemTime(&t);
    return std::format(L"{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z", t.wYear, t.wMonth, t.wDay,
                       t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
}
std::filesystem::path DiagnosticDataDirectory()
{
    if (!TestDirectory.empty()) return TestDirectory;
    PWSTR local{};
    check_hresult(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local));
    std::filesystem::path path = std::filesystem::path(local) / L"BluetoothAudioCodecViewer";
    CoTaskMemFree(local);
    return path;
}
void UseDiagnosticTestDirectory()
{
    TestDirectory = std::filesystem::temp_directory_path() / (L"BluetoothAudioCodec-selftest-" + Id());
    std::filesystem::create_directory(TestDirectory);
}
void CleanupDiagnosticTestDirectory() noexcept
{
    if (TestDirectory.empty()) return;
    try { std::filesystem::remove_all(TestDirectory); } catch (...) { }
}

struct DiagnosticAttempt::State
{
    JsonObject record, attempt, environment;
    JsonArray events, snapshots;
    std::map<std::pair<std::wstring, std::wstring>, std::wstring> tags;
    std::map<std::wstring, size_t> tagCounts;
    uint64_t startedTick{GetTickCount64()};
    uint32_t helperPid{}, snapshotOmitted{};
    size_t playbackRecorded{};
    std::optional<uint32_t> exitCode;
    bool finished{}, listening{}, partial{}, synthetic{};
    std::optional<AudioDiagnosticSnapshot> testTopology;
    std::optional<BluetoothDeviceProfiles> testProfiles;
    std::optional<uint32_t> testConnectedCount;
    std::wstring Tag(std::wstring kind, std::wstring const &raw)
    {
        if (raw.empty()) return {};
        auto key = std::pair(kind, raw);
        if (auto it = tags.find(key); it != tags.end()) return it->second;
        auto value = kind + L"-" + std::to_wstring(++tagCounts[kind]);
        tags.emplace(std::move(key), value);
        return value;
    }
    std::wstring DeviceTag(uint64_t address)
    { return address ? Tag(L"device", L"bluetooth:" + std::to_wstring(address)) : L""; }
    JsonObject ProductField(BluetoothProductField const &field)
    {
        JsonObject value;
        auto text = field.value;
        auto first = text.find_first_not_of(L" \t\r\n"), last = text.find_last_not_of(L" \t\r\n");
        text = first == std::wstring::npos ? L"" : text.substr(first, last - first + 1);
        auto source = field.source == L"windows-device-container" || field.source == L"windows-pnp" ? field.source : L"none";
        bool rejected = text.size() > 120 || std::any_of(text.begin(), text.end(), [](wchar_t ch) { return ch < 0x20 || ch == 0x7f; }) ||
                        (!text.empty() && RedactDiagnostics(text) != text);
        bool available = !text.empty() && source != L"none" && !rejected;
        value.SetNamedValue(L"value", available ? JsonValue::CreateStringValue(text) : JsonValue::CreateNullValue());
        String(value, L"source", text.empty() ? L"none" : source);
        String(value, L"status", rejected ? L"redacted" : available ? L"reported" : L"unavailable");
        Boolean(value, L"user_confirmed", false);
        return value;
    }
    JsonObject Profiles(AudioOutput const &output, BluetoothDeviceProfiles const &profiles)
    {
        JsonObject value;
        String(value, L"scope", L"connected-classic-audio-and-default-output");
        String(value, L"status", profiles.partial ? L"partial" : L"collected");
        OptionalNumber(value, L"connected_classic_audio_count", profiles.connectedCount);
        Number(value, L"omitted_count", profiles.omitted + (profiles.devices.size() > 8 ? profiles.devices.size() - 8 : 0));
        value.SetNamedValue(L"default_device_tag", JsonValue::CreateNullValue());
        JsonArray devices;
        for (auto const &profile : profiles.devices)
        {
            if (devices.Size() >= 8) break;
            if (!profile.address) { partial = true; continue; }
            JsonObject device;
            auto tag = DeviceTag(profile.address);
            String(device, L"device_tag", tag);
            auto container = Tag(L"container", Upper(profile.container));
            device.SetNamedValue(L"container_tag", container.empty() ? JsonValue::CreateNullValue() : JsonValue::CreateStringValue(container));
            String(device, L"connection_observation", profile.connection == L"connected" || profile.connection == L"disconnected" ? profile.connection : L"unknown");
            device.SetNamedValue(L"default_output_match", output.address ? JsonValue::CreateBooleanValue(output.address == profile.address) : JsonValue::CreateNullValue());
            if (output.address == profile.address) String(value, L"default_device_tag", tag);
            auto status = profile.metadataStatus;
            String(device, L"metadata_status", status == L"collected" || status == L"timeout" || status == L"failed" || status == L"ambiguous" ? status : L"unavailable");
            device.SetNamedValue(L"model", ProductField(profile.model));
            device.SetNamedValue(L"manufacturer", ProductField(profile.manufacturer));
            devices.Append(device);
        }
        value.SetNamedValue(L"devices", devices);
        return value;
    }
    JsonObject Output(AudioOutput const &output)
    {
        JsonObject value;
        auto endpoint = Tag(L"endpoint", output.id);
        value.SetNamedValue(L"endpoint_tag", endpoint.empty() ? JsonValue::CreateNullValue() : JsonValue::CreateStringValue(endpoint));
        String(value, L"role", L"multimedia");
        String(value, L"transport", output.transport == OutputTransport::Classic ? L"bluetooth-classic" :
            output.transport == OutputTransport::LowEnergy ? L"bluetooth-le" :
            output.transport == OutputTransport::HandsFree ? L"hands-free" :
            output.transport == OutputTransport::Other ? L"other" : L"unknown");
        value.SetNamedValue(L"identity_resolved", output.id.empty() ? JsonValue::CreateNullValue() : JsonValue::CreateBooleanValue(output.address != 0));
        String(value, L"identity_source", output.identitySource == L"remote-devnode" || output.identitySource == L"pnp-container"
            ? output.identitySource : L"none");
        String(value, L"connection_observation", L"unknown");
        String(value, L"connection_scope", L"audio-video-device-set");
        OptionalNumber(value, L"connected_audio_device_count", {});
        if (synthetic && testConnectedCount)
        {
            Number(value, L"connected_audio_device_count", *testConnectedCount);
            String(value, L"connection_observation", *testConnectedCount ? L"connected" : L"disconnected");
        }
        else if (!synthetic)
        {
            try {
                auto connection = ReadBluetoothConnections();
                if (connection.state != ConnectionState::Unavailable && connection.state != ConnectionState::Checking)
                {
                    Number(value, L"connected_audio_device_count", connection.addresses.size());
                    String(value, L"connection_observation", std::find(connection.addresses.begin(), connection.addresses.end(), output.address)
                        != connection.addresses.end() || !connection.addresses.empty() ? L"connected" : L"disconnected");
                    if (output.address && std::find(connection.addresses.begin(), connection.addresses.end(), output.address) != connection.addresses.end())
                        String(value, L"connection_scope", L"target");
                }
            } catch (...) { partial = true; }
        }
        value.SetNamedValue(L"microphone_active", output.microphoneStateKnown
            ? JsonValue::CreateBooleanValue(output.microphoneActive) : JsonValue::CreateNullValue());
        return value;
    }
    void Event(std::wstring_view name, JsonObject const &data = JsonObject{}, std::wstring_view level = L"info")
    {
        JsonObject event;
        String(event, L"run_id", RunId());
        String(event, L"attempt_id", attempt.GetNamedString(L"attempt_id"));
        Number(event, L"seq", events.Size() + 1); String(event, L"utc", DiagnosticUtc());
        Number(event, L"elapsed_ms", GetTickCount64() - startedTick);
        String(event, L"role", L"ui"); Number(event, L"pid", GetCurrentProcessId()); Number(event, L"tid", GetCurrentThreadId());
        String(event, L"event", name); String(event, L"level", level);
        String(event, L"origin", synthetic ? L"synthetic" : L"observed");
        event.SetNamedValue(L"data", data); events.Append(event);
    }
    void Snapshot(AudioOutput const &output, std::wstring_view trigger, AudioDiagnosticSnapshot const *injected = nullptr)
    {
        if (snapshots.Size() >= 8) { ++snapshotOmitted; partial = true; return; }
        JsonObject snapshot;
        auto id = L"snapshot-" + std::to_wstring(snapshots.Size() + 1);
        String(snapshot, L"snapshot_id", id); String(snapshot, L"attempt_id", attempt.GetNamedString(L"attempt_id"));
        String(snapshot, L"trigger", trigger); String(snapshot, L"captured_start_utc", DiagnosticUtc());
        auto profiles = synthetic ? testProfiles.value_or(BluetoothDeviceProfiles{}) : CaptureBluetoothDeviceProfiles(output);
        std::vector<uint64_t> connected;
        for (auto const &profile : profiles.devices) connected.push_back(profile.address);
        auto topology = injected ? *injected : synthetic && testTopology ? *testTopology : CaptureAudioDiagnosticSnapshot(output, connected);
        auto outputValue = Output(output);
        // Use the same connection observation for the profile list and snapshot output.
        OptionalNumber(outputValue, L"connected_audio_device_count", profiles.connectedCount);
        String(outputValue, L"connection_scope", L"audio-video-device-set");
        String(outputValue, L"connection_observation", !profiles.connectedCount ? L"unknown" : *profiles.connectedCount ? L"connected" : L"disconnected");
        for (auto const &profile : profiles.devices)
            if (profile.address == output.address && profile.connection == L"connected")
            { String(outputValue, L"connection_scope", L"target"); String(outputValue, L"connection_observation", L"connected"); }
        snapshot.SetNamedValue(L"output", outputValue);
        snapshot.SetNamedValue(L"device_profiles", Profiles(output, profiles));
        partial = partial || profiles.partial || profiles.omitted != 0 || profiles.devices.size() > 8;
        partial = partial || topology.partial;
        JsonArray nodes, edges;
        std::vector<std::wstring> nodeTags;
        for (auto const &item : topology.nodes)
        {
            JsonObject node;
            auto kind = item.kind == L"endpoint" ? L"endpoint" : item.kind == L"remote-device" ? L"device" :
                item.kind == L"a2dp-service" ? L"service" : L"adapter";
            // The software endpoint uses the Core Audio identity for all snapshots.
            auto address = item.kind == L"remote-device" ? BluetoothAddressFromInstance(item.identity) : 0;
            auto tag = address ? DeviceTag(address) : Tag(kind, item.kind == L"endpoint" ? output.id : item.identity);
            nodeTags.push_back(tag); String(node, L"tag", tag); String(node, L"kind", item.kind);
            auto container = Tag(L"container", Upper(item.container));
            node.SetNamedValue(L"container_tag", container.empty() ? JsonValue::CreateNullValue() : JsonValue::CreateStringValue(container));
            String(node, L"api", item.api);
            String(node, L"status", item.error ? L"failed" : L"collected");
            if (item.error) node.SetNamedValue(L"error", Error(item.api, *item.error, L"snapshot", L"node-unavailable", L"configret"));
            else node.SetNamedValue(L"error", JsonValue::CreateNullValue());
            nodes.Append(node);
            if (!item.driverVersion.empty())
            {
                auto drivers = environment.GetNamedArray(L"drivers");
                bool known = std::any_of(drivers.begin(), drivers.end(), [&](auto const &driver) {
                    return driver.GetObject().GetNamedString(L"node_tag") == tag;
                });
                if (!known && drivers.Size() < 32)
                {
                    JsonObject driver;
                    String(driver, L"node_tag", tag);
                    String(driver, L"class", item.driverClass == L"Bluetooth" || item.driverClass == L"MEDIA" ||
                        item.driverClass == L"AudioEndpoint" ? item.driverClass : L"System");
                    String(driver, L"provider", RedactDiagnostics(item.driverProvider.empty() ? L"unavailable" : item.driverProvider.substr(0, 80)));
                    String(driver, L"version", RedactDiagnostics(item.driverVersion.substr(0, 80)));
                    drivers.Append(driver);
                }
            }
        }
        for (auto const &item : topology.edges)
        {
            if (item.from >= nodeTags.size() || item.to >= nodeTags.size()) { partial = true; continue; }
            JsonObject edge;
            String(edge, L"from", nodeTags[item.from]); String(edge, L"to", nodeTags[item.to]); String(edge, L"relation", item.relation);
            String(edge, L"origin", synthetic ? L"synthetic" : L"observed"); edges.Append(edge);
        }
        snapshot.SetNamedValue(L"nodes", nodes); snapshot.SetNamedValue(L"edges", edges);
        String(snapshot, L"captured_end_utc", DiagnosticUtc()); snapshots.Append(snapshot);
        JsonObject data; String(data, L"snapshot_id", id); Event(L"snapshot.captured", data);
    }
    void Save()
    {
        record.SetNamedValue(L"attempt", attempt); record.SetNamedValue(L"environment", environment);
        record.SetNamedValue(L"events", events); record.SetNamedValue(L"snapshots", snapshots);
        Boolean(record, L"partial", partial); Boolean(record, L"synthetic", synthetic);
        Persist(record);
    }
};
DiagnosticAttempt::DiagnosticAttempt() : m_state(std::make_unique<State>()) {}
std::unique_ptr<DiagnosticAttempt> DiagnosticAttempt::Begin(AudioOutput const &output) noexcept
{
    try
    {
        auto value = std::unique_ptr<DiagnosticAttempt>(new DiagnosticAttempt);
        auto &s = *value->m_state;
        s.environment = Environment();
        String(s.attempt, L"attempt_id", Id()); String(s.attempt, L"run_id", RunId());
        String(s.attempt, L"started_utc", DiagnosticUtc()); s.attempt.SetNamedValue(L"finished_utc", JsonValue::CreateNullValue());
        String(s.attempt, L"outcome", L"interrupted_unknown"); String(s.attempt, L"reason", L"interrupted-unknown");
        s.attempt.SetNamedValue(L"target", s.Output(output)); s.attempt.SetNamedValue(L"error", JsonValue::CreateNullValue());
        s.attempt.SetNamedValue(L"helper_exit_code", JsonValue::CreateNullValue());
        s.Event(L"attempt.started"); s.Save();
        s.Snapshot(output, L"start"); s.Save();
        return value;
    }
    catch (...) { ++RecordWriteFailures; return {}; }
}
DiagnosticAttempt::~DiagnosticAttempt() = default;
std::wstring DiagnosticAttempt::AttemptId() const { return std::wstring(m_state->attempt.GetNamedString(L"attempt_id")); }
void DiagnosticAttempt::Preflight(AudioOutput const &output, std::wstring_view reason) noexcept
{
    try { JsonObject data; String(data, L"reason", Reason(reason)); data.SetNamedValue(L"output", m_state->Output(output));
        m_state->Event(L"preflight.completed", data); m_state->Save(); } catch (...) { ++RecordWriteFailures; }
}
void DiagnosticAttempt::HelperStarted(uint32_t pid) noexcept
{
    try { m_state->helperPid = pid; JsonObject data; Number(data, L"helper_pid", pid);
        m_state->Event(L"helper.started", data); m_state->Save(); } catch (...) { ++RecordWriteFailures; }
}
void DiagnosticAttempt::Listening() noexcept
{
    try { m_state->listening = true; m_state->Event(L"helper.connected");
        m_state->Event(L"etw.started"); m_state->Save(); } catch (...) { ++RecordWriteFailures; }
}
void DiagnosticAttempt::HelperExited(uint32_t code) noexcept { m_state->exitCode = code; }
void DiagnosticAttempt::Playback(std::span<PlaybackControlEvent const> events) noexcept
{
    try
    {
        auto &s = *m_state;
        for (; s.playbackRecorded < events.size(); ++s.playbackRecorded)
        {
            auto const &control = events[s.playbackRecorded];
            JsonObject detail;
            String(detail, L"control_action", control.action); String(detail, L"reason", control.reason);
            Number(detail, L"control_session", control.session);
            OptionalNumber(detail, L"control_error", control.error);
            s.Event(L"playback.control", detail);
        }
        s.Save();
    }
    catch (...) { ++RecordWriteFailures; }
}
void DiagnosticAttempt::Finish(DetectionResult const &result, AudioOutput const &output, CaptureDiagnostics const &capture,
                               std::optional<uint32_t> error, std::wstring_view api) noexcept
{
    try
    {
        auto &s = *m_state;
        if (s.finished) return;
        Playback(result.playback);
        bool timedOut = error && *error == static_cast<uint32_t>(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
        auto reason = timedOut ? L"helper-timeout" : error ? L"internal-error" : result.observation ? L"success" : result.elevationCanceled ? L"elevation-canceled" :
            result.canceled ? L"user-canceled" : result.reason.empty() ? L"no-matching-event" : result.reason.c_str();
        auto outcome = error ? (s.helperPid ? L"helper_failed" : L"internal_error") : result.elevationCanceled ? L"elevation_canceled" :
            result.canceled ? L"user_canceled" : result.observation ? L"success" :
            !s.helperPid ? L"preflight_blocked" : L"timeout";
        if (result.reason == L"helper-exited" || result.errorCode) outcome = L"helper_failed";
        if (timedOut) outcome = L"timeout";
        if (!error && !result.errorCode && !result.canceled && !result.elevationCanceled && !result.observation &&
            !capture.started && !s.listening && (result.reason == L"not-bluetooth" || result.reason == L"target-unresolved" ||
            result.reason == L"le-audio" || result.reason == L"hands-free")) outcome = L"preflight_blocked";
        s.partial = s.partial || error.has_value() || (!s.listening && s.helperPid != 0) ||
                    (s.helperPid != 0 && !capture.statisticsAvailable);
        if (s.listening || capture.started)
            s.partial = s.partial || !capture.stopStatus || *capture.stopStatus != ERROR_SUCCESS ||
                        !capture.eventsLost || !capture.logBuffersLost || !capture.realtimeBuffersLost ||
                        capture.eventsLost.value_or(0) != 0 || capture.logBuffersLost.value_or(0) != 0 ||
                        capture.realtimeBuffersLost.value_or(0) != 0;
        s.Snapshot(output, std::wstring_view(outcome) == L"success" || std::wstring_view(outcome).find(L"canceled") != std::wstring_view::npos ? L"end" : L"failure");
        if (s.listening) s.Event(L"etw.stopped");
        if (result.observation)
        {
            JsonObject data; Number(data, L"codec_standard", result.observation->standard);
            Number(data, L"codec_vendor", result.observation->vendor); Number(data, L"codec_id", result.observation->codec);
            s.Event(L"codec.observed", data);
        }
        auto code = result.errorCode ? result.errorCode : error;
        if (code) s.attempt.SetNamedValue(L"error", Error(result.errorApi.empty() ? api : result.errorApi,
            *code, s.helperPid ? L"helper" : L"detection", Reason(reason)));
        String(s.attempt, L"outcome", outcome); String(s.attempt, L"reason", Reason(reason));
        String(s.attempt, L"finished_utc", DiagnosticUtc()); OptionalNumber(s.attempt, L"helper_exit_code", s.exitCode);
        JsonObject data; String(data, L"outcome", outcome); String(data, L"reason", Reason(reason));
        s.Event(L"attempt.finished", data, code ? L"error" : L"info");
        JsonObject stats;
        Boolean(stats, L"started", capture.started || s.listening);
        OptionalNumber(stats, L"providers_enabled", capture.statisticsAvailable ? std::optional<uint32_t>{capture.providersEnabled} : std::nullopt);
        OptionalNumber(stats, L"events", capture.statisticsAvailable ? std::optional<uint32_t>{capture.events} : std::nullopt);
        OptionalNumber(stats, L"metadata_failures", capture.statisticsAvailable ? std::optional<uint32_t>{capture.metadataFailures} : std::nullopt);
        OptionalNumber(stats, L"streaming_events", capture.statisticsAvailable ? std::optional<uint32_t>{capture.streamingEvents} : std::nullopt);
        OptionalNumber(stats, L"configuration_events", capture.statisticsAvailable ? std::optional<uint32_t>{capture.configurationEvents} : std::nullopt);
        OptionalNumber(stats, L"parse_failures", capture.statisticsAvailable ? std::optional<uint32_t>{capture.parseFailures} : std::nullopt);
        OptionalNumber(stats, L"rejected", capture.statisticsAvailable ? std::optional<uint32_t>{capture.rejected} : std::nullopt);
        OptionalNumber(stats, L"stop_status", capture.stopStatus);
        OptionalNumber(stats, L"first_metadata_error", capture.firstMetadataError);
        OptionalNumber(stats, L"first_metadata_event_flags", capture.firstMetadataEventFlags);
        s.record.SetNamedValue(L"capture", stats);
        JsonObject loss; Number(loss, L"writer_dropped", RecordWriteFailures.load());
        OptionalNumber(loss, L"ipc_dropped", !s.helperPid || capture.statisticsAvailable ? std::optional<uint32_t>{0} : std::nullopt);
        Number(loss, L"events_omitted", 0); Number(loss, L"snapshots_omitted", s.snapshotOmitted);
        OptionalNumber(loss, L"etw_events_lost", capture.eventsLost); OptionalNumber(loss, L"etw_log_buffers_lost", capture.logBuffersLost);
        OptionalNumber(loss, L"etw_realtime_buffers_lost", capture.realtimeBuffersLost);
        s.record.SetNamedValue(L"loss", loss); s.finished = true; s.Save();
    }
    catch (...) { ++RecordWriteFailures; }
}
namespace
{
struct RuntimeLogAttachment
{
    JsonObject info;
    std::optional<std::string> text;
};
RuntimeLogAttachment RuntimeLog(bool requested, std::function<std::wstring()> const &reader = {})
{
    RuntimeLogAttachment result;
    String(result.info, L"status", L"not_requested");
    String(result.info, L"scope", L"export-time-recent-history");
    Boolean(result.info, L"truncated", false);
    result.info.SetNamedValue(L"captured_start_utc", JsonValue::CreateNullValue());
    result.info.SetNamedValue(L"captured_end_utc", JsonValue::CreateNullValue());
    result.info.SetNamedValue(L"error", JsonValue::CreateNullValue());
    if (!requested) return result;
    String(result.info, L"captured_start_utc", DiagnosticUtc());
    try
    {
        auto text = to_string(RedactDiagnostics(reader ? reader() : CollectRuntimeDiagnostics()));
        text = "Runtime log supplement\nScope: recent history and state read at report export, not a failure-time snapshot.\n"
               "Text log tags may differ from structured report tags.\n"
               "Pattern filtering is not an anonymity guarantee. Review this file before sharing.\n\n" + text;
        constexpr size_t limit = 512 * 1024;
        if (text.size() > limit)
        {
            std::string const marker = "\n\n[Middle omitted to fit the 512 KiB runtime-log limit; newest tail retained.]\n\n";
            size_t head = 16 * 1024;
            auto continuation = [&](size_t i) { return (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80; };
            while (head && continuation(head)) --head;
            auto tail = text.size() - (limit - head - marker.size());
            while (tail < text.size() && continuation(tail)) ++tail;
            text = text.substr(0, head) + marker + text.substr(tail);
            Boolean(result.info, L"truncated", true);
        }
        result.text = std::move(text);
        String(result.info, L"status", L"included");
    }
    catch (hresult_error const &error)
    {
        String(result.info, L"status", L"failed");
        result.info.SetNamedValue(L"error", Error(L"CollectRuntimeDiagnostics", static_cast<uint32_t>(error.code().value),
            L"runtime-log", L"runtime-log-unavailable"));
    }
    catch (...)
    {
        String(result.info, L"status", L"failed");
        result.info.SetNamedValue(L"error", Error(L"CollectRuntimeDiagnostics", static_cast<uint32_t>(E_FAIL), L"runtime-log", L"runtime-log-unavailable"));
    }
    String(result.info, L"captured_end_utc", DiagnosticUtc());
    return result;
}
JsonObject SessionRecord()
{
    JsonObject record;
    String(record, L"scope", L"session");
    record.SetNamedValue(L"environment", Environment());
    record.SetNamedValue(L"events", JsonArray{}); record.SetNamedValue(L"snapshots", JsonArray{});
    Boolean(record, L"partial", true);
    return record;
}
DiagnosticReport BuildReport(JsonObject const &record, bool includeRuntimeLog = false,
                             std::function<std::wstring()> const &logReader = {})
{
    DiagnosticReport report;
    report.id = Id();
    bool session = record.GetNamedString(L"scope", L"attempt") == L"session";
    auto attempt = session ? JsonObject{} : Clone(record.GetNamedObject(L"attempt"));
    auto outcome = session ? hstring(L"not_run") : attempt.GetNamedString(L"outcome");
    bool interrupted = outcome == L"interrupted_unknown";
    auto environment = Clone(record.GetNamedObject(L"environment"));
    report.buildId = environment.GetNamedObject(L"app").GetNamedString(L"build_id");
    report.reason = session ? L"no-detection-record" : std::wstring(attempt.GetNamedString(L"reason"));
    auto runtimeLog = RuntimeLog(includeRuntimeLog, logReader);
    auto common = [&](JsonObject const &object) {
        String(object, L"schema_version", L"1.1"); String(object, L"report_id", report.id);
    };
    common(environment);
    JsonObject attempts, snapshots, collection, manifest;
    common(attempts); common(snapshots); common(collection); common(manifest);
    JsonArray list; if (!session) list.Append(attempt); attempts.SetNamedValue(L"attempts", list);
    snapshots.SetNamedValue(L"snapshots", record.GetNamedArray(L"snapshots"));
    auto events = record.GetNamedArray(L"events");
    std::string lines;
    for (auto const &item : events)
    {
        auto event = Clone(item.GetObject()); common(event);
        auto line = to_string(event.Stringify());
        if (line.size() > 16 * 1024 || events.Size() > 10000) throw hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
        lines += line + "\n";
    }
    JsonObject capture;
    if (record.HasKey(L"capture")) capture = Clone(record.GetNamedObject(L"capture"));
    else
    {
        bool started = std::any_of(events.begin(), events.end(), [](auto const &e) { return e.GetObject().GetNamedString(L"event") == L"etw.started"; });
        Boolean(capture, L"started", started);
        for (auto key : {L"providers_enabled", L"events", L"metadata_failures", L"streaming_events", L"configuration_events", L"parse_failures", L"rejected", L"stop_status"})
            capture.SetNamedValue(key, JsonValue::CreateNullValue());
    }
    // Incomplete topology/driver enumeration is explicitly partial. Collection
    // does not claim that API results formed an atomic system snapshot.
    bool partial = session || record.GetNamedBoolean(L"partial", true) || interrupted || RecordWriteFailures.load() != 0 ||
                   runtimeLog.info.GetNamedString(L"status") == L"failed";
    auto drivers = environment.GetNamedArray(L"drivers");
    if (drivers.Size() == 0) partial = true;
    String(collection, L"completeness", partial ? L"partial" : L"complete");
    JsonArray components;
    auto component = [&](wchar_t const *name, wchar_t const *status, wchar_t const *reason) {
        JsonObject item; String(item, L"name", name); String(item, L"status", status); String(item, L"reason", reason); components.Append(item);
    };
    component(L"events", session ? L"not_requested" : interrupted ? L"partial" : L"collected",
              session ? L"no-detection-record" : interrupted ? L"interrupted-unknown" : L"bounded-events");
    component(L"snapshots", session ? L"not_requested" : record.GetNamedBoolean(L"partial", true) ? L"partial" : L"collected",
              session ? L"no-detection-record" : L"target-topology");
    component(L"drivers", drivers.Size() == 0 ? L"unavailable" : L"collected", L"target-driver-chain");
    bool started = capture.GetNamedBoolean(L"started");
    component(L"etw", !started ? L"not_started" : capture.GetNamedValue(L"events").ValueType() == JsonValueType::Null ? L"partial" : L"collected",
              !started ? L"preflight-or-helper-not-ready" : L"authenticated-helper-summary");
    collection.SetNamedValue(L"components", components); collection.SetNamedValue(L"capture", capture);
    JsonObject loss;
    if (record.HasKey(L"loss")) loss = Clone(record.GetNamedObject(L"loss"));
    else for (auto key : {L"writer_dropped", L"ipc_dropped", L"events_omitted", L"snapshots_omitted", L"etw_events_lost", L"etw_log_buffers_lost", L"etw_realtime_buffers_lost"})
        loss.SetNamedValue(key, JsonValue::CreateNullValue());
    if (RecordWriteFailures.load()) Number(loss, L"writer_dropped", RecordWriteFailures.load());
    collection.SetNamedValue(L"loss", loss);
    if (session)
        report.summary = L"Bluetooth Audio Codec problem report\nFormat: bac-report/1.1\nReport ID: " + report.id +
            L"\nBuild ID: " + report.buildId + L"\nScope: application context; no detection selected\nExported: " + DiagnosticUtc() +
            L"\nNo detection, failure-time device state or ETW capture is claimed. Environment was read while preparing this report.\n";
    else
    {
        auto target = attempt.GetNamedObject(L"target");
        report.summary = L"Bluetooth Audio Codec problem report\nFormat: bac-report/1.1\nReport ID: " + report.id +
            L"\nAttempt ID: " + std::wstring(attempt.GetNamedString(L"attempt_id")) + L"\nBuild ID: " + report.buildId +
            L"\nCaptured: " + std::wstring(attempt.GetNamedString(L"started_utc")) +
            L"\nExported: " + DiagnosticUtc() + L"\nResult: " + std::wstring(outcome) + L" / " + report.reason +
            L"\nDefault output transport: " + std::wstring(target.GetNamedString(L"transport")) +
            L"\nIdentity resolved: " + std::wstring(target.GetNamedValue(L"identity_resolved").Stringify()) +
            L"\nIdentity source: " + std::wstring(target.GetNamedString(L"identity_source")) +
            L"\nConnection observation: " + std::wstring(target.GetNamedString(L"connection_observation")) +
            L" (" + std::wstring(target.GetNamedString(L"connection_scope", L"unknown")) + L")" +
            L"\nETW: " + (started ? std::wstring(L"started; see collection-status.json for available counters") : std::wstring(L"not started")) +
            L"\nCompleteness: " + (partial ? std::wstring(L"partial") : std::wstring(L"complete")) +
            L"\n\nThe device state belongs to this detection, not to the later export.\n"
            L"Structured files exclude device nicknames, raw addresses, original PnP/container IDs, raw ETL, dumps and audio.\n"
            L"Review all report files before sharing. GitHub issue attachments are public and upload when selected.\n";
    }
    report.summary += L"\nRuntime log supplement: " + std::wstring(runtimeLog.info.GetNamedString(L"status")) +
        L"\nRuntime log scope: export-time recent history; separate from the detection snapshots.\n";
    if (runtimeLog.text)
        report.summary += L"Review runtime-log.txt manually before sharing. Pattern filtering does not guarantee anonymity.\n";
    if (runtimeLog.info.GetNamedBoolean(L"truncated")) report.summary += L"Runtime log was shortened to 512 KiB; newest tail retained.\n";
    for (auto const &entry : record.GetNamedArray(L"snapshots"))
    {
        auto snapshot = entry.GetObject();
        if (!snapshot.HasKey(L"device_profiles")) continue; // Older local records remain exportable.
        auto profiles = snapshot.GetNamedObject(L"device_profiles");
        report.summary += L"\nDevice profiles (" + std::wstring(snapshot.GetNamedString(L"trigger")) + L", " +
            std::wstring(profiles.GetNamedString(L"status")) + L"):\n";
        auto deviceList = profiles.GetNamedArray(L"devices");
        if (!deviceList.Size()) report.summary += L"No connected Classic audio device or resolved default Bluetooth target was observed.\n";
        for (auto const &item : deviceList)
        {
            auto device = item.GetObject();
            auto model = device.GetNamedObject(L"model"), manufacturer = device.GetNamedObject(L"manufacturer");
            auto describe = [](JsonObject const &field) -> std::wstring {
                return field.GetNamedValue(L"value").ValueType() == JsonValueType::String
                    ? std::wstring(field.GetNamedString(L"value")) + L" [" + std::wstring(field.GetNamedString(L"source")) + L"; not user-confirmed]"
                    : L"unknown [" + std::wstring(field.GetNamedString(L"status")) + L"]";
            };
            report.summary += std::wstring(device.GetNamedString(L"device_tag")) + L": model=" + describe(model) +
                L"; manufacturer=" + describe(manufacturer) + L"; connection=" + std::wstring(device.GetNamedString(L"connection_observation")) +
                L"; default-output-match=" + std::wstring(device.GetNamedValue(L"default_output_match").Stringify()) + L"\n";
        }
        report.summary += L"Scope: connected Classic Audio/Video devices plus resolved default target; other LE devices may be absent.\n";
    }
    if (record.GetNamedBoolean(L"synthetic", false)) report.summary = L"SYNTHETIC TEST REPORT\n" + report.summary;
    report.files = {{"summary.txt", to_string(report.summary)}, {"environment.json", to_string(environment.Stringify()) + "\n"},
        {"attempts.json", to_string(attempts.Stringify()) + "\n"}, {"events.jsonl", std::move(lines)},
        {"snapshots.json", to_string(snapshots.Stringify()) + "\n"}, {"collection-status.json", to_string(collection.Stringify()) + "\n"}};
    if (runtimeLog.text) report.files.emplace_back("runtime-log.txt", *runtimeLog.text);
    String(manifest, L"format", L"bac-report"); String(manifest, L"audience", L"public"); String(manifest, L"created_utc", DiagnosticUtc());
    String(manifest, L"origin", record.GetNamedBoolean(L"synthetic", false) ? L"synthetic" : L"observed");
    Boolean(manifest, L"fixture_reuse_allowed", false);
    String(manifest, L"scope", session ? L"session" : L"attempt");
    manifest.SetNamedValue(L"runtime_log", runtimeLog.info);
    String(manifest, L"redaction_policy", runtimeLog.text ? L"public-fields-v1-with-filtered-runtime-log" : L"public-fields-v1");
    JsonArray inventory;
    size_t total{};
    for (auto const &[name, bytes] : report.files)
    {
        if (bytes.size() > 2 * MiB) throw hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
        total += bytes.size();
        JsonObject item; String(item, L"path", to_hstring(name)); Number(item, L"bytes", bytes.size());
        String(item, L"sha256", to_hstring(Sha256(bytes))); inventory.Append(item);
    }
    if (total > 4 * MiB) throw hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
    manifest.SetNamedValue(L"files", inventory);
    report.files.insert(report.files.begin(), {"manifest.json", to_string(manifest.Stringify()) + "\n"});
    auto utc = DiagnosticUtc();
    utc.erase(std::remove_if(utc.begin(), utc.end(), [](wchar_t c) { return c == L'-' || c == L':'; }), utc.end());
    report.filename = L"bac-report-" + utc.substr(0, 15) + L"Z-" + report.id + L".zip";
    return report;
}
// ZIP32, method=stored. Seven core files and an optional bounded text log.
// Checksums and headers are verified independently with Python's zipfile in tests.
std::string Zip(DiagnosticReport const &report)
{
    std::string bytes, central;
    auto word = [](std::string &out, uint32_t value, size_t count) {
        for (size_t i = 0; i < count; ++i) out.push_back(static_cast<char>(value >> (8 * i)));
    };
    auto crc = [](std::string_view text) {
        uint32_t value = 0xffffffff;
        for (unsigned char ch : text)
        {
            value ^= ch;
            for (int i = 0; i < 8; ++i) value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1u)));
        }
        return ~value;
    };
    if (report.files.size() != 7 && report.files.size() != 8) throw hresult_invalid_argument();
    std::set<std::string> expected{"manifest.json", "summary.txt", "environment.json", "attempts.json", "events.jsonl", "snapshots.json", "collection-status.json"};
    if (report.files.size() == 8) expected.insert("runtime-log.txt");
    for (auto const &[name, data] : report.files)
    {
        if (!expected.erase(name) || data.size() > 2 * MiB) throw hresult_invalid_argument();
        auto offset = static_cast<uint32_t>(bytes.size()), checksum = crc(data), size = static_cast<uint32_t>(data.size());
        word(bytes, 0x04034b50, 4); word(bytes, 20, 2); word(bytes, 0x800, 2); word(bytes, 0, 2);
        word(bytes, 0, 2); word(bytes, 0x21, 2); word(bytes, checksum, 4); word(bytes, size, 4); word(bytes, size, 4);
        word(bytes, static_cast<uint32_t>(name.size()), 2); word(bytes, 0, 2); bytes += name; bytes += data;
        word(central, 0x02014b50, 4); word(central, 20, 2); word(central, 20, 2); word(central, 0x800, 2); word(central, 0, 2);
        word(central, 0, 2); word(central, 0x21, 2); word(central, checksum, 4); word(central, size, 4); word(central, size, 4);
        word(central, static_cast<uint32_t>(name.size()), 2); word(central, 0, 2); word(central, 0, 2); word(central, 0, 2);
        word(central, 0, 2); word(central, 0, 4); word(central, offset, 4); central += name;
    }
    auto offset = static_cast<uint32_t>(bytes.size()); bytes += central;
    word(bytes, 0x06054b50, 4); word(bytes, 0, 2); word(bytes, 0, 2);
    word(bytes, static_cast<uint32_t>(report.files.size()), 2); word(bytes, static_cast<uint32_t>(report.files.size()), 2);
    word(bytes, static_cast<uint32_t>(central.size()), 4); word(bytes, offset, 4); word(bytes, 0, 2);
    if (bytes.size() > 5 * MiB) throw hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
    return bytes;
}
} // namespace

std::vector<DiagnosticAttemptInfo> RecentDiagnosticAttempts()
{
    std::vector<DiagnosticAttemptInfo> result;
    for (auto const &[id, record] : LoadRecords())
    {
        auto attempt = record.GetNamedObject(L"attempt");
        if (record.GetNamedBoolean(L"synthetic", false) && TestDirectory.empty()) continue;
        result.push_back({id, std::wstring(attempt.GetNamedString(L"started_utc")),
            std::wstring(attempt.GetNamedString(L"outcome")), std::wstring(attempt.GetNamedString(L"reason"))});
    }
    std::sort(result.begin(), result.end(), [](auto const &a, auto const &b) { return a.started > b.started; });
    return result;
}
DiagnosticReport PrepareDiagnosticReport(std::wstring const &attemptId, bool includeRuntimeLog)
{
    if (attemptId.empty()) return BuildReport(SessionRecord(), includeRuntimeLog);
    auto records = LoadRecords();
    auto it = records.find(attemptId);
    if (it == records.end()) throw hresult_error(HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
    return BuildReport(it->second, includeRuntimeLog);
}
std::wstring ChooseDiagnosticReportPath(HWND owner, std::wstring const &filename)
{
    com_ptr<IFileSaveDialog> dialog;
    check_hresult(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IFileSaveDialog), dialog.put_void()));
    DWORD flags{}; check_hresult(dialog->GetOptions(&flags));
    check_hresult(dialog->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_DONTADDTORECENT));
    auto fileType = Localize(L"ReportZipFileType");
    COMDLG_FILTERSPEC types[]{{fileType.c_str(), L"*.zip"}};
    check_hresult(dialog->SetFileTypes(1, types)); check_hresult(dialog->SetDefaultExtension(L"zip"));
    check_hresult(dialog->SetFileName(filename.c_str()));
    auto result = dialog->Show(owner);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {};
    check_hresult(result);
    com_ptr<IShellItem> item; check_hresult(dialog->GetResult(item.put()));
    PWSTR path{}; check_hresult(item->GetDisplayName(SIGDN_FILESYSPATH, &path));
    std::wstring value(path); CoTaskMemFree(path); return value;
}
void SaveDiagnosticReport(DiagnosticReport const &report, std::wstring const &path)
{
    auto destination = std::filesystem::absolute(path).lexically_normal();
    auto root = DiagnosticDataDirectory().lexically_normal().wstring() + L"\\";
    if (Upper(destination.wstring()).starts_with(Upper(root)))
        throw hresult_invalid_argument(L"Save the report outside the application's internal log folder.");
    AtomicWrite(destination, Zip(report));
}
std::wstring DiagnosticGitHubUrl(DiagnosticReport const &report)
{
    auto encode = [](std::wstring const &text) {
        auto utf8 = to_string(text); std::wstring result;
        for (unsigned char ch : utf8)
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~')
                result.push_back(ch);
            else result += std::format(L"%{:02X}", ch);
        return result;
    };
    // The generic new-issue URL also works before templates are published.
    // Only short allowlisted metadata enters the URL. The full preview is copied separately.
    auto body = L"Report ID: " + report.id + L"\nBuild ID: " + report.buildId + L"\nReason: " + report.reason +
        L"\n\nWhat happened / 现象：\n\nSteps / 复现步骤：\n\nHeadset model / 耳机型号：\n\n"
        L"Attach the reviewed bac-report ZIP below. Files upload immediately when selected and are public. No raw ETL/dump.\n";
    auto url = L"https://github.com/A-BenLi06/Bluetooth-Audio-Codec-Viewer-WinUI3/issues/new?title=" +
        encode(L"[Report] " + report.reason) + L"&body=" + encode(body);
    if (url.size() > 1800)
        return L"https://github.com/A-BenLi06/Bluetooth-Audio-Codec-Viewer-WinUI3/issues/new?body=" + encode(L"Report ID: " + report.id);
    return url;
}
void ShowDiagnosticReportInFolder(std::wstring const &path)
{
    auto item = ILCreateFromPathW(path.c_str());
    if (!item) throw hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    auto result = SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
    ILFree(item); check_hresult(result);
}

int RunDiagnosticReportTests(std::wstring const &directory)
{
    std::filesystem::create_directories(directory);
    JsonObject sampleRecord;
    AudioOutput output;
    output.id = L"{0.0.0.00000000}.{SYNTHETIC-ENDPOINT}";
    output.name = L"Alice private headset";
    output.transport = OutputTransport::Other;
    AudioDiagnosticSnapshot topology;
    topology.nodes = {{L"SWD\\MMDEVAPI\\synthetic", L"endpoint", L"synthetic-container", L"CM_Get_DevNode_PropertyW"},
        {L"BTHENUM\\{0000110B-0000-1000-8000-00805F9B34FB}\\112233AABBCC_C00000000", L"a2dp-service", L"synthetic-container", L"CM_Get_DevNode_PropertyW"},
        {L"BTHENUM\\DEV_112233AABBCC\\synthetic", L"remote-device", L"synthetic-container", L"CM_Get_DevNode_PropertyW"},
        {L"INTELAUDIO\\synthetic-private-path", L"audio-adapter", L"", L"IDeviceTopology"}};
    topology.edges = {{0, 1, L"pnp_parent"}, {1, 2, L"same_container"}, {0, 3, L"audio_topology"}};
    for (auto const &test : {L"preflight", L"canceled", L"elevation-canceled", L"helper-error", L"success", L"interrupted",
                            L"listening-canceled", L"events-lost", L"helper-timeout", L"profile-output-change",
                            L"profile-unknown", L"profile-redacted", L"profile-failed", L"profile-bounded", L"playback-control"})
    {
        auto testOutput = output;
        if (std::wstring_view(test) != L"preflight" && std::wstring_view(test) != L"interrupted")
        {
            testOutput.transport = OutputTransport::Classic;
            testOutput.address = 0x112233aabbccULL;
            testOutput.identitySource = L"pnp-container";
            testOutput.microphoneStateKnown = true;
        }
        auto value = std::unique_ptr<DiagnosticAttempt>(new DiagnosticAttempt);
        auto &s = *value->m_state;
        s.synthetic = true; s.testTopology = topology; s.testConnectedCount = 2; s.environment = Environment();
        BluetoothDeviceProfiles profiles;
        profiles.connectedCount = 2;
        profiles.devices = {{0x112233aabbccULL, L"synthetic-container", {L"HD815", L"windows-device-container"},
            {L"Synthetic Audio", L"windows-device-container"}, L"connected", L"collected"},
            {0x223344bbccddULL, L"second-container", {}, {L"Generic PnP vendor", L"windows-pnp"}, L"connected", L"unavailable"}};
        auto name = std::wstring_view(test);
        if (name == L"profile-unknown") { profiles.devices[0].model = {}; profiles.devices[0].manufacturer = {}; }
        if (name == L"profile-redacted")
        { profiles.devices[0].model = {L"112233AABBCC", L"windows-device-container"}; profiles.devices[0].manufacturer = {L"owner@example.com", L"windows-device-container"}; }
        if (name == L"profile-failed") { profiles.partial = true; profiles.devices[0].metadataStatus = L"timeout"; }
        if (name == L"profile-bounded")
        { profiles.partial = true; profiles.connectedCount = 12; profiles.omitted = 4;
          for (uint64_t i = 0; i < 6; ++i) profiles.devices.push_back({0x334455ccdd00ULL + i, L"", {}, {}, L"connected", L"unavailable"}); }
        s.testProfiles = profiles;
        String(s.attempt, L"attempt_id", Id()); String(s.attempt, L"run_id", RunId()); String(s.attempt, L"started_utc", DiagnosticUtc());
        s.attempt.SetNamedValue(L"finished_utc", JsonValue::CreateNullValue()); String(s.attempt, L"outcome", L"interrupted_unknown");
        String(s.attempt, L"reason", L"interrupted-unknown"); s.attempt.SetNamedValue(L"target", s.Output(testOutput));
        s.attempt.SetNamedValue(L"error", JsonValue::CreateNullValue()); s.attempt.SetNamedValue(L"helper_exit_code", JsonValue::CreateNullValue());
        s.Event(L"attempt.started"); s.Snapshot(testOutput, L"start", &topology);
        DetectionResult result; result.reason = L"not-bluetooth";
        value->Preflight(testOutput, std::wstring_view(test) == L"preflight" ? L"not-bluetooth" : L"ready");
        if (std::wstring_view(test) == L"canceled") result.canceled = true;
        else if (std::wstring_view(test) == L"elevation-canceled") result.elevationCanceled = true;
        else if (std::wstring_view(test) == L"helper-error") { value->HelperStarted(123); result.errorCode = E_ACCESSDENIED; result.errorApi = L"StartTraceW"; result.reason = L"helper-failed"; }
        else if (std::wstring_view(test) == L"success" || std::wstring_view(test) == L"events-lost" || name == L"playback-control")
        {
            value->HelperStarted(123); value->Listening(); result.capture.started = true; result.capture.statisticsAvailable = true;
            result.capture.providersEnabled = 2; result.capture.eventsLost = 0; result.capture.logBuffersLost = 0; result.capture.realtimeBuffersLost = 0;
            result.capture.stopStatus = 0;
            if (std::wstring_view(test) == L"events-lost") result.capture.eventsLost = 7;
            Observation codec; codec.standard = 2; codec.vendor = 0; codec.codec = 0; result.observation = codec;
        }
        else if (std::wstring_view(test) == L"listening-canceled")
        { value->HelperStarted(123); value->Listening(); result.canceled = true; }
        else if (std::wstring_view(test) == L"helper-timeout") value->HelperStarted(123);
        if (name == L"playback-control")
        {
            result.playbackManualSources = {L"PRIVATE-UI-ONLY-PROCESS.exe"};
            result.playback = {{L"scan", L"complete", 0, {}}, {L"match", L"matched", 1, {}},
                {L"pause", L"request-accepted", 1, {}}, {L"match", L"ambiguous-route", 2, {}},
                {L"pause", L"request-failed", 3, static_cast<uint32_t>(E_ACCESSDENIED)},
                {L"restore", L"restored", 1, {}}, {L"restore", L"cleanup-pending", 0, {}},
                {L"scan", L"identity-unavailable", 0, 15703}, {L"match", L"package-family-matched", 1, {}},
                {L"scan", L"all-system-media-sessions", 0, {}}, {L"match", L"system-media-selected", 1, {}},
                {L"scan", L"audio-streams-active", 0, {}}};
        }
        if (name == L"profile-output-change") testOutput.address = 0x223344bbccddULL;
        if (std::wstring_view(test) == L"helper-timeout")
            value->Finish(result, testOutput, {}, static_cast<uint32_t>(HRESULT_FROM_WIN32(ERROR_TIMEOUT)), L"ConnectNamedPipe");
        else if (std::wstring_view(test) != L"interrupted") value->Finish(result, testOutput, result.capture);
        s.Save();
        if (name == L"success") sampleRecord = Clone(s.record);
        auto report = BuildReport(s.record);
        auto bytes = Zip(report);
        for (auto const &forbidden : {"112233AABBCC", "223344BBCCDD", "SYNTHETIC-ENDPOINT", "Alice", "synthetic-container", "second-container", "INTELAUDIO", "owner@example.com", "PRIVATE-UI-ONLY-PROCESS.exe"})
            if (bytes.find(forbidden) != std::string::npos) return 41;
        if (std::wstring_view(test) == L"preflight")
        {
            auto path = std::filesystem::path(directory) / L"report 保存测试.zip";
            AtomicWrite(path, "existing-report");
            {
                Handle held(CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
                if (!held) return 44;
                try { SaveDiagnosticReport(report, path.wstring()); return 45; } catch (hresult_error const &) { }
            }
            if (ReadBounded(path, MiB) != "existing-report") return 46;
            SaveDiagnosticReport(report, path.wstring());
            if (ReadBounded(path, MiB) != bytes) return 47;
            try { SaveDiagnosticReport(report, (DiagnosticDataDirectory() / L"Reports" / L"guard.json").wstring()); return 48; }
            catch (hresult_invalid_argument const &) { }
            for (auto const &file : std::filesystem::directory_iterator(directory))
                if (file.path().extension() == L".tmp") return 49;
        }
        AtomicWrite(std::filesystem::path(directory) / (std::wstring(test) + L".zip"), bytes);
        if (DiagnosticGitHubUrl(report).size() > 1800) return 42;
        { std::scoped_lock lock(RecordsGate); MemoryRecords.clear(); }
        if (PrepareDiagnosticReport(std::wstring(s.attempt.GetNamedString(L"attempt_id"))).id == report.id) return 43;
    }
    for (auto name : {L"session", L"session-with-log", L"attempt-with-log", L"runtime-log-truncated", L"runtime-log-failed"})
    {
        bool session = std::wstring_view(name).starts_with(L"session");
        auto record = session ? SessionRecord() : Clone(sampleRecord);
        Boolean(record, L"synthetic", true);
        auto reader = [name]() -> std::wstring {
            if (std::wstring_view(name) == L"runtime-log-failed")
                throw hresult_error(E_ACCESSDENIED, L"private-error-message-must-not-appear");
            if (std::wstring_view(name) == L"runtime-log-truncated") return std::wstring(200000, L'測') + L"\nNEWEST-TAIL\n";
            return L"SYNTHETIC runtime history\nBTHENUM\\DEV_112233AABBCC\\test\nowner@example.com\nNEWEST-TAIL\n";
        };
        auto report = BuildReport(record, std::wstring_view(name) != L"session", reader);
        auto bytes = Zip(report);
        for (auto forbidden : {"112233AABBCC", "owner@example.com", "private-error-message-must-not-appear"})
            if (bytes.find(forbidden) != std::string::npos) return 50;
        AtomicWrite(std::filesystem::path(directory) / (std::wstring(name) + L".zip"), bytes);
    }
    // Codec success does not imply playback recovery success. Keep consecutive
    // recovery failures (including cancellation) in the five-failure allowance.
    auto reports = DiagnosticDataDirectory() / L"Reports";
    std::vector<std::filesystem::path> recoveryFiles;
    auto writeRetentionRecord = [&](bool failed, int order) {
        auto record = Clone(sampleRecord);
        auto id = Id();
        auto attempt = record.GetNamedObject(L"attempt");
        String(attempt, L"attempt_id", id);
        if (order == 2) String(attempt, L"outcome", L"user_canceled");
        auto events = record.GetNamedArray(L"events");
        for (auto const &event : events) String(event.GetObject(), L"attempt_id", id);
        JsonObject event = Clone(events.GetAt(0).GetObject()), data;
        Number(event, L"seq", events.Size() + 1); String(event, L"event", L"playback.control");
        String(data, L"control_action", L"restore"); String(data, L"reason", failed ? L"request-rejected" : L"restored");
        Number(data, L"control_session", 1); data.SetNamedValue(L"control_error", JsonValue::CreateNullValue());
        event.SetNamedValue(L"data", data); events.Append(event);
        if (PlaybackRecoveryFailed(record) != failed) throw hresult_invalid_argument();
        auto path = reports / (std::wstring(id) + L".json");
        AtomicWrite(path, to_string(record.Stringify()));
        std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(order + 1));
        return path;
    };
    auto olderSuccess = writeRetentionRecord(false, 0);
    for (int i = 1; i <= 4; ++i) recoveryFiles.push_back(writeRetentionRecord(true, i));
    auto latestSuccess = writeRetentionRecord(false, 5);
    PruneRecords();
    for (auto const &path : recoveryFiles) if (!std::filesystem::exists(path)) return 51;
    if (std::filesystem::exists(olderSuccess) || !std::filesystem::exists(latestSuccess)) return 52;
    for (int i = 6; i <= 7; ++i) recoveryFiles.push_back(writeRetentionRecord(true, i));
    PruneRecords();
    if (std::filesystem::exists(recoveryFiles.front()) || !std::filesystem::exists(latestSuccess)) return 53;
    for (size_t i = 1; i < recoveryFiles.size(); ++i) if (!std::filesystem::exists(recoveryFiles[i])) return 54;
    return 0;
}
} // namespace Native
