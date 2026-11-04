#pragma once
#include "NativeServices.h"
#include <filesystem>

namespace Native
{
// Structured files use typed/allowlisted data and tagged device identities.
// An explicitly selected runtime-log supplement is filtered and previewed separately.
struct AudioSnapshotNode
{
    std::wstring identity, kind, container, api;
    std::optional<uint32_t> error;
    std::wstring driverClass, driverProvider, driverVersion;
};
struct AudioSnapshotEdge { size_t from{}, to{}; std::wstring relation; };
struct AudioDiagnosticSnapshot
{
    std::vector<AudioSnapshotNode> nodes;
    std::vector<AudioSnapshotEdge> edges;
    bool partial{};
};
AudioDiagnosticSnapshot CaptureAudioDiagnosticSnapshot(AudioOutput const &output, std::span<uint64_t const> connected = {}) noexcept;

struct DiagnosticAttemptInfo { std::wstring id, started, outcome, reason; };
struct DiagnosticReport
{
    std::wstring id, filename, summary, buildId, reason;
    std::vector<std::pair<std::string, std::string>> files;
};
std::wstring DiagnosticUtc();
std::filesystem::path DiagnosticDataDirectory();
void UseDiagnosticTestDirectory();
void CleanupDiagnosticTestDirectory() noexcept;

class DiagnosticAttempt
{
  public:
    static std::unique_ptr<DiagnosticAttempt> Begin(AudioOutput const &output) noexcept;
    ~DiagnosticAttempt();
    std::wstring AttemptId() const;
    void Preflight(AudioOutput const &output, std::wstring_view reason) noexcept;
    void HelperStarted(uint32_t pid) noexcept;
    void Listening() noexcept;
    void Playback(std::span<PlaybackControlEvent const> events) noexcept;
    void HelperExited(uint32_t code) noexcept;
    void Finish(DetectionResult const &result, AudioOutput const &output, CaptureDiagnostics const &capture,
                std::optional<uint32_t> error = {}, std::wstring_view api = L"DetectWithElevation") noexcept;
  private:
    friend int RunDiagnosticReportTests(std::wstring const &directory);
    struct State;
    std::unique_ptr<State> m_state;
    DiagnosticAttempt();
};

std::vector<DiagnosticAttemptInfo> RecentDiagnosticAttempts();
// Empty attemptId creates an application-context report with no fabricated detection.
DiagnosticReport PrepareDiagnosticReport(std::wstring const &attemptId, bool includeRuntimeLog = false);
std::wstring ChooseDiagnosticReportPath(HWND owner, std::wstring const &filename);
void SaveDiagnosticReport(DiagnosticReport const &report, std::wstring const &path);
std::wstring DiagnosticGitHubUrl(DiagnosticReport const &report);
void ShowDiagnosticReportInFolder(std::wstring const &path);
int RunDiagnosticReportTests(std::wstring const &directory);
} // namespace Native
