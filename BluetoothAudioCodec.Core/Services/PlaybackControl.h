#pragma once
#include "NativeServices.h"
namespace Native
{
// Owned by one detection, including exception and cancellation cleanup.
class PlaybackControl
{
  public:
    explicit PlaybackControl(AudioOutput const &target);
    ~PlaybackControl();
    void Pause(Cancellation const &cancel, std::function<void(PlaybackProgress const &)> const &progress);
    void Restore() noexcept;
    void CopyTo(DetectionResult &result) const;
    static bool CleanupPending() noexcept;
  private:
    friend int RunPlaybackControlTests();
    friend int ExercisePlaybackRecovery(std::wstring const &path);
    struct State;
    std::shared_ptr<State> m_state;
};
int RunPlaybackControlTests();
int InspectPlayback(std::wstring const &path);
int ExercisePlaybackRecovery(std::wstring const &path);
}
