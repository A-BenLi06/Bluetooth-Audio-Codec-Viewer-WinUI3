#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.g.cpp"
#include <microsoft.ui.xaml.window.h>
#include <format>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.System.h>
#include "PlaybackControl.h"
using namespace winrt;
using namespace Microsoft::UI;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
namespace winrt::BluetoothAudioCodec::WinUI::implementation
{
namespace
{
std::wstring PlaybackSourceText(std::span<std::wstring const> sources)
{
    std::wstring joined;
    for (auto const &source : sources) { if (!joined.empty()) joined += L"; "; joined += source; }
    return joined.empty() ? L"" : L"\n" + Native::Format(std::wstring(Native::Localize(L"PlaybackManualSources")), joined);
}
}
MainWindow::MainWindow()
{
    InitializeComponent();
    Title(Native::Localize(L"AppTitle/Text"));
    ToolTipService::SetToolTip(ReportProblemButton(), box_value(Native::Localize(L"ProblemReportHint/Text")));
    ToolTipService::SetToolTip(SupportLinks(), box_value(Native::Localize(L"SupportTitle/Text") + L"\n" +
                                                      Native::Localize(L"SupportDescription/Text")));
    ExtendsContentIntoTitleBar(true);
    SetTitleBar(TitleBarGrid());
    ConfigureWindow();
    AppWindow().Closing([weak = get_weak()](auto const &, auto const &args) {
        if (auto self = weak.get(); self && (self->m_detection || Native::PlaybackControl::CleanupPending()))
        {
            args.Cancel(true);
            self->m_closeRequested = true;
            if (self->m_detection) self->m_detection->Cancel();
            self->StatusText().Text(Native::Localize(L"PlaybackRestoring"));
            self->ShowMessage(InfoBarSeverity::Informational, Native::Localize(L"PlaybackRestoring"),
                              Native::Localize(L"PlaybackClosing"));
        }
    });
    Closed([weak = get_weak()](auto const &, auto const &) {
        if (auto self = weak.get())
        {
            self->m_closed = true;
            if (self->m_detectionTimer) self->m_detectionTimer.Stop();
            if (self->m_detection)
                self->m_detection->Cancel();
            self->m_watcher.Stop();
        }
    });
}
void MainWindow::ConfigureWindow()
{
    HWND hwnd{};
    check_hresult(this->try_as<IWindowNative>()->get_WindowHandle(&hwnd));
    double scale = GetDpiForWindow(hwnd) / 96.0;
    auto window = AppWindow();
    auto work =
        Windowing::DisplayArea::GetFromWindowId(window.Id(), Windowing::DisplayAreaFallback::Primary).WorkArea();
    int minWidth = static_cast<int>(std::round(680 * scale)), minHeight = static_cast<int>(std::round(600 * scale));
    int width = std::min(static_cast<int>(std::round(860 * scale)), std::max(minWidth, work.Width - 64));
    int height = std::min(static_cast<int>(std::round(680 * scale)), std::max(minHeight, work.Height - 64));
    window.MoveAndResize({work.X + std::max(0, (work.Width - width) / 2),
                          work.Y + std::max(0, (work.Height - height) / 2), width, height});
    window.TitleBar().ButtonBackgroundColor(Colors::Transparent());
    window.TitleBar().ButtonInactiveBackgroundColor(Colors::Transparent());
    if (auto presenter = window.Presenter().try_as<Windowing::OverlappedPresenter>())
    {
        presenter.PreferredMinimumWidth(minWidth);
        presenter.PreferredMinimumHeight(minHeight);
    }
}
Media::Brush MainWindow::Theme(wchar_t const *key)
{
    return Application::Current().Resources().Lookup(box_value(key)).as<Media::Brush>();
}
void MainWindow::SetReady()
{
    StatusText().Text(Native::Localize(L"StatusReady"));
    StatusDot().Fill(Theme(L"TextFillColorSecondaryBrush"));
    DetectButtonText().Text(Native::Localize(L"DetectButtonInitial"));
}
void MainWindow::OnRootLoaded(Windows::Foundation::IInspectable const &, RoutedEventArgs const &)
{
    if (m_loaded)
        return;
    m_loaded = true;
    auto output = Native::DefaultOutput();
    DeviceText().Text(output.empty() ? Native::Localize(L"DeviceNone") : hstring(output));
    ProtocolText().Text(Native::Localize(L"ProtocolDefault"));
    BluetoothStatusText().Text(Native::Localize(L"BluetoothStatusChecking"));
    SetReady();
    auto queue = DispatcherQueue();
    m_watcher.Start([weak = get_weak(), queue](Native::BluetoothStatus status) {
        queue.TryEnqueue([weak, status = std::move(status)] {
            if (auto self = weak.get(); self && !self->m_closed)
                self->ApplyBluetoothStatus(status);
        });
    });
}
void MainWindow::ApplyBluetoothStatus(Native::BluetoothStatus const &status)
{
    auto output = Native::DefaultOutputInfo();
    auto generation = m_connections.generation;
    if (!m_connections.Accept(status, output.id))
        return;
    DeviceText().Text(output.name.empty() ? Native::Localize(L"DeviceNone") : hstring(output.name));
    if (generation != m_connections.generation)
    {
        Native::Log(L"connection-change", std::format(L"state={} connected-targets={} detection-active={}",
                     static_cast<int>(status.state), status.addresses.size(), m_detection != nullptr));
        ClearObservation();
    }
    wchar_t const *brush = L"TextFillColorSecondaryBrush";
    switch (status.state)
    {
    case Native::ConnectionState::Connected: {
        std::wstring names;
        for (auto const &name : status.names)
        {
            if (!names.empty())
                names += L", ";
            names += name;
        }
        if (names.empty())
            names = Native::Localize(L"BluetoothDeviceUnknown");
        BluetoothStatusText().Text(
            Native::Format(std::wstring(Native::Localize(L"BluetoothStatusConnectedFormat")), names));
        brush = L"SystemFillColorSuccessBrush";
        break;
    }
    case Native::ConnectionState::Disconnected:
        BluetoothStatusText().Text(Native::Localize(L"BluetoothStatusDisconnected"));
        break;
    case Native::ConnectionState::Unavailable:
        BluetoothStatusText().Text(Native::Localize(L"BluetoothStatusUnavailable"));
        brush = L"SystemFillColorCautionBrush";
        break;
    default:
        BluetoothStatusText().Text(Native::Localize(L"BluetoothStatusChecking"));
        break;
    }
    BluetoothStatusDot().Fill(Theme(brush));
}
void MainWindow::ClearObservation()
{
    m_previous.clear();
    StandardIdText().Text(L"—");
    VendorIdText().Text(L"—");
    VendorCodecIdText().Text(L"—");
    ObservedAtText().Text(L"—");
    if (m_detection)
        return; // Keep capturing across a reconnect; only a verified current target may be displayed.
    CodecText().Text(L"—");
    ProtocolText().Text(Native::Localize(L"ProtocolDefault"));
    MessageBar().IsOpen(false);
    SetReady();
}
void MainWindow::RefreshOutput()
{
    auto output = Native::DefaultOutputInfo();
    DeviceText().Text(output.name.empty() ? Native::Localize(L"DeviceNone") : hstring(output.name));
    if (m_connections.outputId != output.id)
    {
        m_connections.outputId = output.id;
        ++m_connections.generation;
        ClearObservation();
    }
}
void MainWindow::ShowMessage(InfoBarSeverity severity, hstring const &title, hstring const &message)
{
    MessageBar().Severity(severity);
    MessageBar().Title(title);
    MessageBar().Message(message);
    MessageBar().IsOpen(true);
}
void MainWindow::RestorePresentation()
{
    if (m_previous.size() != 6)
    {
        CodecText().Text(L"—");
        ProtocolText().Text(Native::Localize(L"ProtocolDefault"));
        return;
    }
    CodecText().Text(m_previous[0]);
    ProtocolText().Text(m_previous[1]);
    StandardIdText().Text(m_previous[2]);
    VendorIdText().Text(m_previous[3]);
    VendorCodecIdText().Text(m_previous[4]);
    ObservedAtText().Text(m_previous[5]);
}
void MainWindow::ShowObservation(Native::Observation const &o)
{
    CodecText().Text(o.name);
    ProtocolText().Text(Native::Localize(o.source == L"stream-summary" ? L"ProtocolLastObserved" : L"ProtocolDefault"));
    StandardIdText().Text(std::format(L"0x{:02X}", o.standard));
    VendorIdText().Text(std::format(L"0x{:08X}", o.vendor));
    VendorCodecIdText().Text(std::format(L"0x{:04X}", o.codec));
    FILETIME utc{static_cast<DWORD>(o.timestamp), static_cast<DWORD>(o.timestamp >> 32)}, local{};
    SYSTEMTIME time{};
    FileTimeToLocalFileTime(&utc, &local);
    FileTimeToSystemTime(&local, &time);
    ObservedAtText().Text(std::format(L"{:04}-{:02}-{:02}  {:02}:{:02}:{:02}", time.wYear, time.wMonth, time.wDay,
                                      time.wHour, time.wMinute, time.wSecond));
    StatusText().Text(Native::Localize(L"StatusDetected"));
    StatusDot().Fill(Theme(L"SystemFillColorSuccessBrush"));
    ShowMessage(InfoBarSeverity::Success, Native::Localize(L"MessageCodecDetectedTitle"),
                Native::Localize(L"MessageCodecDetectedBody"));
}
void MainWindow::StartDetectionTimer(std::shared_ptr<Native::Cancellation> const &cancel)
{
    if (m_detectionTimer) m_detectionTimer.Stop();
    m_detectionTimer = DispatcherQueue().CreateTimer();
    m_detectionTimer.Interval(std::chrono::seconds(1));
    auto started = GetTickCount64();
    m_detectionTimer.Tick([weak = get_weak(), cancel, started](auto const &timer, auto const &) {
        auto self = weak.get();
        if (!self || self->m_closed || self->m_detection != cancel) { timer.Stop(); return; }
        auto elapsed = (GetTickCount64() - started) / 1000;
        if (elapsed >= 45)
        {
            timer.Stop(); self->m_detectionTimedOut = true; cancel->Cancel();
            self->DetectionProgress().IsActive(false);
            self->DetectionProgress().Visibility(Visibility::Collapsed);
            self->CodecText().Text(Native::Localize(L"CodecNoEvent"));
            self->StatusText().Text(Native::Localize(L"StatusError"));
            self->StatusDot().Fill(self->Theme(L"SystemFillColorCriticalBrush"));
            self->DetectButtonText().Text(Native::Localize(L"DetectionFinishing"));
            self->DetectButton().IsEnabled(false);
            self->ShowMessage(InfoBarSeverity::Error, Native::Localize(L"MessageUnableTitle"), Native::Localize(L"DetectionDeadlineBody"));
            Native::Log(L"ui-detection-deadline", L"45 seconds after listening; cancellation requested; background cleanup may still be pending.");
        }
        else if (elapsed >= 30 || cancel->IsCanceled())
            self->CodecText().Text(Native::Localize(L"DetectionFinishing"));
        else
            self->CodecText().Text(Native::Format(std::wstring(Native::Localize(L"DetectionRemaining")), std::to_wstring(30 - elapsed)));
    });
    m_detectionTimer.Start();
}
fire_and_forget MainWindow::OnDetectClicked(Windows::Foundation::IInspectable const &, RoutedEventArgs const &)
{
    if (m_detection)
    {
        m_detection->Cancel();
        co_return;
    }
    auto lifetime = get_strong();
    if (m_reportBusy) co_return;
    if (Native::PlaybackControl::CleanupPending())
    {
        ShowMessage(InfoBarSeverity::Warning, Native::Localize(L"PlaybackRestoring"), Native::Localize(L"PlaybackCleanupPending"));
        co_return;
    }
    apartment_context ui;
    RefreshOutput();
    auto cancel = std::make_shared<Native::Cancellation>();
    m_detection = cancel;
    m_detectionTimedOut = false;
    m_previous = {CodecText().Text(),    ProtocolText().Text(),      StandardIdText().Text(),
                  VendorIdText().Text(), VendorCodecIdText().Text(), ObservedAtText().Text()};
    StandardIdText().Text(L"—");
    VendorIdText().Text(L"—");
    VendorCodecIdText().Text(L"—");
    ObservedAtText().Text(L"—");
    CodecText().Text(Native::Localize(L"CodecPreparing"));
    ProtocolText().Text(Native::Localize(L"ProtocolWaiting"));
    StatusText().Text(Native::Localize(L"StatusPreparing"));
    StatusDot().Fill(Theme(L"AccentFillColorDefaultBrush"));
    MessageBar().IsOpen(false);
    DetectionProgress().Visibility(Visibility::Visible);
    DetectionProgress().IsActive(true);
    DetectIcon().Visibility(Visibility::Collapsed);
    DetectButtonText().Text(Native::Localize(L"DetectButtonCancel"));
    HWND owner{};
    this->try_as<IWindowNative>()->get_WindowHandle(&owner);
    Native::DetectionResult result;
    hstring error;
    auto queue = DispatcherQueue();
    auto weak = get_weak();
    co_await resume_background();
    try
    {
        // Windows thread-pool callbacks do not initialize COM for us. Keep this
        // apartment scoped to the background work, before returning to the STA.
        init_apartment(apartment_type::multi_threaded);
        struct ApartmentScope
        {
            ~ApartmentScope()
            {
                uninit_apartment();
            }
        } apartment;
        result = Native::DetectWithElevation(30, true, *cancel, owner, [weak, queue, cancel] {
            queue.TryEnqueue([weak, cancel] {
                if (auto self = weak.get(); self && !self->m_closed && self->m_detection == cancel && !cancel->IsCanceled())
                {
                    self->CodecText().Text(Native::Localize(L"CodecListening"));
                    self->StatusText().Text(Native::Localize(L"StatusListening"));
                    self->StartDetectionTimer(cancel);
                    self->ShowMessage(InfoBarSeverity::Informational, Native::Localize(L"StatusListening"),
                                      Native::Localize(L"MessageListeningBody"));
                }
            });
        }, [weak, queue, cancel](Native::PlaybackProgress const &update) {
            queue.TryEnqueue([weak, cancel, phase = update.phase, sources = update.manualSources] {
                if (auto self = weak.get(); self && !self->m_closed && self->m_detection == cancel && !self->m_detectionTimedOut)
                {
                    auto key = phase == Native::PlaybackPhase::Scanning ? L"PlaybackScanning" :
                               phase == Native::PlaybackPhase::Pausing ? L"PlaybackPausing" :
                               phase == Native::PlaybackPhase::Restoring ? L"PlaybackRestoring" : L"StatusListening";
                    self->StatusText().Text(Native::Localize(key));
                    if (phase == Native::PlaybackPhase::ManualPause)
                        self->ShowMessage(InfoBarSeverity::Warning, Native::Localize(L"PlaybackManualTitle"),
                            Native::Localize(L"PlaybackManualBody") + PlaybackSourceText(sources));
                }
            });
        });
        if (result.errorCode) error = result.errorMessage.empty() ? Native::Localize(L"MessageUnableTitle") : hstring(result.errorMessage);
    }
    catch (...)
    {
        error = to_message();
        Native::Log(L"detection", error);
    }
    try
    {
        co_await ui;
    }
    catch (hresult_error const &)
    {
        // The window's dispatcher/apartment can already be gone after Closed.
        co_return;
    }
    if (m_closed)
        co_return;
    if (m_detectionTimer) { m_detectionTimer.Stop(); m_detectionTimer = nullptr; }
    if (m_detectionTimedOut) error = Native::Localize(L"DetectionDeadlineBody");
    RefreshOutput();
    auto current = Native::DefaultOutputInfo();
    if (result.observation && (result.observation->outputId != current.id ||
                              result.observation->address != current.address ||
                              current.transport != Native::OutputTransport::Classic))
    {
        result.observation.reset();
        result.reason = L"output-changed";
        Native::Log(L"ui-result-rejected", L"Observation does not belong to the current default Bluetooth output.");
    }
    if (!error.empty())
    {
        RestorePresentation();
        StatusText().Text(Native::Localize(L"StatusError"));
        StatusDot().Fill(Theme(L"SystemFillColorCriticalBrush"));
        ShowMessage(InfoBarSeverity::Error, Native::Localize(L"MessageUnableTitle"), error);
    }
    else if (result.observation)
        ShowObservation(*result.observation);
    else if (result.elevationCanceled || result.canceled || cancel->IsCanceled())
    {
        SetReady();
        RestorePresentation();
        ShowMessage(InfoBarSeverity::Informational,
                    Native::Localize(result.elevationCanceled ? L"MessageElevationCanceledTitle"
                                                              : L"MessageDetectionCanceledTitle"),
                    Native::Localize(result.elevationCanceled ? L"MessageElevationCanceledBody"
                                                              : L"MessageDetectionCanceledBody"));
    }
    else
    {
        CodecText().Text(Native::Localize(L"CodecNoEvent"));
        ProtocolText().Text(Native::Localize(L"ProtocolRetryHint"));
        StatusText().Text(Native::Localize(L"StatusTryAgain"));
        StatusDot().Fill(Theme(L"SystemFillColorCautionBrush"));
        StandardIdText().Text(L"—");
        VendorIdText().Text(L"—");
        VendorCodecIdText().Text(L"—");
        ObservedAtText().Text(L"—");
        auto key = result.reason == L"tone-failed" ? L"ReasonToneFailed" :
                   result.reason == L"schema-unsupported" || result.reason == L"metadata-unavailable" ? L"ReasonMetadata" :
                   result.reason == L"hands-free" || result.reason == L"microphone-active" ? L"ReasonHandsFree" :
                   result.reason == L"le-audio" ? L"MessageLeAudioBody" :
                   result.reason == L"not-bluetooth" ? L"MessageNotBluetoothBody" :
                   result.reason == L"target-unresolved" ? L"ReasonTargetUnresolved" :
                   result.reason == L"output-changed" ? L"ReasonOutputChanged" :
                   result.reason == L"waiting-for-stream-end" ? L"ReasonStreamOpen" :
                   result.reason == L"no-provider-events" ? L"ReasonNoProvider" :
                   result.reason == L"no-matching-event" ? L"ReasonNoMatching" : L"MessageNoEventDefaultBody";
        std::wstring detail(Native::Localize(key));
        if (!result.warnings.empty())
            detail += L"\n" + std::wstring(Native::Localize(L"ReasonExportLog"));
        auto title = result.reason == L"not-bluetooth" ? L"MessageNotBluetoothTitle" :
                     result.reason == L"le-audio" ? L"MessageLeAudioTitle" : L"MessageNoEventTitle";
        if (result.reason == L"not-bluetooth" || result.reason == L"le-audio")
        {
            CodecText().Text(L"—");
            ProtocolText().Text(Native::Localize(L"ProtocolDefault"));
            StatusText().Text(Native::Localize(result.reason == L"le-audio" ? L"StatusLeAudio" : L"StatusNotBluetooth"));
        }
        ShowMessage(InfoBarSeverity::Warning, Native::Localize(title), hstring(detail));
    }
    if (result.playbackRestoreFailed || result.playbackCleanupPending)
    {
        auto detail = Native::Localize(result.playbackCleanupPending ? L"PlaybackCleanupPending" : L"PlaybackRestoreFailed") + PlaybackSourceText(result.playbackManualSources);
        if (!error.empty()) MessageBar().Message(MessageBar().Message() + L"\n" + detail);
        else ShowMessage(InfoBarSeverity::Warning, Native::Localize(L"PlaybackRestoreWarning"), detail);
    }
    else if (result.playbackManualRequired && !result.observation && error.empty() && !result.canceled && !result.elevationCanceled)
        MessageBar().Message(MessageBar().Message() + L"\n" + Native::Localize(L"PlaybackManualBody") + PlaybackSourceText(result.playbackManualSources));
    m_detection.reset();
    DetectButton().IsEnabled(true);
    m_previous.clear();
    DetectionProgress().IsActive(false);
    DetectionProgress().Visibility(Visibility::Collapsed);
    DetectIcon().Visibility(Visibility::Visible);
    DetectButtonText().Text(Native::Localize(L"DetectButtonAgain"));
    // Keep the dispatcher alive on a normal close so late pause completions
    // can restore their own session. Force termination cannot be recovered.
    if (Native::PlaybackControl::CleanupPending())
    {
        DetectButton().IsEnabled(false);
        while (Native::PlaybackControl::CleanupPending()) co_await resume_after(std::chrono::milliseconds(100));
        try { co_await ui; }
        catch (hresult_error const &) { co_return; }
        if (!m_closed) DetectButton().IsEnabled(true);
    }
    if (m_closeRequested && !m_closed) Close();
}
fire_and_forget MainWindow::OnReportClicked(Windows::Foundation::IInspectable const &, RoutedEventArgs const &)
{
    if (m_reportBusy) co_return;
    if (m_detection) { DiagnosticsStatus().Text(Native::Localize(L"ReportWaitForDetection")); co_return; }
    auto lifetime = get_strong();
    apartment_context ui;
    m_reportBusy = true;
    ReportProblemButton().IsEnabled(false);
    struct BusyScope
    {
        MainWindow *self;
        ~BusyScope() { self->m_reportBusy = false; if (!self->m_closed) self->ReportProblemButton().IsEnabled(true); }
    } busy{this};
    std::vector<Native::DiagnosticAttemptInfo> attempts;
    hstring error;
    co_await resume_background();
    try
    {
        init_apartment(apartment_type::multi_threaded);
        struct Apartment { ~Apartment() { uninit_apartment(); } } apartment;
        attempts = Native::RecentDiagnosticAttempts();
    }
    catch (...) { error = to_message(); }
    try { co_await ui; } catch (...) { co_return; }
    if (m_closed) co_return;
    if (!error.empty()) { DiagnosticsStatus().Text(error); error = {}; }
    try
    {
        ComboBox choice;
        choice.HorizontalAlignment(HorizontalAlignment::Stretch);
        int32_t preferred{};
        bool found{};
        for (size_t i = 0; i < attempts.size(); ++i)
        {
            auto const &attempt = attempts[i];
            choice.Items().Append(box_value(attempt.started + L" · " + attempt.reason));
            if (!found && attempt.outcome != L"success" && attempt.outcome != L"user_canceled" && attempt.outcome != L"elevation_canceled")
            { preferred = static_cast<int32_t>(i); found = true; }
        }
        auto applicationIndex = static_cast<int32_t>(attempts.size());
        choice.Items().Append(box_value(Native::Localize(L"ReportApplicationContext")));
        choice.SelectedIndex(attempts.empty() ? applicationIndex : preferred);
        StackPanel options; options.Spacing(12); options.MinWidth(280); options.MaxWidth(540);
        options.Children().Append(choice);
        CheckBox includeLogs;
        TextBlock logLabel; logLabel.Text(Native::Localize(L"ReportIncludeRuntimeLog")); logLabel.TextWrapping(TextWrapping::Wrap);
        includeLogs.Content(logLabel); includeLogs.IsChecked(attempts.empty());
        options.Children().Append(includeLogs);
        TextBlock logNote; logNote.Text(Native::Localize(L"ReportRuntimeLogNotice")); logNote.TextWrapping(TextWrapping::Wrap);
        options.Children().Append(logNote);
        auto contextSelection = choice.SelectionChanged(auto_revoke, [includeLogs, applicationIndex](auto const &sender, auto const &) {
            if (sender.template as<ComboBox>().SelectedIndex() == applicationIndex) includeLogs.IsChecked(true);
        });
        ContentDialog select;
        select.XamlRoot(Content().as<FrameworkElement>().XamlRoot());
        select.Title(box_value(Native::Localize(L"ReportChooseTitle")));
        select.Content(options); select.PrimaryButtonText(Native::Localize(L"ReportPreviewButton"));
        select.CloseButtonText(Native::Localize(L"ReportCancelButton"));
        if (co_await select.ShowAsync() != ContentDialogResult::Primary || m_closed) co_return;
        auto index = choice.SelectedIndex();
        auto id = index == applicationIndex ? std::wstring{} : attempts.at(static_cast<size_t>(index)).id;
        bool withLogs = includeLogs.IsChecked().Value();
        std::shared_ptr<Native::DiagnosticReport> prepared;
        co_await resume_background();
        try
        {
            init_apartment(apartment_type::multi_threaded);
            struct Apartment { ~Apartment() { uninit_apartment(); } } apartment;
            prepared = std::make_shared<Native::DiagnosticReport>(Native::PrepareDiagnosticReport(id, withLogs));
        }
        catch (...) { error = to_message(); }
        try { co_await ui; } catch (...) { co_return; }
        if (m_closed) co_return;
        if (!error.empty()) { DiagnosticsStatus().Text(error); co_return; }
        StackPanel panel; panel.Spacing(10); panel.MinWidth(280); panel.MaxWidth(580);
        TextBlock note; note.Text(Native::Localize(L"ReportPreviewNotice")); note.TextWrapping(TextWrapping::Wrap);
        panel.Children().Append(note);
        ComboBox files; files.HorizontalAlignment(HorizontalAlignment::Stretch);
        int32_t summaryIndex{};
        for (size_t i = 0; i < prepared->files.size(); ++i)
        {
            files.Items().Append(box_value(to_hstring(prepared->files[i].first)));
            if (prepared->files[i].first == "summary.txt") summaryIndex = static_cast<int32_t>(i);
        }
        TextBox preview; preview.IsReadOnly(true); preview.AcceptsReturn(true); preview.TextWrapping(TextWrapping::Wrap);
        preview.Height(260); preview.FontSize(12); preview.Text(prepared->summary);
        ScrollViewer::SetVerticalScrollBarVisibility(preview, ScrollBarVisibility::Auto);
        files.SelectedIndex(summaryIndex);
        auto fileSelection = files.SelectionChanged(auto_revoke, [prepared, preview](auto const &sender, auto const &) {
            auto index = sender.template as<ComboBox>().SelectedIndex();
            if (index >= 0 && static_cast<size_t>(index) < prepared->files.size())
                preview.Text(to_hstring(prepared->files[static_cast<size_t>(index)].second));
        });
        panel.Children().Append(files); panel.Children().Append(preview);
        ContentDialog dialog;
        dialog.XamlRoot(Content().as<FrameworkElement>().XamlRoot());
        dialog.Title(box_value(Native::Localize(L"ReportPreviewTitle"))); dialog.Content(panel);
        dialog.PrimaryButtonText(Native::Localize(L"ReportSaveButton")); dialog.CloseButtonText(Native::Localize(L"ReportCancelButton"));
        if (co_await dialog.ShowAsync() != ContentDialogResult::Primary || m_closed) co_return;
        HWND owner{}; this->try_as<IWindowNative>()->get_WindowHandle(&owner);
        auto path = Native::ChooseDiagnosticReportPath(owner, prepared->filename);
        if (path.empty()) co_return;
        co_await resume_background();
        try { Native::SaveDiagnosticReport(*prepared, path); }
        catch (...) { error = to_message(); }
        try { co_await ui; } catch (...) { co_return; }
        if (m_closed) co_return;
        if (!error.empty()) { DiagnosticsStatus().Text(error); co_return; }
        m_report = std::move(prepared); m_reportPath = path;
        ReportActions().Visibility(Visibility::Visible);
        DiagnosticsStatus().Text(Native::Format(std::wstring(Native::Localize(L"ReportSaved")), path));
    }
    catch (...) { if (!m_closed) DiagnosticsStatus().Text(to_message()); }
}
void MainWindow::OnCopySummaryClicked(Windows::Foundation::IInspectable const &, RoutedEventArgs const &)
{
    if (!m_report) return;
    try
    {
        Windows::ApplicationModel::DataTransfer::DataPackage content;
        content.SetText(m_report->summary);
        Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(content);
        DiagnosticsStatus().Text(Native::Localize(L"ReportSummaryCopied"));
    }
    catch (...) { DiagnosticsStatus().Text(to_message()); }
}
void MainWindow::OnReportFolderClicked(Windows::Foundation::IInspectable const &, RoutedEventArgs const &)
{
    if (m_reportPath.empty()) return;
    try { Native::ShowDiagnosticReportInFolder(m_reportPath); }
    catch (...) { DiagnosticsStatus().Text(to_message()); }
}
fire_and_forget MainWindow::OnGitHubClicked(Windows::Foundation::IInspectable const &, RoutedEventArgs const &)
{
    if (!m_report || m_reportBusy) co_return;
    auto lifetime = get_strong();
    auto report = m_report;
    m_reportBusy = true;
    try
    {
        StackPanel panel; panel.Spacing(12); panel.MaxWidth(520);
        TextBlock notice; notice.Text(Native::Localize(L"ReportPublicNotice")); notice.TextWrapping(TextWrapping::Wrap);
        panel.Children().Append(notice);
        CheckBox acknowledge; acknowledge.Content(box_value(Native::Localize(L"ReportPublicAcknowledge")));
        panel.Children().Append(acknowledge);
        ContentDialog dialog;
        dialog.XamlRoot(Content().as<FrameworkElement>().XamlRoot()); dialog.Content(panel);
        dialog.Title(box_value(Native::Localize(L"ReportGitHubTitle")));
        dialog.PrimaryButtonText(Native::Localize(L"ReportOpenGitHubButton")); dialog.CloseButtonText(Native::Localize(L"ReportCancelButton"));
        dialog.IsPrimaryButtonEnabled(false);
        auto checked = acknowledge.Checked(auto_revoke, [dialog](auto const &, auto const &) { dialog.IsPrimaryButtonEnabled(true); });
        auto unchecked = acknowledge.Unchecked(auto_revoke, [dialog](auto const &, auto const &) { dialog.IsPrimaryButtonEnabled(false); });
        auto response = co_await dialog.ShowAsync();
        if (!m_closed && response == ContentDialogResult::Primary)
        {
            bool launched = co_await Windows::System::Launcher::LaunchUriAsync(Windows::Foundation::Uri(Native::DiagnosticGitHubUrl(*report)));
            if (!m_closed) DiagnosticsStatus().Text(Native::Localize(launched ? L"ReportBrowserOpened" : L"ReportBrowserFailed"));
        }
    }
    catch (...) { if (!m_closed) DiagnosticsStatus().Text(to_message()); }
    m_reportBusy = false;
}
} // namespace winrt::BluetoothAudioCodec::WinUI::implementation
