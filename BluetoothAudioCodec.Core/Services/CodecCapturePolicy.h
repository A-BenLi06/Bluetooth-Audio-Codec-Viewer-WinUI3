#pragma once
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <optional>
#include <string>
#include <string_view>
namespace Native
{
enum class OutputTransport { Unknown, Other, Classic, HandsFree, LowEnergy };
inline std::wstring Upper(std::wstring_view text)
{
    std::wstring value(text);
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });
    return value;
}
inline uint64_t BluetoothAddressFromInstance(std::wstring_view instance)
{
    auto text = Upper(instance);
    // Only a Bluetooth remote devnode is identity evidence. Never infer an
    // address from a friendly name, arbitrary hex substring, or local adapter.
    constexpr std::wstring_view prefix = L"BTHENUM\\DEV_";
    if (!text.starts_with(prefix) || text.size() < prefix.size() + 12)
        return 0;
    uint64_t value{};
    for (size_t i = prefix.size(); i < prefix.size() + 12; ++i)
    {
        auto c = text[i];
        if (!((c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'F')))
            return 0;
        value = (value << 4) | (c <= L'9' ? c - L'0' : c - L'A' + 10);
    }
    auto end = prefix.size() + 12;
    if (text.size() != end && text[end] != L'\\')
        return 0;
    return value;
}
enum class CodecEventKind { Other, StreamStart, StreamSummary, Configuration };
inline bool EventNameIs(std::wstring_view candidate, std::wstring_view expected)
{
    auto value = Upper(candidate), name = Upper(expected);
    return value == name || (value.starts_with(name) && value.size() > name.size() && value[name.size()] == L'/');
}
inline CodecEventKind ClassifyCodecEvent(std::wstring_view event, std::wstring_view task)
{
    if (EventNameIs(event, L"A2dpStreamingStart") || EventNameIs(task, L"A2dpStreamingStart"))
        return CodecEventKind::StreamStart;
    if (EventNameIs(event, L"A2dpStreaming") || EventNameIs(task, L"A2dpStreaming"))
        return CodecEventKind::StreamSummary;
    if (EventNameIs(event, L"AvdtpSetConfigInfo") || EventNameIs(task, L"AvdtpSetConfigInfo"))
        return CodecEventKind::Configuration;
    return CodecEventKind::Other; // Available/supported codec lists are never active codec evidence.
}
struct CodecEvidence
{
    CodecEventKind kind{};
    uint64_t timestamp{}, address{}, standard{}, vendor{}, codec{};
    bool sink{};
    uint64_t result{};
};
struct CodecCapturePolicy
{
    uint64_t target{}, started{};
    bool sawStart{};
    std::optional<CodecEvidence> configuration;
    uint64_t lastConfiguration{};
    std::optional<CodecEvidence> Accept(CodecEvidence const &event)
    {
        if (!target || event.address != target || event.timestamp < started || event.sink)
            return {};
        if (event.kind == CodecEventKind::Other)
            return {};
        if (event.kind == CodecEventKind::StreamStart)
        {
            sawStart = true;
            if (configuration && configuration->timestamp <= event.timestamp)
            {
                auto confirmed = *configuration;
                confirmed.timestamp = event.timestamp;
                return confirmed;
            }
            return {};
        }
        if (event.kind == CodecEventKind::Configuration)
        {
            if (event.timestamp < lastConfiguration)
                return {};
            lastConfiguration = event.timestamp;
            if (event.result == 0 && event.standard <= 255 && event.vendor <= UINT32_MAX && event.codec <= UINT16_MAX)
                configuration = event;
            else
                configuration.reset();
            return {}; // A successful configuration alone is not proof of a running source stream.
        }
        if (event.standard > 255 || event.vendor > UINT32_MAX || event.codec > UINT16_MAX)
            return {};
        return event;
    }
};
} // namespace Native
