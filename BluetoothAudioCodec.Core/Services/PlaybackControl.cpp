#include "pch.h"
#include "PlaybackControl.h"
#include "PlaybackPolicy.h"
#include <winrt/Windows.Media.Control.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <filesystem>
#include <fstream>
#include <bcrypt.h>
using namespace winrt;
using namespace Windows::Media::Control;
using namespace Windows::Foundation;
namespace Native
{
namespace
{
std::atomic<unsigned> PendingControls{};
struct AudioSources
{
    std::vector<std::wstring> names;
    bool complete{true};
    uint32_t active{};
};
std::wstring ProcessName(DWORD pid)
{
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process) return std::wstring(Localize(L"PlaybackUnknownSource"));
    std::wstring path(32768, L'\0'); DWORD length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process.get(), 0, path.data(), &length))
        return std::wstring(Localize(L"PlaybackUnknownSource"));
    path.resize(length);
    return std::filesystem::path(path).filename().wstring();
}
AudioSources ActiveTargetSources(std::wstring const &target, uint64_t deadline, Cancellation const &cancel)
{
    AudioSources result;
    try
    {
        com_ptr<IMMDeviceEnumerator> enumerator;
        check_hresult(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                       __uuidof(IMMDeviceEnumerator), enumerator.put_void()));
        com_ptr<IMMDevice> device;
        check_hresult(enumerator->GetDevice(target.c_str(), device.put()));
        com_ptr<IAudioSessionManager2> manager;
        check_hresult(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, manager.put_void()));
        com_ptr<IAudioSessionEnumerator> sessions;
        check_hresult(manager->GetSessionEnumerator(sessions.put()));
        int count{}; check_hresult(sessions->GetCount(&count));
        if (count > 128) result.complete = false;
        for (int i = 0; i < std::min(count, 128); ++i)
        {
            if (cancel.IsCanceled() || GetTickCount64() >= deadline) { result.complete = false; break; }
            com_ptr<IAudioSessionControl> session;
            check_hresult(sessions->GetSession(i, session.put()));
            AudioSessionState state{}; check_hresult(session->GetState(&state));
            if (state != AudioSessionStateActive) continue;
            auto detail = session.as<IAudioSessionControl2>();
            if (detail->IsSystemSoundsSession() == S_OK) continue;
            DWORD pid{}; auto status = detail->GetProcessId(&pid);
            if (status == S_OK && pid == GetCurrentProcessId()) continue;
            ++result.active;
            // Shared sessions can return a representative PID, not all owners.
            if (status != S_OK || !pid) result.complete = false;
            auto name = pid ? ProcessName(pid) : std::wstring(Localize(L"PlaybackUnknownSource"));
            if (result.names.size() < 6 && std::find(result.names.begin(), result.names.end(), name) == result.names.end())
                result.names.push_back(std::move(name));
        }
    }
    catch (...) { result.complete = false; }
    return result;
}
template<class T> bool Await(IAsyncOperation<T> const &operation, uint64_t deadline, Cancellation const *cancel = nullptr)
{
    while (operation.Status() == AsyncStatus::Started)
    {
        if (GetTickCount64() >= deadline || (cancel && cancel->IsCanceled())) return false;
        // Poll status without installing/replacing an async Completed handler.
        if (cancel) WaitForSingleObject(cancel->event.get(), 20);
        else std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return true;
}
IAsyncOperation<bool> SyntheticControl(unsigned delay, bool result, bool fail = false)
{
    co_await resume_after(std::chrono::milliseconds(delay));
    if (fail) throw hresult_access_denied();
    co_return result;
}
}
struct PlaybackControl::State : std::enable_shared_from_this<State>
{
    struct Entry
    {
        GlobalSystemMediaTransportControlsSession session{nullptr};
        uint32_t ordinal{};
        bool owned{}, pending{}, changed{}, restoring{}, pauseConfirmed{}, monitoring{};
        bool watching{};
        std::wstring restoreBlockReason{L"state-changed"};
        std::wstring changeReason{L"media-or-playback-changed"};
        PlaybackMediaIdentity mediaIdentity;
        event_token playbackToken{};
        DateTime pauseCommandAt{};
        ~Entry()
        {
            if (watching)
            {
                try { session.PlaybackInfoChanged(playbackToken); }
                catch (...) {}
            }
        }
    };
    AudioOutput target;
    GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    std::vector<std::shared_ptr<Entry>> entries;
    mutable std::mutex gate;
    std::vector<PlaybackControlEvent> events;
    std::vector<std::wstring> manualSources;
    bool ending{}, manual{}, restoreFailed{}, restored{};
    std::array<UCHAR, 32> mediaKey{};
    std::once_flag mediaKeyOnce;
    virtual ~State() { SecureZeroMemory(mediaKey.data(), mediaKey.size()); }
    std::optional<PlaybackDigest> Digest(hstring const &value)
    {
        if (value.empty() || value.size() > 2048) return {};
        std::wstring text(value);
        auto first = text.find_first_not_of(L" \t\r\n"), last = text.find_last_not_of(L" \t\r\n");
        if (first == std::wstring::npos) return {};
        text = text.substr(first, last - first + 1);
        std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        auto check = [](NTSTATUS code) { if (code < 0) throw_hresult(static_cast<HRESULT>(code)); };
        std::call_once(mediaKeyOnce, [&] { check(BCryptGenRandom(nullptr, mediaKey.data(), static_cast<ULONG>(mediaKey.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG)); });
        struct Algorithm { BCRYPT_ALG_HANDLE h{}; ~Algorithm() { if (h) BCryptCloseAlgorithmProvider(h, 0); } } algorithm;
        check(BCryptOpenAlgorithmProvider(&algorithm.h, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG));
        struct Hash { BCRYPT_HASH_HANDLE h{}; ~Hash() { if (h) BCryptDestroyHash(h); } } hash;
        check(BCryptCreateHash(algorithm.h, &hash.h, nullptr, 0, mediaKey.data(), static_cast<ULONG>(mediaKey.size()), 0));
        check(BCryptHashData(hash.h, reinterpret_cast<PUCHAR>(text.data()), static_cast<ULONG>(text.size() * sizeof(wchar_t)), 0));
        PlaybackDigest digest{};
        check(BCryptFinishHash(hash.h, digest.data(), static_cast<ULONG>(digest.size()), 0));
        return digest;
    }
    PlaybackMediaIdentity ReadIdentity(std::shared_ptr<Entry> const &entry, uint64_t deadline, Cancellation const *cancel = nullptr)
    {
        try
        {
            auto request = entry->session.TryGetMediaPropertiesAsync();
            if (!Await(request, deadline, cancel)) { request.Cancel(); return {}; }
            auto properties = request.GetResults();
            PlaybackMediaIdentity value;
            value.title = Digest(properties.Title()); value.artist = Digest(properties.Artist()); value.album = Digest(properties.AlbumTitle());
            if (properties.TrackNumber() > 0) value.track = properties.TrackNumber();
            return value;
        }
        catch (...) { return {}; } // Unavailable metadata is not evidence of user input.
    }
    // Isolated control operations allow exercising late completion and cleanup
    // without publishing media sessions or controlling real user playback.
    virtual bool Paused(std::shared_ptr<Entry> const &entry)
    { return entry->session.GetPlaybackInfo().PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused; }
    virtual bool TimelinePublished(std::shared_ptr<Entry> const &entry)
    { return entry->session.GetTimelineProperties().LastUpdatedTime() >= entry->pauseCommandAt; }
    void SnapshotPaused(std::shared_ptr<Entry> const &entry)
    {
        auto paused = Paused(entry);
        std::scoped_lock lock(gate);
        entry->pauseConfirmed = paused;
        entry->monitoring = paused;
    }
    virtual IAsyncOperation<bool> Play(std::shared_ptr<Entry> const &entry) { return entry->session.TryPlayAsync(); }
    void Event(std::wstring action, std::wstring reason, uint32_t ordinal = 0, std::optional<uint32_t> error = {})
    {
        std::scoped_lock lock(gate);
        if (events.size() < 1024) events.push_back({std::move(action), std::move(reason), ordinal, error});
    }
    void ManualSource(std::wstring const &name)
    {
        if (name.empty()) return;
        std::scoped_lock lock(gate);
        if (manualSources.size() < 6 && std::find(manualSources.begin(), manualSources.end(), name) == manualSources.end())
            manualSources.push_back(name);
    }
    void ObservePlayback(std::shared_ptr<Entry> const &entry, GlobalSystemMediaTransportControlsSessionPlaybackStatus status)
    {
        std::scoped_lock lock(gate);
        if (entry->monitoring && !entry->restoring && status != GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused &&
            status != GlobalSystemMediaTransportControlsSessionPlaybackStatus::Changing)
        {
            entry->changed = true;
            entry->changeReason = status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing ? L"playback-playing-observed" :
                status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped ? L"playback-stopped-observed" : L"playback-other-observed";
        }
    }
    void Watch(std::shared_ptr<Entry> const &entry)
    {
        std::weak_ptr<State> weak = shared_from_this();
        std::weak_ptr<Entry> weakEntry = entry;
        entry->watching = true;
        entry->playbackToken = entry->session.PlaybackInfoChanged([weak, weakEntry](auto const &sender, auto const &) {
            try
            {
                auto status = sender.GetPlaybackInfo().PlaybackStatus();
                if (auto self = weak.lock()) if (auto e = weakEntry.lock())
                {
                    self->ObservePlayback(e, status);
                }
            }
            catch (...) {}
        });
    }
    virtual bool Eligible(std::shared_ptr<Entry> const &entry)
    {
        // Metadata reads can wait. Recheck session/playback afterwards,
        // so a user action during that wait cannot start stale playback.
        auto identity = ReadIdentity(entry, GetTickCount64() + 700);
        bool present{};
        for (auto const &session : manager.GetSessions()) if (session == entry->session) present = true;
        auto info = entry->session.GetPlaybackInfo();
        bool paused = info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused;
        std::scoped_lock lock(gate);
        auto blocked = [&](wchar_t const *reason) { entry->restoreBlockReason = reason; return false; };
        if (!entry->owned) return blocked(L"not-owned");
        if (!present) return blocked(L"session-missing");
        if (!paused) return blocked(L"not-paused");
        if (entry->changed) return blocked(entry->changeReason.c_str());
        if (auto reason = PlaybackMediaChange(entry->mediaIdentity, identity); !reason.empty())
            return blocked(reason.data());
        if (!info.Controls().IsPlayEnabled()) return blocked(L"play-unavailable");
        return true;
    }
    static fire_and_forget RestoreOne(std::shared_ptr<State> self, std::shared_ptr<Entry> entry)
    {
        ++PendingControls;
        try
        {
            if (self->Eligible(entry))
            {
                { std::scoped_lock lock(self->gate); entry->restoring = true; }
                bool ok = co_await self->Play(entry);
                self->Event(L"restore", ok ? L"restored" : L"request-rejected", entry->ordinal);
                if (!ok) { std::scoped_lock lock(self->gate); self->restoreFailed = true; }
            }
            else
            {
                self->Event(L"restore", entry->restoreBlockReason, entry->ordinal);
                std::scoped_lock lock(self->gate); self->restoreFailed = true;
            }
        }
        catch (...)
        {
            self->Event(L"restore", L"request-failed", entry->ordinal, static_cast<uint32_t>(to_hresult().value));
            std::scoped_lock lock(self->gate); self->restoreFailed = true;
        }
        { std::scoped_lock lock(self->gate); entry->owned = false; entry->pending = false; }
        Log(L"playback-cleanup", L"A restoration request completed; no media identifiers were saved.");
        --PendingControls;
    }
    static fire_and_forget PauseOne(std::shared_ptr<State> self, std::shared_ptr<Entry> entry, IAsyncOperation<bool> operation)
    {
        ++PendingControls;
        bool success{};
        try
        {
            apartment_context controlApartment;
            success = co_await operation;
            self->Event(L"pause", success ? L"request-accepted" : L"request-rejected", entry->ordinal);
            if (success)
            {
                // The command can be accepted before the player reports Paused.
                // Ownership is retained even if confirmation is delayed.
                auto confirmBy = GetTickCount64() + 400;
                while (!self->Paused(entry) && GetTickCount64() < confirmBy)
                {
                    co_await resume_after(std::chrono::milliseconds(25));
                    co_await controlApartment;
                }
                auto publishBy = GetTickCount64() + 1200;
                while (!self->TimelinePublished(entry) && GetTickCount64() < publishBy)
                {
                    co_await resume_after(std::chrono::milliseconds(50));
                    co_await controlApartment;
                }
                self->Event(L"pause", self->TimelinePublished(entry) ? L"timeline-refreshed" : L"timeline-stale", entry->ordinal);
                // Start recovery monitoring from settled pause state, rather
                // than treating our own Changing/metadata events as user input.
                co_await resume_after(std::chrono::milliseconds(120));
                co_await controlApartment;
                self->SnapshotPaused(entry);
                if (!entry->pauseConfirmed) self->Event(L"pause", L"state-unconfirmed", entry->ordinal);
            }
        }
        catch (...)
        {
            self->Event(L"pause", L"request-failed", entry->ordinal, static_cast<uint32_t>(to_hresult().value));
            if (success) { std::scoped_lock lock(self->gate); entry->changed = true; }
        }
        bool restore{};
        {
            std::scoped_lock lock(self->gate);
            entry->owned = success; entry->pending = false;
            if (!success) self->manual = true;
            restore = success && self->ending;
        }
        if (restore)
        {
            { std::scoped_lock lock(self->gate); entry->pending = true; }
            RestoreOne(self, entry); // A late pause always enters restoration, even after cancellation.
        }
        --PendingControls;
    }
};
PlaybackControl::PlaybackControl(AudioOutput const &target) : m_state(std::make_shared<State>()) { m_state->target = target; }
PlaybackControl::~PlaybackControl() { Restore(); }
bool PlaybackControl::CleanupPending() noexcept { return PendingControls.load() != 0; }
void PlaybackControl::Pause(Cancellation const &cancel, std::function<void(PlaybackProgress const &)> const &progress)
{
    auto self = m_state;
    auto deadline = GetTickCount64() + 5000;
    try
    {
        if (progress) progress({PlaybackPhase::Scanning, {}});
        self->Event(L"scan", L"all-system-media-sessions");
        if (IsElevated()) { self->manual = true; self->Event(L"scan", L"elevated-process"); return; }
        auto request = GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
        if (!Await(request, deadline, &cancel))
        { self->manual = true; self->Event(L"scan", L"request-timeout"); request.Cancel(); return; }
        self->manager = request.GetResults();
        auto sessions = self->manager.GetSessions();
        if (sessions.Size() > 128) { self->manual = true; self->Event(L"scan", L"inventory-incomplete"); return; }
        self->Event(L"scan", L"complete");
        if (progress) progress({PlaybackPhase::Pausing, {}});
        bool skipped{};
        size_t selected{};
        uint32_t ordinal{};
        for (auto const &session : sessions)
        {
            ++ordinal;
            if (cancel.IsCanceled() || GetTickCount64() >= deadline) { skipped = true; break; }
            try
            {
                // Each actual session is independent, even when two sessions
                // share an app ID or render to different output devices.
                if (std::any_of(self->entries.begin(), self->entries.end(), [&](auto const &e) { return e->session == session; })) continue;
                auto info = session.GetPlaybackInfo();
                if (info.PlaybackStatus() != GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing)
                { self->Event(L"match", L"not-playing", ordinal); continue; }
                if (!info.Controls().IsPauseEnabled())
                { skipped = true; self->Event(L"match", L"pause-unavailable", ordinal); continue; }
                auto entry = std::make_shared<State::Entry>();
                entry->session = session; entry->ordinal = ordinal;
                entry->mediaIdentity = self->ReadIdentity(entry, std::min<uint64_t>(deadline, GetTickCount64() + 700), &cancel);
                if (cancel.IsCanceled() || GetTickCount64() >= deadline) { skipped = true; break; }
                bool present{};
                for (auto const &current : self->manager.GetSessions()) if (current == session) present = true;
                if (!present) { skipped = true; self->Event(L"pause", L"session-missing", ordinal); continue; }
                info = session.GetPlaybackInfo();
                if (info.PlaybackStatus() != GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing)
                { self->Event(L"match", L"not-playing", ordinal); continue; }
                if (!info.Controls().IsPauseEnabled())
                { skipped = true; self->Event(L"match", L"pause-unavailable", ordinal); continue; }
                self->Event(L"match", L"system-media-selected", ordinal);
                self->entries.push_back(entry);
                self->Watch(entry);
                entry->pauseCommandAt = clock::now();
                { std::scoped_lock lock(self->gate); entry->pending = true; }
                try { State::PauseOne(self, entry, session.TryPauseAsync()); }
                catch (...) { std::scoped_lock lock(self->gate); entry->pending = false; throw; }
                ++selected;
            }
            catch (...)
            { skipped = true; self->Event(L"pause", L"request-failed", ordinal, static_cast<uint32_t>(to_hresult().value)); }
        }
        if (!selected) self->Event(L"match", L"no-controllable-session");
        for (;;)
        {
            bool pending{}, failed{};
            { std::scoped_lock lock(self->gate);
              for (auto const &e : self->entries) { pending |= e->pending; failed |= !e->pending && !e->pauseConfirmed; }
              self->manual = skipped || failed || pending; }
            if (!pending) break;
            if (cancel.IsCanceled() || GetTickCount64() >= deadline) { self->Event(L"pause", L"request-timeout"); break; }
            WaitForSingleObject(cancel.event.get(), 20);
        }
        // Observation only: a paused player may keep an audio stream running.
        // This never determines which global media sessions receive commands.
        if (!cancel.IsCanceled() && GetTickCount64() < deadline)
        {
            auto sources = ActiveTargetSources(self->target.id, std::min<uint64_t>(deadline, GetTickCount64() + 500), cancel);
            for (auto const &name : sources.names) self->ManualSource(name);
            self->Event(L"scan", sources.active ? L"audio-streams-active" : sources.complete ? L"audio-streams-idle" : L"audio-inventory-incomplete");
            if (!sources.complete && sources.active) self->Event(L"scan", L"audio-inventory-incomplete");
            std::scoped_lock lock(self->gate); self->manual |= sources.active || !sources.complete;
        }
    }
    catch (...)
    {
        self->Event(L"scan", L"request-failed", 0, static_cast<uint32_t>(to_hresult().value));
        std::scoped_lock lock(self->gate); self->manual = true;
    }
}
void PlaybackControl::Restore() noexcept
{
    auto self = m_state;
    try
    {
        std::vector<std::shared_ptr<State::Entry>> restore;
        { std::scoped_lock lock(self->gate);
          if (self->restored) return;
          self->restored = true; self->ending = true;
          for (auto const &e : self->entries) if (e->owned && !e->pending) { e->pending = true; restore.push_back(e); } }
        for (auto const &e : restore) State::RestoreOne(self, e);
        auto deadline = GetTickCount64() + 5000;
        for (;;)
        {
            bool pending{};
            { std::scoped_lock lock(self->gate); for (auto const &e : self->entries) pending |= e->pending; }
            if (!pending) return;
            if (GetTickCount64() >= deadline) { self->Event(L"restore", L"cleanup-pending"); return; }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    catch (...) { Log(L"playback-cleanup", L"Restoration could not finish."); }
}
void PlaybackControl::CopyTo(DetectionResult &result) const
{
    std::scoped_lock lock(m_state->gate);
    result.playback = m_state->events;
    result.playbackManualRequired = m_state->manual;
    result.playbackManualSources = m_state->manualSources;
    result.playbackRestoreFailed = m_state->restoreFailed;
    for (auto const &e : m_state->entries) result.playbackCleanupPending |= e->pending;
}
int RunPlaybackControlTests()
{
    // Missing/backfilled metadata and timeline refreshes are not proof of a
    // media change. Different known identity fields are proof.
    PlaybackDigest a{}, b{}; b[0] = 1;
    PlaybackMediaIdentity identity{a, a, a, 1}, unavailable;
    if (!PlaybackMediaChange(identity, identity).empty() ||
        !PlaybackMediaChange(identity, unavailable).empty() ||
        !PlaybackMediaChange(unavailable, identity).empty()) return 216;
    for (int field = 0; field < 4; ++field)
    {
        auto changed = identity;
        if (field == 0) changed.title = b;
        if (field == 1) changed.artist = b;
        if (field == 2) changed.album = b;
        if (field == 3) changed.track = 2;
        if (PlaybackMediaChange(identity, changed).empty()) return 221;
    }
    int result{};
    std::jthread worker([&] {
        init_apartment(apartment_type::multi_threaded);
        struct Apartment { ~Apartment() { uninit_apartment(); } } apartment;
        try
        {
            struct FakeState : PlaybackControl::State
            {
                bool eligible{true}, playResult{true}, playError{};
                std::atomic<unsigned> plays{};
                bool Paused(std::shared_ptr<Entry> const &) override { return true; }
                bool TimelinePublished(std::shared_ptr<Entry> const &) override { return true; }
                bool Eligible(std::shared_ptr<Entry> const &entry) override { return eligible && !entry->changed; }
                IAsyncOperation<bool> Play(std::shared_ptr<Entry> const &) override
                { ++plays; return SyntheticControl(15, playResult, playError); }
            };
            auto settle = [] {
                auto deadline = GetTickCount64() + 3000;
                while (PlaybackControl::CleanupPending() && GetTickCount64() < deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                return !PlaybackControl::CleanupPending();
            };
            auto hashTest = std::make_shared<FakeState>();
            auto otherAttempt = std::make_shared<FakeState>();
            auto digest = hashTest->Digest(L" PRIVATE TEST MEDIA ");
            if (!digest || digest != hashTest->Digest(L"private test media") ||
                digest == otherAttempt->Digest(L"private test media") || hashTest->Digest(L" \t").has_value())
                { result = 222; return; }
            // A pause that finishes after the detection has ended must still
            // resume exactly the session whose request succeeded.
            for (int scenario = 0; scenario < 6; ++scenario)
            {
                auto self = std::make_shared<FakeState>();
                auto entry = std::make_shared<PlaybackControl::State::Entry>();
                entry->ordinal = 1; entry->pending = true;
                self->entries.push_back(entry);
                self->eligible = scenario != 3;
                self->playResult = scenario != 4;
                self->playError = scenario == 5;
                auto operation = SyntheticControl(65, scenario != 1, scenario == 2);
                if (Await(operation, GetTickCount64() + 15)) { result = 186; return; }
                PlaybackControl::State::PauseOne(self, entry, operation);
                { std::scoped_lock lock(self->gate); self->ending = true; }
                if (!settle()) { result = 187; return; }
                auto expected = scenario == 0 || scenario == 4 || scenario == 5 ? 1U : 0U;
                if (self->plays != expected || entry->owned || entry->pending) { result = 188; return; }
                if (scenario >= 3 && !self->restoreFailed) { result = 189; return; }
                if ((scenario == 1 || scenario == 2) && !self->manual) { result = 190; return; }
                if (scenario == 0 && self->restoreFailed) { result = 191; return; }
            }
            for (int scenario = 0; scenario < 2; ++scenario)
            {
                auto self = std::make_shared<FakeState>();
                auto entry = std::make_shared<PlaybackControl::State::Entry>();
                entry->ordinal = 1; entry->pending = true;
                self->entries.push_back(entry);
                // Our own pause transition and metadata publication must not
                // be mistaken for user input before the paused baseline.
                self->ObservePlayback(entry, GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);
                if (entry->changed) { result = 217; return; }
                PlaybackControl::State::PauseOne(self, entry, SyntheticControl(15, true));
                if (!settle() || !entry->owned || !entry->monitoring) { result = 218; return; }
                self->ObservePlayback(entry, GlobalSystemMediaTransportControlsSessionPlaybackStatus::Changing);
                if (entry->changed) { result = 219; return; }
                if (scenario == 1)
                {
                    self->ObservePlayback(entry, GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);
                    self->ObservePlayback(entry, GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused);
                }
                { std::scoped_lock lock(self->gate); entry->pending = true; }
                PlaybackControl::State::RestoreOne(self, entry);
                if (!settle() || self->plays != (scenario == 0 ? 1U : 0U)) { result = 220; return; }
            }
            // Recovery covers every owned session and never starts a session
            // that was already paused, independently of target-output identity.
            {
                auto self = std::make_shared<FakeState>();
                for (uint32_t i = 0; i < 3; ++i)
                {
                    auto entry = std::make_shared<PlaybackControl::State::Entry>();
                    entry->ordinal = i + 1; entry->owned = i != 2;
                    self->entries.push_back(entry);
                }
                PlaybackControl control(AudioOutput{}); control.m_state = self;
                control.Restore(); control.Restore();
                if (!settle() || self->plays != 2 || self->entries[2]->owned ||
                    std::any_of(self->entries.begin(), self->entries.end(), [](auto const &e) { return e->owned || e->pending; }))
                    { result = 246; return; }
            }
            Log(L"playback-self-test", L"All-owned-session recovery, originally paused exclusion, identity guards, late pause and cleanup passed without controlling media.");
        }
        catch (...) { result = 192; }
    });
    worker.join();
    return result;
}
int InspectPlayback(std::wstring const &path)
{
    int code{};
    std::jthread worker([&] {
        init_apartment(apartment_type::multi_threaded);
        struct Apartment { ~Apartment() { uninit_apartment(); } } apartment;
        using namespace Windows::Data::Json;
        JsonObject result;
        auto number = [&](wchar_t const *key, unsigned value) { result.SetNamedValue(key, JsonValue::CreateNumberValue(value)); };
        result.SetNamedValue(L"control_scope", JsonValue::CreateStringValue(L"all-system-media-sessions"));
        try
        {
            auto target = DefaultOutputInfo(); Cancellation cancel;
            auto deadline = GetTickCount64() + 5000;
            auto sources = ActiveTargetSources(target.id, GetTickCount64() + 500, cancel);
            result.SetNamedValue(L"audio_inventory_complete", JsonValue::CreateBooleanValue(sources.complete));
            number(L"active_target_audio_sessions", sources.active);
            number(L"target_transport", static_cast<unsigned>(target.transport));
            auto request = GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
            if (!Await(request, deadline)) { request.Cancel(); throw hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT)); }
            auto sessions = request.GetResults().GetSessions();
            if (sessions.Size() > 128) throw hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
            number(L"media_sessions", sessions.Size());
            unsigned playing{}, controllable{};
            for (auto const &session : sessions)
            {
                auto info = session.GetPlaybackInfo();
                if (info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing)
                { ++playing; if (info.Controls().IsPauseEnabled()) ++controllable; }
            }
            number(L"playing_media_sessions", playing); number(L"controllable_playing_sessions", controllable); number(L"error", 0);
        }
        catch (...) { number(L"error", static_cast<uint32_t>(to_hresult().value)); code = 1; }
        try { std::ofstream output(std::filesystem::path(path), std::ios::binary); output << to_string(result.Stringify()) << '\n'; if (!output) code = 2; }
        catch (...) { code = 2; }
    });
    worker.join(); return code;
}
int ExercisePlaybackRecovery(std::wstring const &path)
{
    int code{1};
    std::jthread worker([&] {
        init_apartment(apartment_type::multi_threaded);
        struct Apartment { ~Apartment() { uninit_apartment(); } } apartment;
        using namespace Windows::Data::Json;
        JsonObject record; JsonArray events;
        try
        {
            auto target = DefaultOutputInfo();
            if (target.transport != OutputTransport::Classic || !target.address || IsElevated()) throw hresult_invalid_argument();
            PlaybackControl playback(target);
            Cancellation cancel;
            playback.Pause(cancel, {});
            Cancellation toneFinished;
            PlayDiagnosticTone(target, cancel, toneFinished);
            record.SetNamedValue(L"diagnostic_tone_played", JsonValue::CreateBooleanValue(true));
            std::this_thread::sleep_for(std::chrono::seconds(10));
            playback.Restore();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            DetectionResult result; playback.CopyTo(result);
            unsigned paused{}, resumed{};
            for (auto const &e : result.playback)
            {
                JsonObject item;
                item.SetNamedValue(L"action", JsonValue::CreateStringValue(e.action));
                item.SetNamedValue(L"reason", JsonValue::CreateStringValue(e.reason));
                item.SetNamedValue(L"session", JsonValue::CreateNumberValue(e.session));
                item.SetNamedValue(L"error", e.error ? JsonValue::CreateNumberValue(*e.error) : JsonValue::CreateNullValue());
                events.Append(item);
                if (e.action == L"pause" && e.reason == L"request-accepted") ++paused;
            }
            for (auto const &entry : playback.m_state->entries)
                if (entry->pauseConfirmed && entry->session.GetPlaybackInfo().PlaybackStatus() ==
                    GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) ++resumed;
            record.SetNamedValue(L"paused_sessions", JsonValue::CreateNumberValue(paused));
            record.SetNamedValue(L"resumed_sessions", JsonValue::CreateNumberValue(resumed));
            record.SetNamedValue(L"restore_failed", JsonValue::CreateBooleanValue(result.playbackRestoreFailed));
            record.SetNamedValue(L"cleanup_pending", JsonValue::CreateBooleanValue(result.playbackCleanupPending));
            if (paused && paused == resumed && !result.playbackRestoreFailed && !result.playbackCleanupPending) code = 0;
        }
        catch (...) { record.SetNamedValue(L"error", JsonValue::CreateNumberValue(static_cast<uint32_t>(to_hresult().value))); }
        record.SetNamedValue(L"events", events);
        record.SetNamedValue(L"passed", JsonValue::CreateBooleanValue(code == 0));
        std::ofstream output(std::filesystem::path(path), std::ios::binary);
        output << to_string(record.Stringify()) << '\n';
        if (!output) code = 2;
    });
    worker.join();
    return code;
}
}
