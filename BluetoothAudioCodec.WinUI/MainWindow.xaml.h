#pragma once
#include "MainWindow.g.h"
#include "NativeServices.h"
#include "DiagnosticsReport.h"
#include "BluetoothAudioDeviceWatcher.h"
namespace winrt::BluetoothAudioCodec::WinUI::implementation
{
struct MainWindow : MainWindowT<MainWindow>
{
    MainWindow();
    void OnRootLoaded(Windows::Foundation::IInspectable const &, Microsoft::UI::Xaml::RoutedEventArgs const &);
    winrt::fire_and_forget OnDetectClicked(Windows::Foundation::IInspectable const &,
                                           Microsoft::UI::Xaml::RoutedEventArgs const &);
    winrt::fire_and_forget OnReportClicked(Windows::Foundation::IInspectable const &, Microsoft::UI::Xaml::RoutedEventArgs const &);
    winrt::fire_and_forget OnGitHubClicked(Windows::Foundation::IInspectable const &, Microsoft::UI::Xaml::RoutedEventArgs const &);
    void OnCopySummaryClicked(Windows::Foundation::IInspectable const &, Microsoft::UI::Xaml::RoutedEventArgs const &);
    void OnReportFolderClicked(Windows::Foundation::IInspectable const &, Microsoft::UI::Xaml::RoutedEventArgs const &);

  private:
    Native::BluetoothAudioDeviceWatcher m_watcher;
    std::shared_ptr<Native::Cancellation> m_detection;
    bool m_loaded{}, m_closed{};
    bool m_reportBusy{};
    bool m_detectionTimedOut{};
    bool m_closeRequested{};
    Microsoft::UI::Dispatching::DispatcherQueueTimer m_detectionTimer{nullptr};
    void StartDetectionTimer(std::shared_ptr<Native::Cancellation> const &cancel);
    std::shared_ptr<Native::DiagnosticReport> m_report;
    std::wstring m_reportPath;
    std::vector<hstring> m_previous;
    Native::BluetoothStatusTracker m_connections;
    void ConfigureWindow();
    void ApplyBluetoothStatus(Native::BluetoothStatus const &);
    void ClearObservation();
    void RefreshOutput();
    void SetReady();
    void ShowObservation(Native::Observation const &);
    void ShowMessage(Microsoft::UI::Xaml::Controls::InfoBarSeverity, hstring const &, hstring const &);
    void RestorePresentation();
    Microsoft::UI::Xaml::Media::Brush Theme(wchar_t const *key);
};
} // namespace winrt::BluetoothAudioCodec::WinUI::implementation
namespace winrt::BluetoothAudioCodec::WinUI::factory_implementation
{
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
{
};
} // namespace winrt::BluetoothAudioCodec::WinUI::factory_implementation
