#pragma once
#include "CodecCapturePolicy.h"
namespace Native
{
class Handle
{
  public:
    Handle() = default;
    explicit Handle(HANDLE value) : m_value(value)
    {
    }
    ~Handle()
    {
        if (m_value && m_value != INVALID_HANDLE_VALUE)
            CloseHandle(m_value);
    }
    Handle(Handle const &) = delete;
    Handle &operator=(Handle const &) = delete;
    HANDLE get() const
    {
        return m_value;
    }
    explicit operator bool() const
    {
        return m_value && m_value != INVALID_HANDLE_VALUE;
    }

  private:
    HANDLE m_value{};
};
struct Cancellation
{
    Handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    void Cancel() const
    {
        SetEvent(event.get());
    }
    bool IsCanceled() const
    {
        return WaitForSingleObject(event.get(), 0) == WAIT_OBJECT_0;
    }
};
struct Observation
{
    uint8_t standard{};
    uint64_t vendor{}, codec{};
    uint64_t timestamp{}; // UTC FILETIME, retains the ETW event's timestamp.
    std::wstring name, output;
    std::wstring outputId, source;
    uint64_t address{};
};
struct CaptureDiagnostics
{
    bool started{}, statisticsAvailable{};
    uint32_t providersEnabled{}, events{}, metadataFailures{}, streamingEvents{}, configurationEvents{}, parseFailures{}, rejected{};
    std::optional<uint32_t> stopStatus, eventsLost, logBuffersLost, realtimeBuffersLost;
    std::optional<uint32_t> firstMetadataError, firstMetadataEventFlags;
};
enum class PlaybackPhase { Scanning, Pausing, Detecting, ManualPause, Restoring };
struct PlaybackProgress
{
    PlaybackPhase phase;
    std::vector<std::wstring> manualSources; // Local UI only; never report/IPC data.
};
struct PlaybackControlEvent
{
    std::wstring action, reason;
    uint32_t session{}; // Attempt-local ordinal, never an app ID or path.
    std::optional<uint32_t> error;
};
struct DetectionResult
{
    std::optional<Observation> observation;
    std::vector<std::wstring> warnings;
    bool canceled{}, elevationCanceled{};
    std::wstring reason;
    // Transported through the authenticated helper pipe, never free-form logs.
    std::optional<uint32_t> errorCode;
    std::wstring errorApi;
    std::wstring errorMessage; // UI only; never copied into a public report.
    CaptureDiagnostics capture;
    bool playbackManualRequired{}, playbackRestoreFailed{}, playbackCleanupPending{};
    std::vector<PlaybackControlEvent> playback;
    std::vector<std::wstring> playbackManualSources; // Local UI only.
};
void Log(std::wstring_view stage, std::wstring_view message) noexcept;
std::wstring PrivacyTag(std::wstring_view value);
std::wstring RedactDiagnostics(std::wstring text);
void LogEnvironment(std::wstring_view role) noexcept;
std::wstring ChooseDiagnosticsPath(HWND owner);
void ExportDiagnostics(std::wstring const &path);
std::wstring CollectRuntimeDiagnostics();
int RunDiagnosticsTests();
winrt::hstring Localize(std::wstring_view key);
std::wstring Format(std::wstring value, std::wstring_view argument);
struct AudioOutput
{
    std::wstring id, name;
    uint64_t address{};
    OutputTransport transport{OutputTransport::Unknown};
    bool microphoneActive{};
    std::wstring identitySource;
    bool microphoneStateKnown{};
};
AudioOutput DefaultOutputInfo() noexcept;
std::wstring DefaultOutput() noexcept;
void PlayDiagnosticTone(AudioOutput const &output, Cancellation const &cancel, Cancellation const &finished);
bool IsElevated();
std::wstring ResolveCodec(uint8_t standard, uint64_t vendor, uint64_t codec);
DetectionResult Detect(unsigned seconds, bool tone, Cancellation const &cancel, std::function<void()> const &ready = {},
                       CaptureDiagnostics *diagnostics = nullptr);
DetectionResult DetectWithElevation(unsigned seconds, bool tone, Cancellation const &cancel, HWND owner,
                                   std::function<void()> const &ready = {},
                                   std::function<void(PlaybackProgress const &)> const &progress = {});
int RunHelper(std::vector<std::wstring> const &args);
int RunSelfTests();
} // namespace Native
