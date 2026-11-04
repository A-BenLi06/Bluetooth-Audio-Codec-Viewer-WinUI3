#include "pch.h"
#include "PlaybackControl.h"
#include "NativeServices.h"
#include "DiagnosticsReport.h"
#include "BluetoothAudioDeviceWatcher.h"
#include "BluetoothConnectionQuery.h"
#include <condition_variable>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmsystem.h>
#include <bcrypt.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <format>
#include <cmath>
#include <map>
#include <charconv>
#include <array>

using namespace winrt;
using namespace Windows::Data::Json;
namespace Native
{
namespace
{
// Both GUIDs are listed by Microsoft's BluetoothStack.wprp. Actual embedded
// provider names can differ from its configuration labels; use header identity.
constexpr GUID Providers[]{
    {0x8776ad1e, 0x5022, 0x4451, {0xa5, 0x66, 0xf4, 0x7e, 0x70, 0x8b, 0x90, 0x75}},
    {0xddb6da39, 0x08a7, 0x4579, {0x8d, 0x0c, 0x68, 0x01, 0x11, 0x46, 0xe2, 0x05}}};
bool IsA2dpProvider(GUID const &provider)
{
    return std::any_of(std::begin(Providers), std::end(Providers),
                       [&](GUID const &known) { return IsEqualGUID(provider, known); });
}
constexpr DWORD MaxMessage = 256 * 1024;
thread_local std::wstring FailureApi;
void Check(ULONG error, wchar_t const *api = L"Win32")
{
    if (error != ERROR_SUCCESS)
    {
        FailureApi = api;
        throw_hresult(HRESULT_FROM_WIN32(error));
    }
}
uint64_t Now()
{
    FILETIME value{};
    GetSystemTimeAsFileTime(&value);
    return (uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}
std::wstring RandomHex(size_t count)
{
    std::vector<UCHAR> bytes(count);
    auto status = BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(count), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0)
        throw_hresult(status);
    std::wstring result;
    for (auto b : bytes)
        result += std::format(L"{:02X}", b);
    return result;
}
bool TokenMatches(std::wstring_view a, std::wstring_view b)
{
    if (a.size() != 64 || b.size() != 64)
        return false;
    unsigned difference{};
    for (size_t i = 0; i < 64; ++i)
        difference |= towupper(a[i]) ^ towupper(b[i]);
    return difference == 0;
}
bool ApplyHelperControl(JsonObject const &command, std::wstring_view token, std::wstring_view attempt,
                        Cancellation const &cancel, Cancellation const &beginProbe)
{
    if (!TokenMatches(token, command.GetNamedString(L"Token", L"")) ||
        command.GetNamedString(L"AttemptId", L"") != attempt)
        throw hresult_access_denied(L"Unexpected helper control message.");
    auto action = command.GetNamedString(L"Action", L"");
    if (action == L"cancel") { cancel.Cancel(); return false; }
    if (action != L"begin-probe" || beginProbe.IsCanceled())
        throw hresult_invalid_argument(L"Unexpected or duplicate helper command.");
    beginProbe.Cancel();
    return true;
}
std::wstring Executable()
{
    std::wstring result(32768, L'\0');
    auto count = GetModuleFileNameW(nullptr, result.data(), static_cast<DWORD>(result.size()));
    if (!count || count >= result.size())
        throw_last_error();
    result.resize(count);
    return result;
}
// All pipe operations are overlapped, bounded, and cancelable. A peer cannot
// keep either process alive with an incomplete or unbounded JSON message.
DWORD Transfer(HANDLE pipe, void *bytes, DWORD size, bool write, Cancellation const &cancel, ULONGLONG deadline)
{
    if (cancel.IsCanceled()) throw_hresult(HRESULT_FROM_WIN32(ERROR_CANCELLED));
    if (GetTickCount64() >= deadline) throw_hresult(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    OVERLAPPED operation{};
    operation.hEvent = event.get();
    DWORD transferred{};
    auto success = write ? WriteFile(pipe, bytes, size, &transferred, &operation)
                         : ReadFile(pipe, bytes, size, &transferred, &operation);
    if (!success && GetLastError() != ERROR_IO_PENDING)
        throw_last_error();
    if (!success)
    {
        HANDLE waits[]{event.get(), cancel.event.get()};
        auto now = GetTickCount64();
        DWORD remaining = now >= deadline ? 0 : static_cast<DWORD>(std::min<ULONGLONG>(deadline - now, MAXDWORD - 1));
        auto state = WaitForMultipleObjects(2, waits, FALSE, remaining);
        if (state != WAIT_OBJECT_0)
        {
            CancelIoEx(pipe, &operation);
            GetOverlappedResult(pipe, &operation, &transferred, TRUE);
            throw_hresult(HRESULT_FROM_WIN32(state == WAIT_OBJECT_0 + 1 ? ERROR_CANCELLED : ERROR_TIMEOUT));
        }
        if (!GetOverlappedResult(pipe, &operation, &transferred, FALSE))
            throw_last_error();
    }
    return transferred;
}
std::wstring ReadLine(HANDLE pipe, Cancellation const &cancel, ULONGLONG deadline)
{
    std::string bytes;
    // Read bytewise so cancellation and the final response never consume
    // each other's messages. The protocol sends only a handful of small lines.
    while (bytes.size() < MaxMessage)
    {
        char ch{};
        if (!Transfer(pipe, &ch, 1, false, cancel, deadline))
            throw_hresult(HRESULT_FROM_WIN32(ERROR_BROKEN_PIPE));
        if (ch == '\n')
            return std::wstring(to_hstring(bytes));
        bytes += ch;
    }
    throw hresult_invalid_argument(L"The helper message is too large.");
}
void WriteLine(HANDLE pipe, JsonObject const &message, Cancellation const &cancel, ULONGLONG deadline)
{
    auto bytes = to_string(message.Stringify()) + "\n";
    size_t offset{};
    while (offset < bytes.size())
    {
        auto count =
            Transfer(pipe, bytes.data() + offset, static_cast<DWORD>(bytes.size() - offset), true, cancel, deadline);
        if (!count)
            throw_hresult(HRESULT_FROM_WIN32(ERROR_BROKEN_PIPE));
        offset += count;
    }
}
JsonObject Encode(DetectionResult const &result, std::wstring const &token, std::wstring const &error = {},
                  std::wstring const &attemptId = {})
{
    JsonObject json;
    json.SetNamedValue(L"Token", JsonValue::CreateStringValue(token));
    json.SetNamedValue(L"Canceled", JsonValue::CreateBooleanValue(result.canceled));
    json.SetNamedValue(L"Reason", JsonValue::CreateStringValue(result.reason));
    json.SetNamedValue(L"Error", JsonValue::CreateStringValue(error));
    json.SetNamedValue(L"AttemptId", JsonValue::CreateStringValue(attemptId));
    json.SetNamedValue(L"ErrorCode", result.errorCode ? JsonValue::CreateNumberValue(*result.errorCode) : JsonValue::CreateNullValue());
    json.SetNamedValue(L"ErrorApi", JsonValue::CreateStringValue(result.errorApi));
    JsonObject capture;
    capture.SetNamedValue(L"Started", JsonValue::CreateBooleanValue(result.capture.started));
    capture.SetNamedValue(L"StatisticsAvailable", JsonValue::CreateBooleanValue(result.capture.statisticsAvailable));
    auto number = [&](wchar_t const *name, uint32_t value) { capture.SetNamedValue(name, JsonValue::CreateNumberValue(value)); };
    number(L"ProvidersEnabled", result.capture.providersEnabled); number(L"Events", result.capture.events);
    number(L"MetadataFailures", result.capture.metadataFailures); number(L"StreamingEvents", result.capture.streamingEvents);
    number(L"ConfigurationEvents", result.capture.configurationEvents); number(L"ParseFailures", result.capture.parseFailures);
    number(L"Rejected", result.capture.rejected);
    auto optional = [&](wchar_t const *name, std::optional<uint32_t> value) {
        capture.SetNamedValue(name, value ? JsonValue::CreateNumberValue(*value) : JsonValue::CreateNullValue());
    };
    optional(L"StopStatus", result.capture.stopStatus); optional(L"EventsLost", result.capture.eventsLost);
    optional(L"LogBuffersLost", result.capture.logBuffersLost); optional(L"RealtimeBuffersLost", result.capture.realtimeBuffersLost);
    optional(L"FirstMetadataError", result.capture.firstMetadataError); optional(L"FirstMetadataEventFlags", result.capture.firstMetadataEventFlags);
    json.SetNamedValue(L"Capture", capture);
    JsonArray warnings;
    for (auto const &warning : result.warnings)
        warnings.Append(JsonValue::CreateStringValue(warning));
    json.SetNamedValue(L"Warnings", warnings);
    if (result.observation)
    {
        auto const &o = *result.observation;
        JsonObject item;
        item.SetNamedValue(L"StandardCodecId", JsonValue::CreateNumberValue(o.standard));
        item.SetNamedValue(L"VendorId", JsonValue::CreateNumberValue(static_cast<double>(o.vendor)));
        item.SetNamedValue(L"VendorCodecId", JsonValue::CreateNumberValue(static_cast<double>(o.codec)));
        item.SetNamedValue(L"ObservedFileTime", JsonValue::CreateStringValue(std::to_wstring(o.timestamp)));
        item.SetNamedValue(L"DefaultOutput", JsonValue::CreateStringValue(o.output));
        item.SetNamedValue(L"OutputId", JsonValue::CreateStringValue(o.outputId));
        item.SetNamedValue(L"Source", JsonValue::CreateStringValue(o.source));
        item.SetNamedValue(L"Address", JsonValue::CreateStringValue(std::to_wstring(o.address)));
        json.SetNamedValue(L"Observation", item);
    }
    return json;
}
DetectionResult Decode(JsonObject const &json, std::wstring const &token)
{
    if (!TokenMatches(token, json.GetNamedString(L"Token")))
        throw hresult_access_denied(L"The helper response could not be authenticated.");
    DetectionResult result;
    result.errorMessage = json.GetNamedString(L"Error", L"");
    auto optional = [](JsonObject const &item, wchar_t const *key) -> std::optional<uint32_t> {
        if (!item.HasKey(key) || item.GetNamedValue(key).ValueType() == JsonValueType::Null) return {};
        auto value = item.GetNamedNumber(key);
        if (!std::isfinite(value) || value < 0 || value > MAXDWORD || value != std::floor(value))
            throw hresult_invalid_argument(L"Invalid helper diagnostic counter.");
        return static_cast<uint32_t>(value);
    };
    result.errorCode = optional(json, L"ErrorCode");
    result.errorApi = json.GetNamedString(L"ErrorApi", L"");
    if (result.errorApi.size() > 80 || result.errorMessage.size() > 16384) throw hresult_invalid_argument();
    if (!result.errorMessage.empty() && !result.errorCode) result.errorCode = E_FAIL;
    if (json.HasKey(L"Capture"))
    {
        auto capture = json.GetNamedObject(L"Capture");
        result.capture.started = capture.GetNamedBoolean(L"Started");
        result.capture.statisticsAvailable = capture.GetNamedBoolean(L"StatisticsAvailable");
        result.capture.providersEnabled = optional(capture, L"ProvidersEnabled").value_or(0);
        result.capture.events = optional(capture, L"Events").value_or(0);
        result.capture.metadataFailures = optional(capture, L"MetadataFailures").value_or(0);
        result.capture.streamingEvents = optional(capture, L"StreamingEvents").value_or(0);
        result.capture.configurationEvents = optional(capture, L"ConfigurationEvents").value_or(0);
        result.capture.parseFailures = optional(capture, L"ParseFailures").value_or(0);
        result.capture.rejected = optional(capture, L"Rejected").value_or(0);
        result.capture.stopStatus = optional(capture, L"StopStatus"); result.capture.eventsLost = optional(capture, L"EventsLost");
        result.capture.logBuffersLost = optional(capture, L"LogBuffersLost"); result.capture.realtimeBuffersLost = optional(capture, L"RealtimeBuffersLost");
        result.capture.firstMetadataError = optional(capture, L"FirstMetadataError");
        result.capture.firstMetadataEventFlags = optional(capture, L"FirstMetadataEventFlags");
    }
    result.canceled = json.GetNamedBoolean(L"Canceled", false);
    result.reason = json.GetNamedString(L"Reason", L"");
    for (auto const &item : json.GetNamedArray(L"Warnings", JsonArray{}))
        result.warnings.emplace_back(item.GetString());
    if (json.HasKey(L"Observation"))
    {
        auto item = json.GetNamedObject(L"Observation");
        auto integer = [&](wchar_t const *name, double maximum) -> uint64_t {
            double value = item.GetNamedNumber(name);
            if (!std::isfinite(value) || value < 0 || value > maximum || value != std::floor(value))
                throw hresult_invalid_argument(L"Invalid codec identifier.");
            return static_cast<uint64_t>(value);
        };
        Observation o;
        o.standard = static_cast<uint8_t>(integer(L"StandardCodecId", 255));
        o.vendor = integer(L"VendorId", MAXDWORD);
        o.codec = integer(L"VendorCodecId", UINT16_MAX);
        auto timestamp = to_string(item.GetNamedString(L"ObservedFileTime"));
        auto parsed = std::from_chars(timestamp.data(), timestamp.data() + timestamp.size(), o.timestamp);
        if (parsed.ec != std::errc{} || parsed.ptr != timestamp.data() + timestamp.size() ||
            o.timestamp > Now() + 600000000)
            throw hresult_invalid_argument(L"Invalid observation timestamp.");
        o.name = ResolveCodec(o.standard, o.vendor, o.codec);
        o.output = item.GetNamedString(L"DefaultOutput", L"");
        o.outputId = item.GetNamedString(L"OutputId", L"");
        o.source = item.GetNamedString(L"Source", L"");
        auto address = to_string(item.GetNamedString(L"Address", L"0"));
        auto addressParsed = std::from_chars(address.data(), address.data() + address.size(), o.address);
        if (addressParsed.ec != std::errc{} || addressParsed.ptr != address.data() + address.size() ||
            o.address > 0xffffffffffffULL)
            throw hresult_invalid_argument(L"Invalid Bluetooth address in helper result.");
        result.observation = std::move(o);
    }
    return result;
}
struct HelperArguments
{
    std::wstring pipe, token, attemptId;
    unsigned seconds{};
    bool tone{};
};
HelperArguments Parse(std::vector<std::wstring> const &args)
{
    if (args.size() < 7 || args.size() > 10 || args[0] != L"--elevated-helper" || args[1] != L"--pipe" ||
        args[3] != L"--token" || args[5] != L"--timeout-seconds")
        throw hresult_invalid_argument();
    if (args[2].empty() || args[2].size() > 128 || !std::all_of(args[2].begin(), args[2].end(), [](wchar_t c) {
            return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'-' ||
                   c == L'_';
        }))
        throw hresult_invalid_argument();
    if (args[4].size() != 64 || !std::all_of(args[4].begin(), args[4].end(), [](wchar_t c) {
            return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
        }))
        throw hresult_invalid_argument();
    auto number = to_string(args[6]);
    unsigned seconds{};
    auto parsed = std::from_chars(number.data(), number.data() + number.size(), seconds);
    if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() || seconds < 1 || seconds > 60)
        throw hresult_invalid_argument();
    size_t next = 7;
    std::wstring attemptId;
    if (next < args.size() && args[next] == L"--attempt-id")
    {
        if (++next >= args.size()) throw hresult_invalid_argument();
        attemptId = args[next++];
        if (attemptId.size() != 36 || !std::all_of(attemptId.begin(), attemptId.end(), [](wchar_t c) {
            return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || c == L'-';
        }) || attemptId[8] != L'-' || attemptId[13] != L'-' || attemptId[18] != L'-' || attemptId[23] != L'-')
            throw hresult_invalid_argument();
        for (size_t i = 0; i < attemptId.size(); ++i)
        {
            if (i == 8 || i == 13 || i == 18 || i == 23) continue;
            if (!((attemptId[i] >= L'0' && attemptId[i] <= L'9') || (attemptId[i] >= L'a' && attemptId[i] <= L'f')))
                throw hresult_invalid_argument();
        }
        if (attemptId[14] != L'4' || std::wstring_view(L"89ab").find(attemptId[19]) == std::wstring_view::npos)
            throw hresult_invalid_argument();
    }
    bool tone = next < args.size() && args[next] == L"--play-tone";
    if (tone) ++next;
    if (next != args.size()) throw hresult_invalid_argument();
    return {args[2], args[4], attemptId, seconds, tone};
}
struct EventStats
{
    uint64_t count{};
    unsigned id{}, version{}, opcode{};
    ULONG metadataStatus{};
    std::wstring name;
};
struct TraceContext
{
    Handle found{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::mutex gate;
    std::optional<Observation> observation;
    std::vector<std::wstring> warnings;
    AudioOutput output;
    CodecCapturePolicy policy;
    std::atomic<bool> invalidateConfiguration{};
    uint64_t events{}, metadataFailures{}, streamingEvents{}, parseFailures{}, rejected{}, configurationEvents{};
    std::optional<uint32_t> firstMetadataError, firstMetadataEventFlags;
    std::map<uint64_t, EventStats> stats;
    std::map<uint64_t, bool> ignoredSchemas; // Only successfully decoded non-codec schemas; never cache failures.
};
uint64_t SchemaKey(EVENT_RECORD const *record)
{
    uint64_t key = record->EventHeader.EventDescriptor.Id |
                   (uint64_t(record->EventHeader.EventDescriptor.Version) << 16) |
                   (uint64_t(record->EventHeader.EventDescriptor.Opcode) << 24);
    key ^= uint64_t(record->EventHeader.ProviderId.Data1) << 32;
    for (USHORT i = 0; i < std::min<USHORT>(record->ExtendedDataCount, 128); ++i)
        if (record->ExtendedData[i].ExtType == EVENT_HEADER_EXT_TYPE_EVENT_SCHEMA_TL)
        {
            auto &item = record->ExtendedData[i];
            auto data = reinterpret_cast<BYTE const *>(static_cast<uintptr_t>(item.DataPtr));
            for (USHORT j = 0; data && j < item.DataSize; ++j)
                key = (key ^ data[j]) * 1099511628211ULL;
            break;
        }
    return key;
}
std::wstring MetadataString(std::vector<BYTE> const &buffer, ULONG offset)
{
    if (!offset || offset >= buffer.size() || offset % sizeof(wchar_t))
        return {};
    auto text = reinterpret_cast<wchar_t const *>(buffer.data() + offset);
    auto capacity = (buffer.size() - offset) / sizeof(wchar_t);
    auto length = wcsnlen_s(text, capacity);
    if (length == capacity)
        return {};
    return {text, length};
}
std::array<std::wstring, 3> CodecProperties(std::vector<BYTE> const &metadata)
{
    if (metadata.size() < sizeof(TRACE_EVENT_INFO))
        throw hresult_invalid_argument(L"Incomplete A2DP event metadata.");
    auto info = reinterpret_cast<TRACE_EVENT_INFO const *>(metadata.data());
    if (offsetof(TRACE_EVENT_INFO, EventPropertyInfoArray) +
                uint64_t(info->PropertyCount) * sizeof(EVENT_PROPERTY_INFO) > metadata.size() ||
        info->TopLevelPropertyCount > info->PropertyCount)
        throw hresult_invalid_argument(L"Invalid A2DP property metadata.");
    constexpr wchar_t const *names[]{L"A2dpStandardCodecId", L"A2dpVendorId", L"A2dpVendorCodecId"};
    std::array<std::wstring, 3> properties;
    for (ULONG index = 0; index < info->TopLevelPropertyCount; ++index)
    {
        auto candidate = MetadataString(metadata, info->EventPropertyInfoArray[index].NameOffset);
        for (size_t field = 0; field < properties.size(); ++field)
            if (_wcsicmp(candidate.c_str(), names[field]) == 0)
                properties[field] = candidate;
    }
    if (std::any_of(properties.begin(), properties.end(), [](auto const &name) { return name.empty(); }))
        throw hresult_invalid_argument(L"The A2dpStreaming event is missing named codec fields; its schema is unsupported.");
    return properties;
}
uint64_t ReadProperty(EVENT_RECORD *record, std::wstring const &property)
{
    PROPERTY_DATA_DESCRIPTOR descriptor{};
    descriptor.PropertyName = reinterpret_cast<ULONGLONG>(property.c_str());
    descriptor.ArrayIndex = ULONG_MAX;
    ULONG size{};
    Check(TdhGetPropertySize(record, 0, nullptr, 1, &descriptor, &size));
    if (size != 1 && size != 2 && size != 4 && size != 8)
        throw hresult_invalid_argument(L"Unsupported codec field size.");
    uint64_t value{};
    Check(TdhGetProperty(record, 0, nullptr, 1, &descriptor, size, reinterpret_cast<BYTE *>(&value)));
    return value;
}
std::wstring NamedProperty(std::vector<BYTE> const &metadata, std::wstring_view requested)
{
    auto info = reinterpret_cast<TRACE_EVENT_INFO const *>(metadata.data());
    for (ULONG i = 0; i < info->TopLevelPropertyCount; ++i)
    {
        auto name = MetadataString(metadata, info->EventPropertyInfoArray[i].NameOffset);
        if (Upper(name) == Upper(requested))
        {
            auto const &property = info->EventPropertyInfoArray[i];
            auto type = property.nonStructType.InType;
            bool integer = type == TDH_INTYPE_UINT8 || type == TDH_INTYPE_UINT16 || type == TDH_INTYPE_UINT32 ||
                           type == TDH_INTYPE_UINT64 || type == TDH_INTYPE_HEXINT32 || type == TDH_INTYPE_HEXINT64;
            bool direction = Upper(requested) == L"A2DPISSINK" && type == TDH_INTYPE_BOOLEAN;
            if ((property.Flags & ~PropertyHasTags) || property.count != 1 || (!integer && !direction))
                throw hresult_invalid_argument(L"An A2DP field is not a supported scalar integer.");
            return name;
        }
    }
    throw hresult_invalid_argument(hstring(L"The event is missing a required named field: " + std::wstring(requested)));
}
void WINAPI OnEvent(EVENT_RECORD *record) noexcept
{
    auto &context = *static_cast<TraceContext *>(record->UserContext);
    if (!IsA2dpProvider(record->EventHeader.ProviderId))
        return;
    ++context.events;
    try
    {
        auto key = SchemaKey(record);
        auto statistic = context.stats.find(key);
        if (statistic == context.stats.end() && context.stats.size() < 32)
            statistic = context.stats.emplace(key, EventStats{0, record->EventHeader.EventDescriptor.Id,
                        record->EventHeader.EventDescriptor.Version, record->EventHeader.EventDescriptor.Opcode}).first;
        if (statistic != context.stats.end())
            ++statistic->second.count;
        if (context.ignoredSchemas.contains(key))
            return;
        ULONG size{};
        auto error = TdhGetEventInformation(record, 0, nullptr, nullptr, &size);
        if (error != ERROR_INSUFFICIENT_BUFFER || size < sizeof(TRACE_EVENT_INFO) || size > 1024 * 1024)
        {
            ++context.metadataFailures;
            if (!context.firstMetadataError)
            {
                context.firstMetadataError = error;
                context.firstMetadataEventFlags = record->EventHeader.Flags;
            }
            if (statistic != context.stats.end())
                statistic->second.metadataStatus = error;
            if (context.metadataFailures <= 8)
                Log(L"a2dp-metadata", std::format(L"id={} opcode={} status={} bytes={} extensions={}",
                      record->EventHeader.EventDescriptor.Id, record->EventHeader.EventDescriptor.Opcode,
                      error, size, record->ExtendedDataCount));
            return;
        }
        std::vector<BYTE> metadata(size);
        Check(TdhGetEventInformation(record, 0, nullptr, reinterpret_cast<TRACE_EVENT_INFO *>(metadata.data()), &size));
        if (size > metadata.size() || size < sizeof(TRACE_EVENT_INFO))
            throw hresult_invalid_argument(L"Invalid event metadata size returned by TDH.");
        metadata.resize(size);
        auto info = reinterpret_cast<TRACE_EVENT_INFO const *>(metadata.data());
        if (offsetof(TRACE_EVENT_INFO, EventPropertyInfoArray) +
                    uint64_t(info->PropertyCount) * sizeof(EVENT_PROPERTY_INFO) >
                metadata.size() ||
            info->TopLevelPropertyCount > info->PropertyCount)
            throw hresult_invalid_argument(L"The event property table is invalid.");
        // Provider identity comes from the event header. Display names differ
        // between the trace and telemetry providers and across Windows builds.
        auto event = MetadataString(metadata, info->EventNameOffset);
        auto task = MetadataString(metadata, info->TaskNameOffset);
        auto kind = ClassifyCodecEvent(event, task);
        if (statistic != context.stats.end())
            statistic->second.name = (event.empty() ? task : event).substr(0, 160);
        if (kind == CodecEventKind::Other)
        {
            if (context.ignoredSchemas.size() < 256)
                context.ignoredSchemas[key] = true;
            return;
        }
        CodecEvidence evidence;
        evidence.kind = kind;
        evidence.timestamp = static_cast<uint64_t>(record->EventHeader.TimeStamp.QuadPart);
        evidence.address = ReadProperty(record, NamedProperty(metadata, kind == CodecEventKind::Configuration ?
                                                              L"BTDeviceAddressRemote" : L"BTDeviceAddress"));
        if (evidence.address > 0xffffffffffffULL || evidence.timestamp > Now() + 600000000)
            throw hresult_invalid_argument(L"Invalid event identity or timestamp.");
        if (evidence.address != context.policy.target || evidence.timestamp < context.policy.started)
        {
            ++context.rejected;
            return;
        }
        if (kind != CodecEventKind::Configuration)
        {
            auto sink = ReadProperty(record, NamedProperty(metadata, L"A2dpIsSink"));
            if (sink > 1)
                throw hresult_invalid_argument(L"Invalid A2DP direction field.");
            evidence.sink = sink != 0;
            ++context.streamingEvents;
        }
        else
        {
            ++context.configurationEvents;
            evidence.result = ReadProperty(record, NamedProperty(metadata, L"ResultCode"));
        }
        if (kind != CodecEventKind::StreamStart &&
            (kind != CodecEventKind::Configuration || evidence.result == 0))
        {
            auto fields = CodecProperties(metadata);
            evidence.standard = ReadProperty(record, NamedProperty(metadata, fields[0]));
            evidence.vendor = ReadProperty(record, NamedProperty(metadata, fields[1]));
            evidence.codec = ReadProperty(record, NamedProperty(metadata, fields[2]));
            if (evidence.standard > 255 || evidence.vendor > MAXDWORD || evidence.codec > UINT16_MAX)
                throw hresult_invalid_argument(L"Codec identifiers exceed the A2DP field ranges.");
        }
        if (context.invalidateConfiguration.exchange(false))
            context.policy.configuration.reset();
        auto accepted = context.policy.Accept(evidence);
        if (context.streamingEvents + context.configurationEvents <= 32)
            Log(L"a2dp-evidence", std::format(L"kind={} source-role={} accepted={} result={} target={} standard={} vendor={} codec={} filetime={}",
                  static_cast<int>(kind), !evidence.sink, accepted.has_value(), evidence.result,
                  PrivacyTag(std::to_wstring(evidence.address)), evidence.standard, evidence.vendor, evidence.codec, evidence.timestamp));
        if (!accepted)
            return;
        std::scoped_lock lock(context.gate);
        if (!context.observation || accepted->timestamp >= context.observation->timestamp)
        {
            auto const &item = *accepted;
            context.observation = Observation{static_cast<uint8_t>(item.standard), item.vendor, item.codec,
                item.timestamp, ResolveCodec(static_cast<uint8_t>(item.standard), item.vendor, item.codec),
                context.output.name, context.output.id,
                item.kind == CodecEventKind::Configuration ? L"configuration+stream-start" : L"stream-summary", item.address};
        }
        SetEvent(context.found.get());
    }
    catch (...)
    {
        ++context.parseFailures;
        std::scoped_lock lock(context.gate);
        if (context.warnings.size() < 8)
        {
            context.warnings.emplace_back(to_message());
            Log(L"a2dp-parse", context.warnings.back());
        }
    }
}
} // namespace
std::wstring Format(std::wstring value, std::wstring_view argument)
{
    if (auto index = value.find(L"{0}"); index != std::wstring::npos)
        value.replace(index, 3, argument);
    return value;
}
bool IsElevated()
{
    HANDLE raw{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
        throw_last_error();
    Handle token(raw);
    TOKEN_ELEVATION elevation{};
    DWORD size{};
    if (!GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &size))
        throw_last_error();
    return elevation.TokenIsElevated != 0;
}
std::wstring ResolveCodec(uint8_t standard, uint64_t vendor, uint64_t codec)
{
    static std::map<unsigned, std::wstring> const standards{
        {0, L"SBC"}, {1, L"MPEG-1/2 Audio (MP3)"}, {2, L"MPEG-2/4 AAC"}, {3, L"MPEG-D USAC"}, {4, L"ATRAC family"}};
    static std::map<std::pair<uint64_t, uint64_t>, std::wstring> const vendors{
        {{0x004F, 1}, L"Qualcomm/CSR aptX Classic"},
        {{0x00D7, 0x24}, L"Qualcomm/CSR aptX HD"},
        {{0x000A, 2}, L"Qualcomm/CSR aptX Low Latency"},
        {{0x00D7, 2}, L"Qualcomm/CSR aptX Low Latency"},
        {{0x000A, 1}, L"Qualcomm/CSR FastStream"},
        {{0x000A, 0x104}, L"Qualcomm/CSR True Wireless Stereo v3 AAC"},
        {{0x000A, 0x105}, L"Qualcomm/CSR True Wireless Stereo v3 MP3"},
        {{0x000A, 0x106}, L"Qualcomm/CSR True Wireless Stereo v3 aptX"},
        {{0x012D, 0xAA}, L"Sony LDAC"},
        {{0x0075, 0x102}, L"Samsung HD"},
        {{0x0075, 0x103}, L"Samsung Scalable Codec"},
        {{0x053A, 0x484C}, L"Savitech LHDC"}};
    if (auto found = standards.find(standard); found != standards.end())
        return found->second;
    if (standard != 0xFF)
        return std::format(L"Unknown standard codec (0x{:02X})", standard);
    if (auto found = vendors.find({vendor, codec}); found != vendors.end())
        return found->second;
    return std::format(L"Unknown vendor codec (vendor 0x{:08X}, codec 0x{:04X})", vendor, codec);
}
DetectionResult Detect(unsigned seconds, bool tone, Cancellation const &cancel, std::function<void()> const &ready,
                       CaptureDiagnostics *diagnostics)
{
    if (seconds < 1 || seconds > 60)
        throw hresult_invalid_argument();
    if (!IsElevated())
        throw hresult_access_denied(L"Bluetooth ETW tracing requires administrator access.");
    if (cancel.IsCanceled())
        return {{}, {}, true};
    TraceContext context;
    CaptureDiagnostics fallback;
    auto &diagnostic = diagnostics ? *diagnostics : fallback;
    struct Summary
    {
        TraceContext &context;
        CaptureDiagnostics &value;
        ~Summary()
        {
            value.firstMetadataError = context.firstMetadataError;
            value.firstMetadataEventFlags = context.firstMetadataEventFlags;
            value.statisticsAvailable = value.started && context.events <= MAXDWORD && context.metadataFailures <= MAXDWORD &&
                context.streamingEvents <= MAXDWORD && context.configurationEvents <= MAXDWORD &&
                context.parseFailures <= MAXDWORD && context.rejected <= MAXDWORD;
            if (value.statisticsAvailable)
            {
                value.events = static_cast<uint32_t>(context.events); value.metadataFailures = static_cast<uint32_t>(context.metadataFailures);
                value.streamingEvents = static_cast<uint32_t>(context.streamingEvents); value.configurationEvents = static_cast<uint32_t>(context.configurationEvents);
                value.parseFailures = static_cast<uint32_t>(context.parseFailures); value.rejected = static_cast<uint32_t>(context.rejected);
            }
        }
    } summary{context, diagnostic};
    context.output = DefaultOutputInfo();
    Log(L"detection-target", std::format(L"endpoint={} transport={} address-resolved={} microphone-active={} timeout={} tone={}",
        PrivacyTag(context.output.id), static_cast<int>(context.output.transport), context.output.address != 0,
        context.output.microphoneActive, seconds, tone));
    if (context.output.transport != OutputTransport::Classic || !context.output.address)
    {
        DetectionResult result;
        result.reason = context.output.transport == OutputTransport::HandsFree ? L"hands-free" :
                        context.output.transport == OutputTransport::LowEnergy ? L"le-audio" :
                        context.output.transport == OutputTransport::Other ? L"not-bluetooth" : L"target-unresolved";
        return result;
    }
    context.policy.target = context.output.address;
    context.policy.started = Now();
    // Bluetooth telemetry manifests are not registered on every Windows build.
    // Load the installed driver's eventing resources into this helper's TDH
    // cache; this neither executes the driver nor registers a system manifest.
    wchar_t system[MAX_PATH]{};
    if (GetSystemDirectoryW(system, MAX_PATH))
    {
        for (auto driver : {L"btha2dp.sys", L"bthport.sys"})
        {
            auto binary = (std::filesystem::path(system) / L"drivers" / driver).wstring();
            auto status = TdhLoadManifestFromBinary(binary.data());
            Log(L"tdh-driver-metadata", std::format(L"driver={} status={}", driver, status));
        }
    }
    std::wstring name = L"BluetoothAudioCodec-Native-" + std::to_wstring(GetCurrentProcessId()) + L"-" + RandomHex(8);
    std::vector<BYTE> storage(sizeof(EVENT_TRACE_PROPERTIES) + (name.size() + 1) * sizeof(wchar_t));
    auto properties = reinterpret_cast<EVENT_TRACE_PROPERTIES *>(storage.data());
    properties->Wnode.BufferSize = static_cast<ULONG>(storage.size());
    properties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    properties->Wnode.ClientContext = 2; // system time; OpenTrace converts to UTC FILETIME.
    properties->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    properties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    properties->FlushTimer = 1;
    TRACEHANDLE session{};
    Check(StartTraceW(&session, name.c_str(), properties), L"StartTraceW");
    diagnostic.started = true;
    struct SessionStop
    {
        TRACEHANDLE value;
        EVENT_TRACE_PROPERTIES *properties;
        CaptureDiagnostics &diagnostic;
        bool stopped{};
        void Stop()
        {
            if (!stopped)
            {
                auto error = ControlTraceW(value, nullptr, properties, EVENT_TRACE_CONTROL_STOP);
                diagnostic.stopStatus = error;
                if (error == ERROR_SUCCESS)
                {
                    diagnostic.eventsLost = properties->EventsLost;
                    diagnostic.logBuffersLost = properties->LogBuffersLost;
                    diagnostic.realtimeBuffersLost = properties->RealTimeBuffersLost;
                }
                Log(L"etw-stop", std::format(L"status={} events-lost={} buffers-lost={}", error,
                                             properties->EventsLost, properties->RealTimeBuffersLost));
                stopped = true;
            }
        }
        ~SessionStop()
        {
            Stop();
        }
    } cleanup{session, properties, diagnostic};
    unsigned enabled{};
    ULONG enableError{};
    for (auto const &provider : Providers)
    {
        auto error = EnableTraceEx2(session, &provider, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                                   TRACE_LEVEL_VERBOSE, 0, 0, 5000, nullptr);
        Log(L"etw-enable", std::format(L"provider={:08X} status={} synchronous-timeout-ms=5000", provider.Data1, error));
        if (error == ERROR_SUCCESS)
            ++enabled;
        else
        {
            enableError = error;
            context.warnings.emplace_back(std::format(L"An A2DP event provider could not be enabled (Windows error {}).", error));
        }
    }
    if (!enabled)
        Check(enableError, L"EnableTraceEx2");
    diagnostic.providersEnabled = enabled;
    EVENT_TRACE_LOGFILEW log{};
    log.LoggerName = name.data();
    log.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    log.EventRecordCallback = OnEvent;
    log.Context = &context;
    auto trace = OpenTraceW(&log);
    if (trace == INVALID_PROCESSTRACE_HANDLE)
    {
        FailureApi = L"OpenTraceW";
        throw_last_error();
    }
    struct TraceClose
    {
        TRACEHANDLE value;
        void Close()
        {
            if (value != INVALID_PROCESSTRACE_HANDLE)
            {
                CloseTrace(value);
                value = INVALID_PROCESSTRACE_HANDLE;
            }
        }
        ~TraceClose()
        {
            Close();
        }
    } traceClose{trace};
    Handle completed(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    ULONG processError{};
    std::jthread consumer([&] {
        processError = ProcessTrace(&trace, 1, nullptr, nullptr);
        SetEvent(completed.get());
    });
    // Stop the real-time consumer before jthread joins, including if starting
    // the tone worker fails. CloseTrace also wakes a quiet provider if stopping
    // the session itself fails.
    struct ConsumerShutdown
    {
        SessionStop &session;
        TraceClose &trace;
        ~ConsumerShutdown()
        {
            session.Stop();
            trace.Close();
        }
    } shutdown{cleanup, traceClose};
    Cancellation finished;
    // Readiness may wait for the parent to pause players. This time is part of
    // the existing capture budget, not an unbounded extension of detection.
    auto deadline = GetTickCount64() + seconds * 1000;
    if (ready)
        ready();
    Log(L"listening", L"Real-time consumer opened and provider enable requests completed; awaiting driver events.");
    std::atomic<bool> toneFailed{};
    std::jthread toneWorker([&] {
        HANDLE waits[]{finished.event.get(), cancel.event.get()};
        try
        {
            if (tone && WaitForMultipleObjects(2, waits, FALSE, 150) == WAIT_TIMEOUT)
                PlayDiagnosticTone(context.output, cancel, finished);
        }
        catch (...)
        {
            auto message = to_message();
            toneFailed = true;
            Log(L"diagnostic-tone", message);
            std::scoped_lock lock(context.gate);
            if (context.warnings.size() < 8)
                context.warnings.emplace_back(message);
        }
    });
    HANDLE waits[]{context.found.get(), cancel.event.get(), completed.get()};
    DWORD wait{};
    do
    {
        auto now = GetTickCount64();
        wait = WaitForMultipleObjects(3, waits, FALSE, now >= deadline ? 0 : static_cast<DWORD>(deadline - now));
        if (wait != WAIT_OBJECT_0)
            break;
        auto current = DefaultOutputInfo();
        // DefaultOutputInfo only resolves active Core Audio endpoints. Do not
        // gate detection on the connection badge's Audio/Video class filter:
        // some valid headsets advertise another Bluetooth class of device.
        if (current.address == context.output.address && current.transport == OutputTransport::Classic)
            break;
        // A disconnected stream's closing summary is not a result for the
        // current output. Keep the remaining window available for reconnection.
        std::scoped_lock lock(context.gate);
        context.observation.reset();
        context.invalidateConfiguration = true;
        ResetEvent(context.found.get());
        Log(L"capture-target-changed", L"Discarded closing evidence; continuing within the original capture deadline.");
    } while (GetTickCount64() < deadline);
    if (wait == WAIT_FAILED)
        Log(L"capture-wait", std::format(L"status={}", GetLastError()));
    finished.Cancel();
    toneWorker.join(); // Release our audio client while the ETW consumer is still running.
    if (!cancel.IsCanceled())
    {
        auto flush = ControlTraceW(session, nullptr, properties, EVENT_TRACE_CONTROL_FLUSH);
        Log(L"etw-flush", std::format(L"status={} wait={}", flush, wait));
        if (wait == WAIT_TIMEOUT)
            WaitForMultipleObjects(3, waits, FALSE, 1000);
    }
    cleanup.Stop();
    // Stopping the producer flushes queued events. Give ProcessTrace a bounded
    // drain opportunity before closing a consumer that may still hold a buffer.
    WaitForSingleObject(completed.get(), 1500);
    traceClose.Close();
    consumer.join();
    Log(L"a2dp-capture", std::format(L"providers-enabled={} events={} metadata-unavailable={} streaming-events={} detected={} config-events={} parse-failures={} rejected={}",
                                     enabled, context.events, context.metadataFailures, context.streamingEvents,
                                     context.observation.has_value(), context.configurationEvents, context.parseFailures, context.rejected));
    for (auto const &[key, stat] : context.stats)
        Log(L"a2dp-event", std::format(L"schema={:016X} id={} version={} opcode={} count={} metadata-status={} name={}",
             key, stat.id, stat.version, stat.opcode, stat.count, stat.metadataStatus, stat.name));
    if (processError != ERROR_SUCCESS && processError != ERROR_CANCELLED)
        Check(processError, L"ProcessTrace");
    std::scoped_lock lock(context.gate);
    bool canceled = cancel.IsCanceled() && !context.observation;
    DetectionResult result{std::move(context.observation), std::move(context.warnings), canceled};
    auto current = DefaultOutputInfo();
    if (result.observation && (current.address != context.output.address ||
                               current.transport != OutputTransport::Classic))
    {
        result.observation.reset();
        result.reason = L"output-changed";
    }
    else if (result.observation)
    {
        // Reconnection can recreate an endpoint ID for the same physical target.
        result.observation->outputId = current.id;
        result.observation->output = current.name;
    }
    if (!result.observation && result.reason.empty())
        result.reason = toneFailed ? L"tone-failed" : context.parseFailures ? L"schema-unsupported" :
                        context.metadataFailures ? L"metadata-unavailable" : current.microphoneActive ? L"microphone-active" :
                        context.policy.sawStart ? L"waiting-for-stream-end" : !context.events ? L"no-provider-events" : L"no-matching-event";
    Log(L"detection-finished", std::format(L"canceled={} observed={} reason={}", result.canceled,
                                         result.observation.has_value(), result.reason));
    return result;
}
DetectionResult DetectWithElevationCore(unsigned seconds, bool tone, Cancellation const &cancel, HWND owner,
                                       std::function<void()> const &ready, AudioOutput const &target, DiagnosticAttempt *report,
                                       PlaybackControl &playback, std::function<void(PlaybackProgress const &)> const &progress)
{
    if (seconds < 1 || seconds > 60)
        throw hresult_invalid_argument();
    if (cancel.IsCanceled())
        return {{}, {}, true};
    if (target.transport != OutputTransport::Classic || !target.address)
    {
        DetectionResult result;
        result.reason = target.transport == OutputTransport::HandsFree ? L"hands-free" :
                        target.transport == OutputTransport::LowEnergy ? L"le-audio" :
                        target.transport == OutputTransport::Other ? L"not-bluetooth" : L"target-unresolved";
        Log(L"detection-preflight", result.reason);
        if (report) report->Preflight(target, result.reason);
        return result;
    }
    if (report) report->Preflight(target, L"ready");
    // Keep ETW in the short-lived helper even when the user manually starts
    // the UI elevated. Closing the UI then closes the pipe and cancels tracing
    // independently of the UI dispatcher shutting down.
    auto name = L"BluetoothAudioCodec-" + std::to_wstring(GetCurrentProcessId()) + L"-" + RandomHex(16);
    auto token = RandomHex(32);
    HANDLE rawToken{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken))
        throw_last_error();
    Handle processToken(rawToken);
    DWORD size{};
    GetTokenInformation(processToken.get(), TokenUser, nullptr, 0, &size);
    std::vector<BYTE> user(size);
    if (!GetTokenInformation(processToken.get(), TokenUser, user.data(), size, &size))
        throw_last_error();
    LPWSTR sid{};
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid, &sid))
        throw_last_error();
    std::wstring sddl = L"D:P(A;;GA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor{};
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
        throw_last_error();
    struct LocalRelease
    {
        void *value;
        ~LocalRelease()
        {
            LocalFree(value);
        }
    } descriptorRelease{descriptor};
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    auto path = L"\\\\.\\pipe\\" + name;
    Handle pipe(CreateNamedPipeW(path.c_str(),
                                 PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                 PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
                                 MaxMessage, MaxMessage, 0, &security));
    if (!pipe)
        throw_last_error();
    Handle connected(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    OVERLAPPED connect{};
    connect.hEvent = connected.get();
    auto connectedNow = ConnectNamedPipe(pipe.get(), &connect);
    if (!connectedNow && GetLastError() != ERROR_IO_PENDING)
        throw_last_error();
    struct ConnectionCleanup
    {
        HANDLE pipe;
        OVERLAPPED &operation;
        bool pending;
        ~ConnectionCleanup()
        {
            if (pending)
            {
                // Keep OVERLAPPED and its event alive until cancellation is
                // acknowledged, including failures before ShellExecuteEx.
                CancelIoEx(pipe, &operation);
                DWORD ignored{};
                GetOverlappedResult(pipe, &operation, &ignored, TRUE);
            }
        }
    } connectCleanup{pipe.get(), connect, !connectedNow};
    auto executable = Executable();
    auto parameters = L"--elevated-helper --pipe " + name + L" --token " + token + L" --timeout-seconds " +
                      std::to_wstring(seconds) + (report ? L" --attempt-id " + report->AttemptId() : L"") + (tone ? L" --play-tone" : L"");
    auto directory = std::filesystem::path(executable).parent_path().wstring();
    SHELLEXECUTEINFOW launch{sizeof(launch)};
    launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    launch.hwnd = owner;
    launch.lpVerb = L"runas";
    launch.lpFile = executable.c_str();
    launch.lpParameters = parameters.c_str();
    launch.lpDirectory = directory.c_str();
    launch.nShow = SW_HIDE;
    if (!ShellExecuteExW(&launch))
    {
        FailureApi = L"ShellExecuteExW";
        auto error = GetLastError();
        CancelIoEx(pipe.get(), &connect);
        DWORD ignored{};
        GetOverlappedResult(pipe.get(), &connect, &ignored, TRUE);
        if (error == ERROR_CANCELLED)
            return {{}, {}, false, true};
        Check(error);
    }
    Handle helper(launch.hProcess);
    if (report) report->HelperStarted(GetProcessId(helper.get()));
    Log(L"helper-launched", std::format(L"helper-pid={}", GetProcessId(helper.get())));
    HANDLE waits[]{connected.get(), cancel.event.get(), helper.get()};
    auto state = connectedNow ? WAIT_OBJECT_0 : WaitForMultipleObjects(3, waits, FALSE, 15000);
    if (state != WAIT_OBJECT_0)
    {
        CancelIoEx(pipe.get(), &connect);
        DWORD ignored{};
        GetOverlappedResult(pipe.get(), &connect, &ignored, TRUE);
        if (cancel.IsCanceled())
            return {{}, {}, true};
        if (state == WAIT_OBJECT_0 + 2)
        {
            DWORD code{};
            DetectionResult result;
            result.reason = L"helper-exited";
            result.errorCode = HRESULT_FROM_WIN32(ERROR_PROCESS_ABORTED);
            result.errorApi = L"WaitForMultipleObjects";
            result.errorMessage = L"The administrator helper exited before connecting.";
            if (GetExitCodeProcess(helper.get(), &code) && report) report->HelperExited(code);
            return result;
        }
        FailureApi = L"ConnectNamedPipe";
        throw hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT),
                            L"The administrator helper did not connect within 15 seconds.");
    }
    DWORD ignored{};
    if (!connectedNow && !GetOverlappedResult(pipe.get(), &connect, &ignored, FALSE))
        throw_last_error();
    ULONG client{};
    if (!GetNamedPipeClientProcessId(pipe.get(), &client) || client != GetProcessId(helper.get()))
        throw hresult_access_denied(L"Unexpected helper process.");
    try
    {
        bool listening{};
        auto deadline = GetTickCount64() + (seconds + 25) * 1000;
        for (;;)
        {
            auto message = JsonObject::Parse(ReadLine(pipe.get(), cancel, deadline));
            if (!TokenMatches(token, message.GetNamedString(L"Token", L"")))
                throw hresult_access_denied(L"The helper response could not be authenticated.");
            if (report && message.GetNamedString(L"AttemptId", L"") != report->AttemptId())
                throw hresult_access_denied(L"The helper response belongs to another detection.");
            if (message.GetNamedString(L"Phase", L"") == L"Listening")
            {
                if (listening)
                    throw hresult_invalid_argument(L"Duplicate helper readiness message.");
                listening = true;
                if (report) report->Listening();
                deadline = GetTickCount64() + (seconds + 10) * 1000;
                if (ready)
                    ready();
                playback.Pause(cancel, progress);
                if (progress) progress({PlaybackPhase::Detecting, {}});
                DetectionResult playbackStatus;
                playback.CopyTo(playbackStatus);
                if (report) report->Playback(playbackStatus.playback);
                if (progress && playbackStatus.playbackManualRequired) progress({PlaybackPhase::ManualPause, playbackStatus.playbackManualSources});
                JsonObject command;
                command.SetNamedValue(L"Token", JsonValue::CreateStringValue(token));
                command.SetNamedValue(L"AttemptId", JsonValue::CreateStringValue(report ? report->AttemptId() : L""));
                command.SetNamedValue(L"Action", JsonValue::CreateStringValue(L"begin-probe"));
                WriteLine(pipe.get(), command, cancel, GetTickCount64() + 1000);
                continue;
            }
            auto result = Decode(message, token);
            if (WaitForSingleObject(helper.get(), 1500) == WAIT_OBJECT_0)
            {
                DWORD code{};
                if (GetExitCodeProcess(helper.get(), &code) && report) report->HelperExited(code);
            }
            if (result.observation && result.observation->address != target.address)
                throw hresult_error(E_FAIL, L"The default output changed before detection started. Try again.");
            return result;
        }
    }
    catch (...)
    {
        // Timeouts/protocol failures must stop the helper just like user cancellation.
        // Preserve the original error after requesting cancellation.
        bool userCanceled = cancel.IsCanceled();
        Cancellation send;
        try
        {
            JsonObject command;
            command.SetNamedValue(L"Token", JsonValue::CreateStringValue(token));
            command.SetNamedValue(L"Action", JsonValue::CreateStringValue(L"cancel"));
            command.SetNamedValue(L"AttemptId", JsonValue::CreateStringValue(report ? report->AttemptId() : L""));
            WriteLine(pipe.get(), command, send, GetTickCount64() + 1000);
        }
        catch (...)
        {
        }
        if (!userCanceled) throw;
        return {{}, {}, true};
    }
}
DetectionResult DetectWithElevation(unsigned seconds, bool tone, Cancellation const &cancel, HWND owner,
                                   std::function<void()> const &ready, std::function<void(PlaybackProgress const &)> const &progress)
{
    FailureApi.clear();
    // A pre-cancelled request must not query device metadata or create an
    // attempt. In particular, no WinRT device query should run on a caller's
    // STA when the request will never reach the background detection path.
    if (cancel.IsCanceled()) return {{}, {}, true};
    auto target = DefaultOutputInfo();
    auto report = DiagnosticAttempt::Begin(target);
    PlaybackControl playback(target);
    auto restore = [&] {
        if (progress) { try { progress({PlaybackPhase::Restoring, {}}); } catch (...) {} }
        playback.Restore();
    };
    try
    {
        auto result = DetectWithElevationCore(seconds, tone, cancel, owner, ready, target, report.get(), playback, progress);
        restore();
        playback.CopyTo(result);
        auto current = DefaultOutputInfo();
        if (result.observation && (result.observation->outputId != current.id || result.observation->address != current.address ||
                                  current.transport != OutputTransport::Classic))
        {
            result.observation.reset(); result.reason = L"output-changed";
        }
        if (report) report->Finish(result, current, result.capture);
        return result;
    }
    catch (...)
    {
        auto failureCode = to_hresult();
        auto failureMessage = to_message();
        restore();
        DetectionResult result;
        playback.CopyTo(result);
        result.errorCode = static_cast<uint32_t>(failureCode.value);
        result.errorMessage = failureMessage;
        result.errorApi = FailureApi.empty() ? L"DetectWithElevation" : FailureApi;
        result.reason = L"helper-failed";
        if (report)
        {
            report->Finish(result, DefaultOutputInfo(), {}, static_cast<uint32_t>(failureCode.value),
                           FailureApi.empty() ? L"DetectWithElevation" : FailureApi);
        }
        return result;
    }
}
int RunHelper(std::vector<std::wstring> const &args)
{
    HelperArguments options;
    try
    {
        options = Parse(args);
    }
    catch (...)
    {
        return 2;
    }
    auto path = L"\\\\.\\pipe\\" + options.pipe;
    if (!WaitNamedPipeW(path.c_str(), 15000))
        return 3;
    Handle pipe(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                            FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr));
    if (!pipe)
        return 3;
    Cancellation cancel, stopMonitor, beginProbe;
    DetectionResult result;
    std::wstring error;
    CaptureDiagnostics capture;
    std::jthread monitor([&] {
        try
        {
            init_apartment(apartment_type::multi_threaded);
            struct ApartmentScope
            {
                ~ApartmentScope()
                {
                    uninit_apartment();
                }
            } apartment;
            while (!stopMonitor.IsCanceled())
            {
                auto command = JsonObject::Parse(ReadLine(pipe.get(), stopMonitor, GetTickCount64() + (options.seconds + 30) * 1000));
                if (!ApplyHelperControl(command, options.token, options.attemptId, cancel, beginProbe)) break;
            }
        }
        catch (...)
        {
            if (!stopMonitor.IsCanceled())
            {
                Log(L"helper-control", to_message());
                cancel.Cancel();
            }
        }
    });
    try
    {
        if (!IsElevated())
            throw hresult_access_denied(L"The codec helper was not started with administrator access.");
        result = Detect(options.seconds, options.tone, cancel, [&] {
            JsonObject phase;
            phase.SetNamedValue(L"Token", JsonValue::CreateStringValue(options.token));
            phase.SetNamedValue(L"Phase", JsonValue::CreateStringValue(L"Listening"));
            phase.SetNamedValue(L"AttemptId", JsonValue::CreateStringValue(options.attemptId));
            WriteLine(pipe.get(), phase, cancel, GetTickCount64() + 3000);
            HANDLE waits[]{cancel.event.get(), beginProbe.event.get()};
            auto wait = WaitForMultipleObjects(2, waits, FALSE, 6500);
            if (wait == WAIT_TIMEOUT) throw hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT), L"The parent did not start the codec probe in time.");
            if (wait == WAIT_FAILED) throw_last_error();
        }, &capture);
    }
    catch (...)
    {
        error = to_message();
        result.errorCode = static_cast<uint32_t>(to_hresult().value);
        result.errorApi = FailureApi.empty() ? L"Detect" : FailureApi;
        result.reason = L"helper-failed";
        Log(L"helper-detection", error);
    }
    stopMonitor.Cancel();
    monitor.join();
    result.capture = capture;
    try
    {
        Cancellation send;
        WriteLine(pipe.get(), Encode(result, options.token, error, options.attemptId), send, GetTickCount64() + 3000);
    }
    catch (...)
    {
        return 4;
    }
    return error.empty() ? 0 : 5;
}
int RunCaptureTests()
{
    struct Field { std::string name; BYTE type; uint64_t value; size_t bytes; };
    TraceContext context;
    context.output.id = L"test-endpoint";
    context.output.name = L"Test headset";
    context.policy.target = 0x112233445566ULL;
    context.policy.started = Now() - 10000000;
    auto send = [&](std::string const &name, std::vector<Field> const &fields, bool schemaPresent = true, BYTE opcode = 0) {
        auto appendString = [](std::vector<BYTE> &data, std::string const &value) {
            data.insert(data.end(), value.begin(), value.end()); data.push_back(0);
        };
        std::vector<BYTE> schema(3), traits(2), payload;
        appendString(schema, name);
        appendString(traits, "Microsoft.Windows.Bluetooth.BthA2dp");
        for (auto const &field : fields)
        {
            appendString(schema, field.name);
            schema.push_back(field.type);
            for (size_t i = 0; i < field.bytes; ++i)
                payload.push_back(static_cast<BYTE>(field.value >> (i * 8)));
        }
        auto schemaSize = static_cast<USHORT>(schema.size()), traitsSize = static_cast<USHORT>(traits.size());
        memcpy(schema.data(), &schemaSize, 2); memcpy(traits.data(), &traitsSize, 2);
        EVENT_HEADER_EXTENDED_DATA_ITEM extended[2]{};
        extended[0].ExtType = EVENT_HEADER_EXT_TYPE_EVENT_SCHEMA_TL;
        extended[0].DataSize = schemaSize;
        extended[0].DataPtr = reinterpret_cast<ULONGLONG>(schema.data());
        extended[1].ExtType = EVENT_HEADER_EXT_TYPE_PROV_TRAITS;
        extended[1].DataSize = traitsSize;
        extended[1].DataPtr = reinterpret_cast<ULONGLONG>(traits.data());
        EVENT_RECORD record{};
        record.EventHeader.Size = sizeof(EVENT_HEADER);
        record.EventHeader.Flags = EVENT_HEADER_FLAG_EXTENDED_INFO | EVENT_HEADER_FLAG_64_BIT_HEADER;
        record.EventHeader.ProviderId = Providers[0];
        record.EventHeader.TimeStamp.QuadPart = static_cast<LONGLONG>(Now());
        record.EventHeader.EventDescriptor.Channel = 11;
        record.EventHeader.EventDescriptor.Level = 5;
        record.EventHeader.EventDescriptor.Opcode = opcode;
        record.ExtendedDataCount = schemaPresent ? 2 : 1;
        record.ExtendedData = schemaPresent ? extended : extended + 1;
        record.UserDataLength = static_cast<USHORT>(payload.size());
        record.UserData = payload.data();
        record.UserContext = &context;
        OnEvent(&record);
    };
    std::vector<Field> summary{{"A2dpStandardCodecId", 4, 255, 1}, {"A2dpVendorId", 8, 301, 4},
        {"A2dpVendorCodecId", 6, 170, 2}, {"A2dpIsSink", 13, 0, 4}, {"BTDeviceAddress", 10, context.policy.target, 8}};
    send("A2dpStreaming", summary, false);
    if (!context.metadataFailures || context.observation)
        return 40;
    send("A2dpAvailableRemoteCodec", summary);
    if (context.observation)
        return 41;
    auto other = summary;
    other.back().value++;
    send("A2dpStreaming", other);
    if (context.observation || context.rejected != 1)
        return 42;
    other = summary; other[3].value = 1;
    send("A2dpStreaming", other);
    if (context.observation)
        return 43;
    send("A2dpStreaming", summary, true, 2);
    if (!context.observation || context.observation->name != L"Sony LDAC" ||
        context.observation->address != context.policy.target || context.observation->source != L"stream-summary")
        return 44;
    context.observation.reset(); ResetEvent(context.found.get());
    auto missing = summary; missing.erase(missing.begin());
    send("A2dpStreaming", missing);
    if (context.observation || !context.parseFailures)
        return 45;
    send("a2dpstreaming/Stop", summary);
    if (!context.observation)
        return 46; // A failed schema was not cached; later valid records must recover.
    context.observation.reset(); ResetEvent(context.found.get());
    auto invalidType = summary; invalidType[1].type = 13;
    auto failures = context.parseFailures;
    send("A2dpStreaming", invalidType);
    if (context.observation || context.parseFailures != failures + 1)
        return 50;
    auto config = summary;
    config.erase(config.begin() + 3);
    config.back().name = "BTDeviceAddressRemote";
    config.push_back({"ResultCode", 4, 1, 1});
    std::vector<Field> start{{"A2dpIsSink", 13, 0, 4}, {"BTDeviceAddress", 10, context.policy.target, 8}};
    send("AvdtpSetConfigInfo", config); send("A2dpStreamingStart", start);
    if (context.observation)
        return 47;
    config.back().value = 0;
    send("AvdtpSetConfigInfo", config);
    if (context.observation)
        return 48;
    send("A2dpStreamingStart", start);
    if (!context.observation || context.observation->source != L"configuration+stream-start")
        return 49;
    return 0;
}
int RunSelfTests()
{
    try
    {
        Log(L"self-test-begin", L"Synthetic event fixtures follow; these are not live Bluetooth observations.");
        {
            auto name = L"\\\\.\\pipe\\BAC-deadline-test-" + RandomHex(12);
            Handle server(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 4096, 4096, 0, nullptr));
            Handle client(CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr));
            if (!server || !client) return 60;
            if (!ConnectNamedPipe(server.get(), nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) return 61;
            DWORD written{};
            if (!WriteFile(client.get(), "x", 1, &written, nullptr) || written != 1) return 62;
            Cancellation stop;
            auto expect = [&](uint32_t code, ULONGLONG deadline) {
                try { ReadLine(server.get(), stop, deadline); }
                catch (hresult_error const &error) { return static_cast<uint32_t>(error.code().value) == code; }
                return false;
            };
            // Even already-buffered input must honor an expired deadline/cancel.
            if (!expect(static_cast<uint32_t>(HRESULT_FROM_WIN32(ERROR_TIMEOUT)), GetTickCount64())) return 63;
            stop.Cancel();
            if (!expect(static_cast<uint32_t>(HRESULT_FROM_WIN32(ERROR_CANCELLED)), GetTickCount64() + 1000)) return 64;
            ResetEvent(stop.event.get());
            // A peer that writes a partial response then stays alive cannot hang the reader.
            auto started = GetTickCount64();
            if (!expect(static_cast<uint32_t>(HRESULT_FROM_WIN32(ERROR_TIMEOUT)), started + 40) || GetTickCount64() - started > 2000) return 65;
            Log(L"deadline-regression", L"Buffered-input deadline, cancellation and stalled partial helper response passed.");
        }
        if (auto error = RunDiagnosticsTests())
            return error;
        if (auto error = RunPlaybackControlTests())
            return error;
        {
            auto token = RandomHex(32);
            std::wstring attempt = L"00000000-0000-4000-8000-000000000001";
            Cancellation cancel, probe;
            JsonObject command;
            command.SetNamedValue(L"Token", JsonValue::CreateStringValue(token));
            command.SetNamedValue(L"AttemptId", JsonValue::CreateStringValue(attempt));
            command.SetNamedValue(L"Action", JsonValue::CreateStringValue(L"begin-probe"));
            try { ApplyHelperControl(command, RandomHex(32), attempt, cancel, probe); return 193; }
            catch (hresult_access_denied const &) {}
            try { ApplyHelperControl(command, token, L"another-attempt", cancel, probe); return 194; }
            catch (hresult_access_denied const &) {}
            command.SetNamedValue(L"Action", JsonValue::CreateStringValue(L"unknown"));
            try { ApplyHelperControl(command, token, attempt, cancel, probe); return 195; }
            catch (hresult_invalid_argument const &) {}
            command.SetNamedValue(L"Action", JsonValue::CreateStringValue(L"begin-probe"));
            if (!ApplyHelperControl(command, token, attempt, cancel, probe) || !probe.IsCanceled() || cancel.IsCanceled()) return 196;
            try { ApplyHelperControl(command, token, attempt, cancel, probe); return 197; }
            catch (hresult_invalid_argument const &) {}
            command.SetNamedValue(L"Action", JsonValue::CreateStringValue(L"cancel"));
            if (ApplyHelperControl(command, token, attempt, cancel, probe) || !cancel.IsCanceled()) return 198;
        }
        if (auto error = RunCaptureTests())
            return error;
        // Synthetic TDH schemas cover reordered/case-varied fields and ensure
        // a changed schema cannot silently turn an unrelated field into SBC.
        GUID traceProvider{0xddb6da39, 0x08a7, 0x4579, {0x8d, 0x0c, 0x68, 0x01, 0x11, 0x46, 0xe2, 0x05}};
        GUID telemetryProvider{0x8776ad1e, 0x5022, 0x4451, {0xa5, 0x66, 0xf4, 0x7e, 0x70, 0x8b, 0x90, 0x75}};
        if (!IsA2dpProvider(traceProvider) || !IsA2dpProvider(telemetryProvider) || IsA2dpProvider(GUID{}))
            return 23;
        auto schema = [](std::vector<std::wstring> const &names) {
            auto header = std::max(sizeof(TRACE_EVENT_INFO), offsetof(TRACE_EVENT_INFO, EventPropertyInfoArray) +
                                                               names.size() * sizeof(EVENT_PROPERTY_INFO));
            size_t size = header;
            for (auto const &name : names)
                size += (name.size() + 1) * sizeof(wchar_t);
            std::vector<BYTE> metadata(size);
            auto info = reinterpret_cast<TRACE_EVENT_INFO *>(metadata.data());
            info->PropertyCount = info->TopLevelPropertyCount = static_cast<ULONG>(names.size());
            size_t offset = header;
            for (size_t index = 0; index < names.size(); ++index)
            {
                info->EventPropertyInfoArray[index].NameOffset = static_cast<ULONG>(offset);
                auto bytes = (names[index].size() + 1) * sizeof(wchar_t);
                memcpy(metadata.data() + offset, names[index].c_str(), bytes);
                offset += bytes;
            }
            return metadata;
        };
        auto fields = CodecProperties(schema({L"Unrelated", L"a2dpvendorcodecid", L"A2dpStandardCodecId", L"A2dpVendorId"}));
        if (fields[0] != L"A2dpStandardCodecId" || fields[1] != L"A2dpVendorId" || fields[2] != L"a2dpvendorcodecid")
            return 24;
        auto rejectsSchema = [](auto const &metadata) {
            try { CodecProperties(metadata); return false; }
            catch (hresult_invalid_argument const &) { return true; }
        };
        if (!rejectsSchema(schema({L"a", L"b", L"c", L"d", L"e", L"f"})) ||
            !rejectsSchema(schema({L"A2dpStandardCodecId", L"A2dpVendorId"})) ||
            !rejectsSchema(std::vector<BYTE>{}))
            return 25;
        auto malformed = schema({L"A2dpStandardCodecId", L"A2dpVendorId", L"A2dpVendorCodecId"});
        reinterpret_cast<TRACE_EVENT_INFO *>(malformed.data())->PropertyCount = ULONG_MAX;
        if (!rejectsSchema(malformed))
            return 26;
        if (ResolveCodec(0, 0, 0) != L"SBC" || ResolveCodec(2, 0, 0) != L"MPEG-2/4 AAC" ||
            ResolveCodec(255, 0x12D, 0xAA) != L"Sony LDAC" ||
            ResolveCodec(255, 0x4F, 1) != L"Qualcomm/CSR aptX Classic" ||
            ResolveCodec(255, 0xD7, 0x24) != L"Qualcomm/CSR aptX HD")
            return 11;
        if (ResolveCodec(254, 0, 0).find(L"Unknown standard") != 0 ||
            ResolveCodec(255, MAXDWORD, UINT16_MAX).find(L"Unknown vendor") != 0)
            return 12;
        auto token = RandomHex(32);
        auto timestamp = Now();
        DetectionResult original{
            {Observation{255, 0x12D, 0xAA, timestamp, L"Sony LDAC", L"耳机 🎧", L"test-endpoint", L"stream-summary", 0x112233aabbccULL}}, {L"warning"}, false};
        original.reason = L"test-reason";
        auto restored = Decode(JsonObject::Parse(Encode(original, token).Stringify()), token);
        if (!restored.observation || restored.observation->output != L"耳机 🎧" ||
            restored.observation->timestamp != timestamp || restored.observation->vendor != 0x12D ||
            restored.warnings.size() != 1 || restored.reason != original.reason ||
            restored.observation->address != original.observation->address ||
            restored.observation->source != L"stream-summary" || restored.observation->outputId != L"test-endpoint")
            return 13;
        DetectionResult failed;
        failed.errorCode = E_ACCESSDENIED; failed.errorApi = L"StartTraceW";
        failed.capture.started = true; failed.capture.statisticsAvailable = true;
        failed.capture.providersEnabled = 2; failed.capture.events = 17; failed.capture.stopStatus = 0;
        failed.capture.eventsLost = 3; failed.capture.logBuffersLost = 0;
        failed.capture.firstMetadataError = ERROR_NOT_FOUND; failed.capture.firstMetadataEventFlags = 0x100;
        auto failurePacket = Encode(failed, token, L"permission denied", L"00000000-0000-4000-8000-000000000001");
        auto failureRestored = Decode(failurePacket, token);
        if (failureRestored.errorCode != static_cast<uint32_t>(E_ACCESSDENIED) || failureRestored.errorApi != L"StartTraceW" ||
            !failureRestored.capture.statisticsAvailable || failureRestored.capture.events != 17 ||
            failureRestored.capture.eventsLost != 3u || failureRestored.capture.realtimeBuffersLost.has_value() ||
            failureRestored.capture.firstMetadataError != static_cast<uint32_t>(ERROR_NOT_FOUND) ||
            failureRestored.capture.firstMetadataEventFlags != 0x100u) return 28;
        auto badCapture = failurePacket.GetNamedObject(L"Capture");
        badCapture.SetNamedValue(L"Events", JsonValue::CreateNumberValue(-1));
        try { Decode(failurePacket, token); return 29; } catch (hresult_invalid_argument const &) { }
        try
        {
            Decode(Encode(original, token), RandomHex(32));
            return 14;
        }
        catch (hresult_access_denied const &)
        {
        }
        std::vector<std::wstring> valid{L"--elevated-helper", L"--pipe", L"test-1",     L"--token", token,
                                        L"--timeout-seconds", L"30",     L"--play-tone"};
        if (Parse(valid).seconds != 30)
            return 15;
        auto correlated = valid;
        correlated.insert(correlated.end() - 1, {L"--attempt-id", L"00000000-0000-4000-8000-000000000001"});
        if (Parse(correlated).attemptId != L"00000000-0000-4000-8000-000000000001") return 30;
        correlated[8] = L"------------------------------------";
        try { Parse(correlated); return 39; } catch (hresult_invalid_argument const &) { }
        auto invalid = valid;
        invalid[6] = L"61";
        try
        {
            Parse(invalid);
            return 16;
        }
        catch (...)
        {
        }
        invalid = valid;
        invalid.push_back(L"extra");
        try
        {
            Parse(invalid);
            return 17;
        }
        catch (...)
        {
        }
        invalid = valid;
        invalid[2] = L"../pipe";
        try
        {
            Parse(invalid);
            return 18;
        }
        catch (...)
        {
        }
        invalid = valid;
        invalid[4] = L"bad";
        try
        {
            Parse(invalid);
            return 19;
        }
        catch (...)
        {
        }
        Cancellation canceled;
        canceled.Cancel();
        if (!DetectWithElevation(1, false, canceled, nullptr).canceled)
            return 20;
        // Exercise polling even when no watcher event arrives, and make sure
        // Stop joins the worker before captured callback state is destroyed.
        std::mutex gate;
        std::condition_variable refreshed;
        uint64_t revision{};
        bool periodic{}, ordered{true};
        auto started = std::chrono::steady_clock::now();
        BluetoothAudioDeviceWatcher watcher;
        watcher.Start([&](BluetoothStatus status) {
            std::scoped_lock lock(gate);
            ordered = ordered && status.revision > revision;
            revision = status.revision;
            periodic = std::chrono::steady_clock::now() - started >= std::chrono::seconds(2);
            refreshed.notify_one();
        });
        bool completed{};
        {
            std::unique_lock lock(gate);
            completed = refreshed.wait_for(lock, std::chrono::seconds(7), [&] { return periodic; });
        }
        watcher.Stop();
        watcher.Stop();
        if (!completed || !ordered || revision < 2)
            return 22;
        Log(L"self-test", L"Native service tests passed.");
        return 0;
    }
    catch (...)
    {
        Log(L"self-test", to_message());
        return 21;
    }
}
} // namespace Native
