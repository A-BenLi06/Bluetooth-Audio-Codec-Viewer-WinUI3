#pragma once
#include "BluetoothConnectionState.h"
#include <functional>
#include <memory>
namespace Native
{
class BluetoothAudioDeviceWatcher
{
  public:
    BluetoothAudioDeviceWatcher();
    ~BluetoothAudioDeviceWatcher();
    void Start(std::function<void(BluetoothStatus)> callback);
    void Stop() noexcept;

  private:
    struct State;
    std::shared_ptr<State> m_state;
};
} // namespace Native
