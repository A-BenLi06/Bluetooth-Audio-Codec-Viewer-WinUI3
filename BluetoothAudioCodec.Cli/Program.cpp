#include "pch.h"
#include "NativeServices.h"
#include "DiagnosticsReport.h"
#include "PlaybackControl.h"
#include <iostream>

namespace {
Native::Cancellation* activeCancellation{};
BOOL WINAPI OnControl(DWORD type)
{
    if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT) return FALSE;
    if (activeCancellation) activeCancellation->Cancel();
    return TRUE;
}
}
// The only localized Core strings are presentation labels supplied by the host.
winrt::hstring Native::Localize(std::wstring_view key)
{
    if (key == L"PlaybackUnknownSource") return L"Unknown application";
    if (key == L"ReportZipFileType") return L"ZIP archive";
    return winrt::hstring(key);
}

int wmain(int argc, wchar_t** argv)
{
    try
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        std::vector<std::wstring> args(argv + 1, argv + argc);
        if (!args.empty() && args[0] == L"--elevated-helper") return Native::RunHelper(args);
        if (args.size() == 1 && args[0] == L"--help")
        {
            std::wcout << L"BluetoothAudioCodec.Cli [--timeout 1..60] [--json] [--no-tone] [--watch]\n"
                          L"  --export-report PATH [--include-runtime-log]\n"
                          L"  --export-diagnostics PATH\n  --self-test\n"
                          L"Detection requests UAC for a short-lived helper. Ctrl+C cancels.\n";
            return 0;
        }
        if (args.size() == 1 && args[0] == L"--self-test")
        {
            Native::UseDiagnosticTestDirectory();
            struct Cleanup { ~Cleanup() { Native::CleanupDiagnosticTestDirectory(); } } cleanup;
            return Native::RunSelfTests();
        }
        Native::LogEnvironment(L"cli");
        if (!args.empty() && args[0] == L"--export-diagnostics")
        {
            if (args.size() != 2) return 2;
            Native::ExportDiagnostics(args[1]); return 0;
        }
        if (!args.empty() && args[0] == L"--export-report")
        {
            if (args.size() != 2 && (args.size() != 3 || args[2] != L"--include-runtime-log")) return 2;
            auto attempts = Native::RecentDiagnosticAttempts();
            auto report = Native::PrepareDiagnosticReport(attempts.empty() ? L"" : attempts.front().id, args.size() == 3);
            Native::SaveDiagnosticReport(report, args[1]); return 0;
        }
        unsigned seconds = 15;
        bool json{}, tone = true, watch{};
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (args[i] == L"--json") json = true;
            else if (args[i] == L"--no-tone") tone = false;
            else if (args[i] == L"--watch") watch = true;
            else if (args[i] == L"--timeout" && ++i < args.size())
            {
                auto const& value = args[i];
                if (value.empty() || value.size() > 2 || value.find_first_not_of(L"0123456789") != std::wstring::npos) return 2;
                seconds = static_cast<unsigned>(std::stoul(value));
                if (seconds < 1 || seconds > 60) return 2;
            }
            else { std::wcerr << L"Invalid arguments. Use --help.\n"; return 2; }
        }
        Native::Cancellation cancellation;
        activeCancellation = &cancellation;
        SetConsoleCtrlHandler(OnControl, TRUE);
        struct HandlerCleanup { ~HandlerCleanup() { SetConsoleCtrlHandler(OnControl, FALSE); activeCancellation = nullptr; } } handlerCleanup;
        int exitCode{};
        do
        {
            auto result = Native::DetectWithElevation(seconds, tone, cancellation, nullptr);
            using namespace winrt::Windows::Data::Json;
            JsonObject output;
            output.SetNamedValue(L"reason", JsonValue::CreateStringValue(result.reason));
            output.SetNamedValue(L"canceled", JsonValue::CreateBooleanValue(result.canceled || result.elevationCanceled));
            output.SetNamedValue(L"playbackRestoreFailed", JsonValue::CreateBooleanValue(result.playbackRestoreFailed));
            output.SetNamedValue(L"playbackCleanupPending", JsonValue::CreateBooleanValue(result.playbackCleanupPending));
            output.SetNamedValue(L"errorCode", result.errorCode ? JsonValue::CreateNumberValue(*result.errorCode) : JsonValue::CreateNullValue());
            if (result.observation)
            {
                auto const& o = *result.observation;
                output.SetNamedValue(L"codec", JsonValue::CreateStringValue(o.name));
                output.SetNamedValue(L"output", JsonValue::CreateStringValue(o.output));
                output.SetNamedValue(L"standardCodecId", JsonValue::CreateNumberValue(o.standard));
                output.SetNamedValue(L"vendorId", JsonValue::CreateStringValue(std::to_wstring(o.vendor)));
                output.SetNamedValue(L"vendorCodecId", JsonValue::CreateStringValue(std::to_wstring(o.codec)));
                output.SetNamedValue(L"timestampFileTime", JsonValue::CreateStringValue(std::to_wstring(o.timestamp)));
            }
            else output.SetNamedValue(L"codec", JsonValue::CreateNullValue());
            exitCode = result.errorCode ? 10 : (result.canceled || result.elevationCanceled) ? 4 : result.observation ? 0 : 3;
            if (json) std::cout << winrt::to_string(output.Stringify()) << std::endl;
            else if (result.observation) std::wcout << result.observation->name << L" | " << result.observation->output << std::endl;
            else std::wcout << L"No codec: " << result.reason << L" (exit " << exitCode << L")\n";
            if (result.playbackRestoreFailed || result.playbackCleanupPending)
                std::wcerr << L"Playback recovery requires attention; check your media applications.\n";
            while (Native::PlaybackControl::CleanupPending()) Sleep(100);
            if (!watch || result.elevationCanceled || result.errorCode) break;
            WaitForSingleObject(cancellation.event.get(), 1000);
        } while (!cancellation.IsCanceled());
        return cancellation.IsCanceled() ? 4 : exitCode;
    }
    catch (...) { std::cerr << winrt::to_string(winrt::to_message()) << '\n'; return 10; }
}
