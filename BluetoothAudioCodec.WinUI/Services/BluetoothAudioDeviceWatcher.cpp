#include "pch.h"
#include "BluetoothAudioDeviceWatcher.h"
#include "BluetoothConnectionQuery.h"
#include "NativeServices.h"
#include <condition_variable>
using namespace winrt;
using namespace Windows::Devices::Enumeration;
namespace Native
{
BluetoothStatus ReadBluetoothConnections()
{
    struct Api
    {
        auto FirstRadio(BLUETOOTH_FIND_RADIO_PARAMS *p, HANDLE *h) { return BluetoothFindFirstRadio(p, h); }
        auto NextRadio(HBLUETOOTH_RADIO_FIND f, HANDLE *h) { return BluetoothFindNextRadio(f, h); }
        void CloseRadioFind(HBLUETOOTH_RADIO_FIND f) { BluetoothFindRadioClose(f); }
        void CloseRadio(HANDLE h) { CloseHandle(h); }
        auto FirstDevice(BLUETOOTH_DEVICE_SEARCH_PARAMS *p, BLUETOOTH_DEVICE_INFO *d)
        { return BluetoothFindFirstDevice(p, d); }
        auto NextDevice(HBLUETOOTH_DEVICE_FIND f, BLUETOOTH_DEVICE_INFO *d) { return BluetoothFindNextDevice(f, d); }
        void CloseDeviceFind(HBLUETOOTH_DEVICE_FIND f) { BluetoothFindDeviceClose(f); }
        DWORD Error() { return GetLastError(); }
    } api;
    return Detail::QueryBluetoothConnections(api);
}
struct BluetoothAudioDeviceWatcher::State
{
    std::mutex gate;
    std::condition_variable changed;
    bool stopped{}, started{}, refresh{true};
    std::thread worker;
    DeviceWatcher bluetooth{nullptr}, audio{nullptr};
    DeviceWatcher::Added_revoker added, audioAdded;
    DeviceWatcher::Updated_revoker updated, audioUpdated;
    DeviceWatcher::Removed_revoker removed, audioRemoved;
    DeviceWatcher::EnumerationCompleted_revoker enumeration;
    DeviceWatcher::Stopped_revoker stoppedEvent;
    std::function<void(BluetoothStatus)> callback;
    void Refresh()
    {
        std::scoped_lock lock(gate);
        refresh = true;
        changed.notify_one();
    }
    void Run() noexcept
    {
        uint64_t revision{};
        uint32_t previousError{};
        for (;;)
        {
            {
                std::unique_lock lock(gate);
                changed.wait_for(lock, std::chrono::seconds(2), [&] { return stopped || refresh; });
                if (stopped)
                    return;
                refresh = false;
            }
            BluetoothStatus status;
            try
            {
                status = ReadBluetoothConnections();
            }
            catch (...)
            {
                status.state = ConnectionState::Unavailable;
                status.error = ERROR_GEN_FAILURE;
                Log(L"bluetooth-snapshot", to_message());
            }
            try
            {
                status.revision = ++revision;
                if (status.error && status.error != previousError)
                    Log(L"bluetooth-snapshot", std::to_wstring(status.error));
                previousError = status.error;
                std::function<void(BluetoothStatus)> notify;
                {
                    std::scoped_lock lock(gate);
                    if (stopped)
                        return;
                    notify = callback;
                }
                if (notify)
                    notify(std::move(status));
            }
            catch (...)
            {
                Log(L"bluetooth-snapshot-callback", to_message());
            }
        }
    }
};
BluetoothAudioDeviceWatcher::BluetoothAudioDeviceWatcher() : m_state(std::make_shared<State>()) {}
BluetoothAudioDeviceWatcher::~BluetoothAudioDeviceWatcher() { Stop(); }
void BluetoothAudioDeviceWatcher::Start(std::function<void(BluetoothStatus)> callback)
{
    auto state = m_state;
    {
        std::scoped_lock lock(state->gate);
        if (state->started || state->stopped)
            return;
        state->callback = std::move(callback);
        state->worker = std::thread([state] { state->Run(); });
        state->started = true;
    }
    std::weak_ptr<State> weak = state;
    // Pairing properties can remain true with the radio off. Events only
    // invalidate; the worker always replaces the entire connection snapshot.
    // Its timer also reconciles missed events, radio toggles and resume.
    try
    {
        auto watcher = DeviceInformation::CreateWatcher(
            Windows::Devices::Bluetooth::BluetoothDevice::GetDeviceSelectorFromPairingState(true),
            {L"System.Devices.Aep.IsConnected"}, DeviceInformationKind::AssociationEndpoint);
        state->bluetooth = watcher;
        auto refresh = [weak](auto const &, auto const &) { if (auto s = weak.lock()) s->Refresh(); };
        state->added = watcher.Added(auto_revoke, refresh);
        state->updated = watcher.Updated(auto_revoke, refresh);
        state->removed = watcher.Removed(auto_revoke, refresh);
        state->enumeration = watcher.EnumerationCompleted(auto_revoke, refresh);
        state->stoppedEvent = watcher.Stopped(auto_revoke, refresh);
        watcher.Start();
    }
    catch (...) { Log(L"bluetooth-watcher-start", to_message()); }
    try
    {
        auto watcher = DeviceInformation::CreateWatcher(Windows::Media::Devices::MediaDevice::GetAudioRenderSelector());
        state->audio = watcher;
        auto refresh = [weak](auto const &, auto const &) { if (auto s = weak.lock()) s->Refresh(); };
        state->audioAdded = watcher.Added(auto_revoke, refresh);
        state->audioUpdated = watcher.Updated(auto_revoke, refresh);
        state->audioRemoved = watcher.Removed(auto_revoke, refresh);
        watcher.Start();
    }
    catch (...) { Log(L"audio-watcher-start", to_message()); }
}
void BluetoothAudioDeviceWatcher::Stop() noexcept
{
    auto state = m_state;
    {
        std::scoped_lock lock(state->gate);
        if (state->stopped)
            return;
        state->stopped = true;
        state->callback = {};
        state->changed.notify_one();
    }
    state->added.revoke();
    state->updated.revoke();
    state->removed.revoke();
    state->enumeration.revoke();
    state->stoppedEvent.revoke();
    state->audioAdded.revoke();
    state->audioUpdated.revoke();
    state->audioRemoved.revoke();
    for (auto const &watcher : {state->bluetooth, state->audio})
    {
        try
        {
            if (watcher && (watcher.Status() == DeviceWatcherStatus::Started ||
                            watcher.Status() == DeviceWatcherStatus::EnumerationCompleted))
                watcher.Stop();
        }
        catch (...) { Log(L"device-watcher-stop", to_message()); }
    }
    if (state->worker.joinable())
        state->worker.join();
    state->bluetooth = nullptr;
    state->audio = nullptr;
}
} // namespace Native
