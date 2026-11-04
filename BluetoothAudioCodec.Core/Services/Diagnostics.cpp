#include "pch.h"
#include "NativeServices.h"
#include "DiagnosticsReport.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <bcrypt.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <filesystem>
#include <fstream>
#include <format>
#include <regex>
#include <set>
#include <array>
using namespace winrt;
using namespace Windows::Data::Json;
namespace Native
{
namespace
{
constexpr uintmax_t LogLimit = 1024 * 1024;
bool RotateIfNeeded(std::filesystem::path const &file)
{
    if (!std::filesystem::exists(file) || std::filesystem::file_size(file) < LogLimit)
        return true;
    auto previous = file;
    previous += L".previous";
    return MoveFileExW(file.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
}
std::filesystem::path Directory()
{
    auto directory = DiagnosticDataDirectory() / L"Logs";
    std::filesystem::create_directories(directory);
    return directory;
}
std::wstring Utc()
{
    SYSTEMTIME time{};
    GetSystemTime(&time);
    return std::format(L"{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z", time.wYear, time.wMonth, time.wDay,
                       time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
}
struct LogLock
{
    Handle mutex{CreateMutexW(nullptr, FALSE, L"Local\\BluetoothAudioCodecViewer-Log")};
    bool acquired{};
    explicit LogLock(DWORD milliseconds)
    {
        auto state = WaitForSingleObject(mutex.get(), milliseconds);
        acquired = state == WAIT_OBJECT_0 || state == WAIT_ABANDONED;
    }
    ~LogLock() { if (acquired) ReleaseMutex(mutex.get()); }
};
std::wstring FileVersion(std::filesystem::path const &path)
{
    DWORD ignored{};
    auto size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (!size || size > 1024 * 1024)
        return L"unavailable";
    std::vector<BYTE> bytes(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, bytes.data()))
        return L"unavailable";
    VS_FIXEDFILEINFO *version{};
    UINT length{};
    if (!VerQueryValueW(bytes.data(), L"\\", reinterpret_cast<void **>(&version), &length) || length < sizeof(*version))
        return L"unavailable";
    return std::format(L"{}.{}.{}.{}", HIWORD(version->dwFileVersionMS), LOWORD(version->dwFileVersionMS),
                       HIWORD(version->dwFileVersionLS), LOWORD(version->dwFileVersionLS));
}
std::wstring DeviceText(DEVINST node, DEVPROPKEY const &key)
{
    DEVPROPTYPE type{};
    ULONG size{};
    if (CM_Get_DevNode_PropertyW(node, &key, &type, nullptr, &size, 0) != CR_BUFFER_SMALL ||
        type != DEVPROP_TYPE_STRING || size > 32768)
        return {};
    std::vector<wchar_t> text(size / sizeof(wchar_t) + 1);
    if (CM_Get_DevNode_PropertyW(node, &key, &type, reinterpret_cast<BYTE *>(text.data()), &size, 0) != CR_SUCCESS)
        return {};
    return text.data();
}
std::wstring Environment()
{
    wchar_t executable[32768]{}, system[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    GetSystemDirectoryW(system, MAX_PATH);
    using RtlVersion = LONG(WINAPI *)(OSVERSIONINFOW *);
    OSVERSIONINFOW os{sizeof(os)};
    auto rtl = reinterpret_cast<RtlVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    if (rtl)
        rtl(&os);
    SYSTEM_INFO info{};
    GetNativeSystemInfo(&info);
    std::wstring text = std::format(L"engine=native-cpp capture-protocol=2 app-version={} windows={}.{}.{} native-architecture={} process-bits={} btha2dp.sys={}\n",
                                    FileVersion(executable), os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber,
                                    info.wProcessorArchitecture, sizeof(void *) * 8,
                                    FileVersion(std::filesystem::path(system) / L"drivers" / L"btha2dp.sys"));
    // Installed Bluetooth-class drivers, without instance IDs, serial numbers,
    // hardware IDs, remote friendly names, or a computer/user name.
    ULONG count{};
    constexpr wchar_t bluetoothClass[] = L"{e0cbf06c-cd8b-4647-bb8a-263b43f0f974}";
    if (CM_Get_Device_ID_List_SizeW(&count, bluetoothClass, CM_GETIDLIST_FILTER_CLASS | CM_GETIDLIST_FILTER_PRESENT) == CR_SUCCESS &&
        count && count < 1024 * 1024)
    {
        std::vector<wchar_t> ids(count);
        if (CM_Get_Device_ID_ListW(bluetoothClass, ids.data(), count,
                                  CM_GETIDLIST_FILTER_CLASS | CM_GETIDLIST_FILTER_PRESENT) == CR_SUCCESS)
        {
            std::set<std::wstring> drivers;
            for (auto id = ids.data(); *id; id += wcslen(id) + 1)
            {
                DEVINST node{};
                if (CM_Locate_DevNodeW(&node, id, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
                    continue;
                auto driver = DeviceText(node, DEVPKEY_Device_DriverVersion);
                auto manufacturer = DeviceText(node, DEVPKEY_Device_Manufacturer);
                auto service = DeviceText(node, DEVPKEY_Device_Service);
                if (!driver.empty())
                    drivers.insert(std::format(L"bluetooth-driver service={} manufacturer={} version={}", service, manufacturer, driver));
            }
            for (auto const &driver : drivers)
                text += driver + L"\n";
        }
    }
    return RedactDiagnostics(std::move(text));
}
} // namespace
std::wstring PrivacyTag(std::wstring_view value)
{
    if (value.empty())
        return L"none";
    static auto salt = [] {
        std::array<BYTE, 32> bytes{};
        check_nt(BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
        return bytes;
    }();
    auto input = to_string(value);
    std::vector<BYTE> salted(salt.begin(), salt.end());
    salted.insert(salted.end(), input.begin(), input.end());
    std::array<BYTE, 32> digest{};
    check_nt(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, salted.data(), static_cast<ULONG>(salted.size()),
                        digest.data(), static_cast<ULONG>(digest.size())));
    std::wstring tag = L"id:";
    for (size_t i = 0; i < 6; ++i)
    {
        if (i && i % 2 == 0)
            tag += L'-';
        tag += std::format(L"{:02x}", digest[i]);
    }
    return tag;
}
std::wstring RedactDiagnostics(std::wstring text)
{
    // Applied both before writing new logs and again on export, including
    // legacy logs produced by earlier application versions.
    static std::wregex const user(LR"(([A-Z]:[\\/]+Users[\\/]+)[^\\/\r\n"]+)", std::regex::icase);
    static std::wregex const mac(LR"(\b(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}\b)", std::regex::icase);
    static std::wregex const tokens(LR"(\b[0-9a-f]{32,}\b)", std::regex::icase);
    static std::wregex const address(LR"((^|[^0-9a-f])[0-9a-f]{12}(?=$|[^0-9a-f]))", std::regex::icase);
    static std::wregex const unc(LR"(\\\\[^\s\\/"\r\n]+[\\/][^\s"\r\n]*)", std::regex::icase);
    static std::wregex const email(LR"([A-Z0-9._%+\-]+@[A-Z0-9.\-]+\.[A-Z]{2,})", std::regex::icase);
    static std::wregex const sid(LR"(S-1-\d+(?:-\d+){1,})", std::regex::icase);
    static std::wregex const endpoint(LR"(\{0\.0\.[^\s"\r\n]+)", std::regex::icase);
    text = std::regex_replace(text, user, L"$1[user]");
    text = std::regex_replace(text, mac, L"[bluetooth-address]");
    text = std::regex_replace(text, tokens, L"[token]");
    // New pseudonyms use separated groups, so do not destroy those.
    // Plain 12-hex values in legacy logs may contain device addresses.
    text = std::regex_replace(text, address, L"$1[identifier]");
    text = std::regex_replace(text, unc, L"[network-path]");
    text = std::regex_replace(text, email, L"[email]");
    text = std::regex_replace(text, sid, L"[sid]");
    return std::regex_replace(text, endpoint, L"[endpoint]");
}
void Log(std::wstring_view stage, std::wstring_view message) noexcept
{
    try
    {
        auto directory = Directory();
        LogLock lock(100);
        if (!lock.acquired)
            return;
        auto file = directory / L"runtime.log";
        if (!RotateIfNeeded(file))
            return;
        JsonObject entry;
        entry.SetNamedValue(L"utc", JsonValue::CreateStringValue(Utc()));
        entry.SetNamedValue(L"pid", JsonValue::CreateNumberValue(GetCurrentProcessId()));
        entry.SetNamedValue(L"stage", JsonValue::CreateStringValue(hstring(stage)));
        entry.SetNamedValue(L"message", JsonValue::CreateStringValue(RedactDiagnostics(std::wstring(message.substr(0, 16384)))));
        std::ofstream output(file, std::ios::app | std::ios::binary);
        output << to_string(entry.Stringify()) << '\n';
    }
    catch (...) { }
}
void LogEnvironment(std::wstring_view role) noexcept
{
    try { Log(L"startup", std::wstring(role) + L" " + Environment()); }
    catch (...) { Log(L"startup", L"System version information unavailable."); }
}
std::wstring ChooseDiagnosticsPath(HWND owner)
{
    com_ptr<IFileSaveDialog> dialog;
    check_hresult(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   __uuidof(IFileSaveDialog), dialog.put_void()));
    DWORD options{};
    check_hresult(dialog->GetOptions(&options));
    check_hresult(dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_DONTADDTORECENT));
    COMDLG_FILTERSPEC types[]{{L"Diagnostic log (*.txt)", L"*.txt"}};
    check_hresult(dialog->SetFileTypes(1, types));
    check_hresult(dialog->SetDefaultExtension(L"txt"));
    SYSTEMTIME time{};
    GetLocalTime(&time);
    auto name = std::format(L"BluetoothAudioCodec-diagnostics-{:04}{:02}{:02}-{:02}{:02}{:02}.txt",
                            time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
    check_hresult(dialog->SetFileName(name.c_str()));
    auto result = dialog->Show(owner);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return {};
    check_hresult(result);
    com_ptr<IShellItem> item;
    check_hresult(dialog->GetResult(item.put()));
    PWSTR path{};
    check_hresult(item->GetDisplayName(SIGDN_FILESYSPATH, &path));
    std::wstring value(path);
    CoTaskMemFree(path);
    return value;
}
std::wstring CollectRuntimeDiagnostics()
{
    auto directory = Directory();
    std::wstring contents = L"Bluetooth Audio Codec diagnostic report\nFormat: 1\nExported UTC: " + Utc() +
        L"\nThis report contains application/system/driver versions and bounded technical logs.\n"
        L"Audio is not recorded. User paths, device addresses and tokens are redacted. Nothing is uploaded automatically.\n\n" + Environment();
    auto output = DefaultOutputInfo();
    contents += std::format(L"default-output transport={} identity-resolved={} microphone-active={} identity-source={}\n",
                            static_cast<int>(output.transport), output.address != 0, output.microphoneActive,
                            output.identitySource.empty() ? L"unresolved" : output.identitySource);
    {
        LogLock lock(2000);
        if (!lock.acquired)
            throw hresult_error(HRESULT_FROM_WIN32(ERROR_BUSY), L"Application logs are busy. Try exporting again.");
        for (auto const *name : {L"runtime.log.previous", L"runtime.log"})
        {
            contents += L"\n--- " + std::wstring(name) + L" ---\n";
            auto file = directory / name;
            if (!std::filesystem::exists(file))
                continue;
            std::ifstream input(file, std::ios::binary);
            if (!input)
                throw hresult_error(E_FAIL, L"A diagnostic log could not be read.");
            // A fixed read bound also handles a legacy/unexpected oversized log.
            std::string bytes(LogLimit + 65536, '\0');
            input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            bytes.resize(static_cast<size_t>(input.gcount()));
            // Legacy logs or a bounded read may end in an incomplete UTF-8
            // sequence. Replace invalid bytes instead of losing the report.
            auto length = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
            std::wstring decoded(length, L'\0');
            if (length)
                MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), decoded.data(), length);
            contents += decoded;
        }
    }
    return RedactDiagnostics(std::move(contents));
}
void ExportDiagnostics(std::wstring const &path)
{
    auto directory = Directory();
    auto destination = std::filesystem::absolute(path).lexically_normal();
    if (_wcsicmp(destination.c_str(), (directory / L"runtime.log").c_str()) == 0 ||
        _wcsicmp(destination.c_str(), (directory / L"runtime.log.previous").c_str()) == 0)
        throw hresult_invalid_argument(L"Choose a different file from the active application log.");
    auto safe = to_string(CollectRuntimeDiagnostics());
    // Create the full report next to its destination before replacing it, so
    // a failed export cannot leave a truncated user-selected file behind.
    auto temporary = destination;
    GUID unique{};
    check_hresult(CoCreateGuid(&unique));
    wchar_t suffix[40]{};
    StringFromGUID2(unique, suffix, static_cast<int>(std::size(suffix)));
    temporary += std::wstring(suffix) + L".tmp";
    struct RemoveTemporary { std::filesystem::path path; ~RemoveTemporary() { std::error_code ignored; std::filesystem::remove(path, ignored); } } cleanup{temporary};
    {
        Handle file(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file)
            throw_last_error();
        DWORD written{};
        if (!WriteFile(file.get(), safe.data(), static_cast<DWORD>(safe.size()), &written, nullptr) ||
            written != safe.size() || !FlushFileBuffers(file.get()))
            throw_last_error();
    }
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw_last_error();
    Log(L"diagnostics-exported", L"A redacted report was saved by the user.");
}
int RunDiagnosticsTests()
{
    auto sample = RedactDiagnostics(L"C:\\Users\\Alice\\a.txt C:\\\\Users\\\\Ben\\\\b.txt 30:53:C1:92:C7:8D "
                                    L"3053C192C78D 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef "
                                    L"S-1-5-21-123-456 {0.0.0.00000000}.{endpoint} "
                                    L"BTHENUM\\DEV_112233AABBCC\\x 112233AABBCC_C00000000 "
                                    L"\\\\workstation\\Users\\PrivateUser\\data.txt person@example.test");
    if (sample.find(L"Alice") != std::wstring::npos || sample.find(L"Ben") != std::wstring::npos ||
        sample.find(L"30:53") != std::wstring::npos || sample.find(L"3053C192C78D") != std::wstring::npos ||
        sample.find(L"0123456789abcdef") != std::wstring::npos || sample.find(L"S-1-5") != std::wstring::npos ||
        sample.find(L"{0.0.") != std::wstring::npos || sample.find(L"112233AABBCC") != std::wstring::npos ||
        sample.find(L"workstation") != std::wstring::npos || sample.find(L"PrivateUser") != std::wstring::npos ||
        sample.find(L"person@example") != std::wstring::npos)
        return 31;
    if (PrivacyTag(L"private-endpoint") == L"private-endpoint" ||
        PrivacyTag(L"private-endpoint") != PrivacyTag(L"private-endpoint") ||
        PrivacyTag(L"private-endpoint") == PrivacyTag(L"different-endpoint"))
        return 32;
    if (RedactDiagnostics(PrivacyTag(L"private-endpoint")) != PrivacyTag(L"private-endpoint"))
        return 33;
    GUID unique{};
    check_hresult(CoCreateGuid(&unique));
    wchar_t suffix[40]{};
    StringFromGUID2(unique, suffix, static_cast<int>(std::size(suffix)));
    auto directory = std::filesystem::temp_directory_path() / (std::wstring(L"BluetoothAudioCodec-test-") + suffix);
    if (!std::filesystem::create_directory(directory))
        return 34;
    struct TestFiles
    {
        std::filesystem::path directory;
        ~TestFiles()
        {
            std::error_code ignored;
            for (auto name : {L"runtime.log", L"runtime.log.previous"})
                std::filesystem::remove(directory / name, ignored);
            std::filesystem::remove(directory, ignored);
        }
    } cleanup{directory};
    auto current = directory / L"runtime.log", previous = directory / L"runtime.log.previous";
    std::string bytes(static_cast<size_t>(LogLimit), 'a');
    { std::ofstream file(current, std::ios::binary); file.write(bytes.data(), bytes.size() - 1); }
    if (!RotateIfNeeded(current) || std::filesystem::exists(previous))
        return 35;
    { std::ofstream file(current, std::ios::binary | std::ios::app); file.put('a'); }
    if (!RotateIfNeeded(current) || std::filesystem::exists(current) || std::filesystem::file_size(previous) != LogLimit)
        return 36;
    bytes[0] = 'b';
    { std::ofstream file(current, std::ios::binary); file.write(bytes.data(), bytes.size()); }
    if (!RotateIfNeeded(current) || std::filesystem::exists(current))
        return 37;
    std::ifstream file(previous, std::ios::binary);
    if (file.get() != 'b')
        return 38;
    return 0;
}
} // namespace Native
