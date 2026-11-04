#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "NativeServices.h"
namespace winrt::BluetoothAudioCodec::WinUI::implementation
{
App::App()
{
    InitializeComponent();
    UnhandledException([](auto const &, Microsoft::UI::Xaml::UnhandledExceptionEventArgs const &args) {
        Native::Log(L"xaml-unhandled", args.Message());
    });
}
void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const &)
{
    m_window = make<MainWindow>();
    m_window.Activate();
}
} // namespace winrt::BluetoothAudioCodec::WinUI::implementation
