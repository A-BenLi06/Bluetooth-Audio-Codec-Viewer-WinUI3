#include "pch.h"
#include "App.xaml.h"
#include "NativeServices.h"
#include "DiagnosticsReport.h"
#include "PlaybackControl.h"
#include <shellapi.h>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        int count{};
        auto values = CommandLineToArgvW(GetCommandLineW(), &count);
        std::vector<std::wstring> args;
        if (values)
        {
            for (int i = 1; i < count; ++i)
                args.emplace_back(values[i]);
            LocalFree(values);
        }
        bool testing = !args.empty() && (args[0] == L"--self-test" || args[0] == L"--report-self-test");
        if (testing) Native::UseDiagnosticTestDirectory();
        struct TestCleanup { ~TestCleanup() { Native::CleanupDiagnosticTestDirectory(); } } testCleanup;
        Native::LogEnvironment(!args.empty() && args[0] == L"--elevated-helper" ? L"helper" :
                               !args.empty() && args[0] == L"--self-test" ? L"self-test" : L"application");
        if (!args.empty() && args[0] == L"--elevated-helper")
            return Native::RunHelper(args);
        if (args.size() == 1 && args[0] == L"--self-test")
            return Native::RunSelfTests();
        if (args.size() == 2 && args[0] == L"--inspect-playback")
            return Native::InspectPlayback(args[1]);
        if (args.size() == 2 && args[0] == L"--exercise-playback-recovery")
            return Native::ExercisePlaybackRecovery(args[1]);
        if (!args.empty() && args[0] == L"--report-self-test")
        {
            if (args.size() != 2) return 2;
            return Native::RunDiagnosticReportTests(args[1]);
        }
        if (!args.empty() && (args[0] == L"--export-standard-report" || args[0] == L"--export-report"))
        {
            if (args.size() != 2 && (args.size() != 3 || args[2] != L"--include-runtime-log")) return 2;
            auto attempts = Native::RecentDiagnosticAttempts();
            if (attempts.empty() && args[0] == L"--export-standard-report") return 3;
            auto report = Native::PrepareDiagnosticReport(attempts.empty() ? L"" : attempts.front().id, args.size() == 3);
            Native::SaveDiagnosticReport(report, args[1]);
            return 0;
        }
        if (!args.empty() && args[0] == L"--export-diagnostics")
        {
            if (args.size() != 2)
                return 2;
            Native::ExportDiagnostics(args[1]);
            return 0;
        }
        winrt::Microsoft::UI::Xaml::Application::Start(
            [](auto &&) { winrt::make<winrt::BluetoothAudioCodec::WinUI::implementation::App>(); });
        return 0;
    }
    catch (...)
    {
        Native::Log(L"entry-point", winrt::to_message());
        return 10;
    }
}
