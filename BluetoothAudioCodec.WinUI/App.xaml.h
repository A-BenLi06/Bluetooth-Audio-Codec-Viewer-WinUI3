#pragma once
#include "App.xaml.g.h"
#include "MainWindow.xaml.h"
namespace winrt::BluetoothAudioCodec::WinUI::implementation
{
struct App : AppT<App>
{
    App();
    void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const &);

  private:
    Microsoft::UI::Xaml::Window m_window{nullptr};
};
} // namespace winrt::BluetoothAudioCodec::WinUI::implementation
