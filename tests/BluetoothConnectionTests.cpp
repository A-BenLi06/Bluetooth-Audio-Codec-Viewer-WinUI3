#include "../BluetoothAudioCodec.WinUI/Services/BluetoothConnectionQuery.h"
#include "../BluetoothAudioCodec.WinUI/Services/CodecCapturePolicy.h"
#include "../BluetoothAudioCodec.WinUI/Services/BluetoothContainerIdentity.h"
#include <iostream>
#include <stdexcept>
using namespace Native;
void Require(bool ok, char const *message)
{
    if (!ok) throw std::runtime_error(message);
}
BLUETOOTH_DEVICE_INFO Device(uint64_t address, wchar_t const *name, bool connected = true, unsigned major = 4)
{
    BLUETOOTH_DEVICE_INFO d{sizeof(d)};
    d.Address.ullLong = address;
    wcscpy_s(d.szName, name);
    d.fConnected = connected;
    d.ulClassofDevice = major << 8;
    return d;
}
struct FakeApi
{
    std::vector<std::vector<BLUETOOTH_DEVICE_INFO>> radios;
    size_t radioIndex{}, deviceIndex{};
    DWORD error{ERROR_NO_MORE_ITEMS}, firstRadioError{}, firstDeviceError{}, nextRadioError{}, nextDeviceError{};
    int openRadios{}, openDeviceFinds{}, openRadioFinds{};
    int FirstRadio(BLUETOOTH_FIND_RADIO_PARAMS *p, HANDLE *h)
    {
        Require(p->dwSize == sizeof(*p), "radio parameter size");
        radioIndex = 0;
        if (firstRadioError || radios.empty()) { error = firstRadioError ? firstRadioError : ERROR_NO_MORE_ITEMS; return 0; }
        ++openRadioFinds; ++openRadios; *h = reinterpret_cast<HANDLE>(1); return 1;
    }
    bool NextRadio(int, HANDLE *h)
    {
        Require(openRadios == 0 && openDeviceFinds == 0, "previous radio must close before advancing");
        if (nextRadioError || ++radioIndex >= radios.size())
        { error = nextRadioError ? nextRadioError : ERROR_NO_MORE_ITEMS; return false; }
        ++openRadios; *h = reinterpret_cast<HANDLE>(radioIndex + 1); return true;
    }
    void CloseRadioFind(int) { --openRadioFinds; }
    void CloseRadio(HANDLE) { --openRadios; }
    int FirstDevice(BLUETOOTH_DEVICE_SEARCH_PARAMS *p, BLUETOOTH_DEVICE_INFO *d)
    {
        Require(p->dwSize == sizeof(*p) && d->dwSize == sizeof(*d), "device parameter sizes");
        Require(!p->fIssueInquiry && p->fReturnConnected && !p->fReturnRemembered && !p->fReturnAuthenticated,
                "query must never scan or request remembered-only records");
        Require(p->hRadio == reinterpret_cast<HANDLE>(radioIndex + 1), "query must target an active radio");
        deviceIndex = 0;
        if (firstDeviceError || radios[radioIndex].empty())
        { error = firstDeviceError ? firstDeviceError : ERROR_NO_MORE_ITEMS; return 0; }
        ++openDeviceFinds; *d = radios[radioIndex][0]; return 1;
    }
    bool NextDevice(int, BLUETOOTH_DEVICE_INFO *d)
    {
        if (nextDeviceError || ++deviceIndex >= radios[radioIndex].size())
        { error = nextDeviceError ? nextDeviceError : ERROR_NO_MORE_ITEMS; return false; }
        *d = radios[radioIndex][deviceIndex]; return true;
    }
    void CloseDeviceFind(int) { --openDeviceFinds; }
    DWORD Error() { return error; }
    BluetoothStatus Read()
    {
        auto status = Detail::QueryBluetoothConnections(*this);
        Require(!openRadios && !openDeviceFinds && !openRadioFinds, "snapshot leaked a handle");
        return status;
    }
};
int main()
{
    try
    {
        // Sanitized HD815 fixture: the A2DP service's parent is the local
        // adapter, not the remote DEV_ node. Their container is the identity.
        GUID headset{0x12345678, 0x1234, 0x5678, {1, 2, 3, 4, 5, 6, 7, 8}};
        GUID otherHeadset{0x87654321, 0x1234, 0x5678, {1, 2, 3, 4, 5, 6, 7, 8}};
        std::vector<BluetoothContainerIdentity> containers{{headset, 0x112233aabbccULL},
                                                          {otherHeadset, 0x998877665544ULL}};
        Require(BluetoothAddressForContainer(headset, containers) == 0x112233aabbccULL,
                "a sibling A2DP service must resolve the remote device in its exact container");
        Require(!BluetoothAddressForContainer(GUID{}, containers), "missing container is not identity");
        containers.push_back({headset, 0x112233aabbccULL});
        Require(BluetoothAddressForContainer(headset, containers) == 0x112233aabbccULL,
                "duplicate evidence for the same device remains unambiguous");
        containers.push_back({headset, 0x998877665544ULL});
        Require(!BluetoothAddressForContainer(headset, containers), "conflicting container identities must fail closed");
        GUID machine{0, 0, 0, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
        containers = {{machine, 123}, {GUID{}, 456}};
        Require(!BluetoothAddressForContainer(machine, containers) && !BluetoothAddressForContainer(GUID{}, containers),
                "shared local and null containers cannot identify a headset");
        Require(BluetoothAddressFromInstance(L"BTHENUM\\Dev_112233AABBCC\\8&1") == 0x112233aabbccULL,
                "address must come from an exact remote devnode identity");
        for (auto id : {L"USB\\DEV_112233AABBCC", L"BTHENUM\\DEV_112233AABBCC0", L"BTHENUM\\DEV_112233AABBCZ", L"My 112233AABBCC headset"})
            Require(!BluetoothAddressFromInstance(id), "do not guess Bluetooth identities from arbitrary names or substrings");
        Require(ClassifyCodecEvent(L"a2dpstreaming/Stop", L"") == CodecEventKind::StreamSummary &&
                ClassifyCodecEvent(L"", L"A2dpStreamingStart") == CodecEventKind::StreamStart &&
                ClassifyCodecEvent(L"A2dpStreamingOther", L"") == CodecEventKind::Other &&
                ClassifyCodecEvent(L"A2dpAvailableRemoteCodec", L"") == CodecEventKind::Other,
                "event filtering must accept opcode suffixes, reject prefixes and never consume capability lists");
        CodecCapturePolicy capture{0x112233aabbccULL, 100};
        CodecEvidence event{CodecEventKind::StreamSummary, 101, capture.target, 255, 301, 170};
        auto wrong = event; wrong.address++;
        Require(!capture.Accept(wrong), "another headset cannot supply the displayed codec");
        wrong = event; wrong.sink = true;
        Require(!capture.Accept(wrong), "a PC Bluetooth receiver is not the headset playback source");
        wrong = event; wrong.timestamp = 99;
        Require(!capture.Accept(wrong), "reject events from before this capture");
        wrong = event; wrong.vendor = uint64_t(UINT32_MAX) + 1;
        Require(!capture.Accept(wrong), "out-of-range codec identifiers cannot be accepted");
        Require(capture.Accept(event).has_value(), "a matching summary remains valid evidence");
        event.kind = CodecEventKind::Configuration;
        Require(!capture.Accept(event), "configuration alone does not prove a running stream");
        CodecEvidence start{CodecEventKind::StreamStart, 102, capture.target};
        auto confirmed = capture.Accept(start);
        Require(confirmed && confirmed->codec == 170 && confirmed->timestamp == 102,
                "new source stream confirms the successful configuration for the same device");
        event.result = 1;
        capture.Accept(event);
        Require(!capture.Accept(start), "failed negotiation invalidates a cached candidate");
        event.result = 0; event.timestamp = 103;
        capture.Accept(event);
        Require(!capture.Accept(start), "a future configuration cannot confirm an earlier start");
        auto stale = event; stale.timestamp = 101; stale.codec = 1;
        capture.Accept(stale);
        start.timestamp = 104;
        Require(capture.Accept(start)->codec == 170, "out-of-order configuration cannot replace a newer candidate");
        event.timestamp = 105; event.result = 1; event.vendor = UINT64_MAX;
        capture.Accept(event); start.timestamp = 106;
        Require(!capture.Accept(start), "a failed negotiation invalidates even when failed codec fields are unusable");
        FakeApi api;
        auto off = api.Read();
        Require(off.state == ConnectionState::Disconnected && off.names.empty(), "radio off must clear stale paired names");
        api.radios = {{Device(1, L"WH-CH710N")}};
        auto on = api.Read();
        Require(on.state == ConnectionState::Connected && on.names == std::vector<std::wstring>{L"WH-CH710N"}, "connected headset");
        api.radios[0][0].fConnected = FALSE;
        auto disconnected = api.Read();
        Require(disconnected.state == ConnectionState::Disconnected && disconnected.addresses.empty() && disconnected.names.empty(), "disconnect replaces previous snapshot");
        api.radios = {{}, {Device(2, L"Other"), Device(3, L"Mouse", true, 5), Device(4, L"Old", false)}};
        Require(api.Read().addresses == std::vector<uint64_t>{2}, "all radios; ignore non-audio and disconnected devices");
        api.radios = {{Device(1, L"Same")}, {Device(1, L"Same"), Device(2, L"Same")}};
        auto multi = api.Read();
        Require(multi.names.size() == 1 && multi.addresses == std::vector<uint64_t>{1, 2}, "deduplicate radios without merging different headsets");
        api.radios = {{Device(1, L"")}};
        Require(api.Read().state == ConnectionState::Connected, "nameless connected device still counts");
        for (int fault = 0; fault != 4; ++fault)
        {
            FakeApi broken;
            broken.radios = {{Device(1, L"Old")}};
            DWORD code = ERROR_ACCESS_DENIED;
            if (fault == 0) broken.firstRadioError = code;
            if (fault == 1) broken.firstDeviceError = code;
            if (fault == 2) broken.nextDeviceError = code;
            if (fault == 3) broken.nextRadioError = code;
            auto failed = broken.Read();
            Require(failed.state == ConnectionState::Unavailable && failed.names.empty() && failed.addresses.empty() && failed.error == code,
                    "query failure must discard even a partial connected snapshot");
        }
        BluetoothStatusTracker tracker;
        on.revision = 1;
        Require(tracker.Accept(on, L"speakers-id"), "initial status");
        auto generation = tracker.generation;
        on.revision = 2;
        Require(tracker.Accept(on, L"speakers-id") && tracker.generation == generation,
                "connected but not default must remain connected; repeated refresh must preserve valid detection");
        disconnected.revision = 3;
        tracker.Accept(disconnected, L"speakers-id");
        Require(tracker.generation != generation, "missed disconnect event invalidates detection on next snapshot");
        Require(!tracker.Accept(on, L"old-id") && tracker.state == ConnectionState::Disconnected,
                "late queued connected status must not resurrect a name");
        generation = tracker.generation;
        on.revision = 4;
        tracker.Accept(on, L"speakers-id");
        Require(tracker.generation != generation, "reconnect requires a new observation");
        generation = tracker.generation;
        on.revision = 5; on.addresses = {99};
        tracker.Accept(on, L"speakers-id");
        Require(tracker.generation != generation, "same friendly name with different address invalidates old codec");
        generation = tracker.generation;
        on.revision = 6;
        tracker.Accept(on, L"another-endpoint-with-same-name");
        Require(tracker.generation != generation, "endpoint identity rather than display name invalidates old results");
        std::cout << "Bluetooth regressions passed: off/disconnect, multiple radios, partial failures, handle cleanup, stale notifications and detection invalidation.\n";
        return 0;
    }
    catch (std::exception const &e) { std::cerr << e.what() << '\n'; return 1; }
}
