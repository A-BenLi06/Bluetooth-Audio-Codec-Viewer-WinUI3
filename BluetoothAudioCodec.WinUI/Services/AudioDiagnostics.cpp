#include "pch.h"
#include "NativeServices.h"
#include "BluetoothContainerIdentity.h"
#include "DiagnosticsReport.h"
#include <mmdeviceapi.h>
#include <devicetopology.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <cfgmgr32.h>
#include <devpkey.h>
#include <ksmedia.h>
#include <format>
using namespace winrt;
namespace Native
{
namespace
{
struct CoString
{
    PWSTR value{};
    ~CoString() { CoTaskMemFree(value); }
};
std::wstring PropertyString(IMMDevice *device, PROPERTYKEY const &key)
{
    com_ptr<IPropertyStore> store;
    check_hresult(device->OpenPropertyStore(STGM_READ, store.put()));
    PROPVARIANT property{};
    check_hresult(store->GetValue(key, &property));
    std::wstring text = property.vt == VT_LPWSTR && property.pwszVal ? property.pwszVal : L"";
    PropVariantClear(&property);
    return text;
}
GUID DeviceContainer(DEVINST node)
{
    GUID container{};
    DEVPROPTYPE type{};
    ULONG bytes = sizeof(container);
    if (CM_Get_DevNode_PropertyW(node, &DEVPKEY_Device_ContainerId, &type,
                               reinterpret_cast<PBYTE>(&container), &bytes, 0) != CR_SUCCESS ||
        type != DEVPROP_TYPE_GUID || bytes != sizeof(container))
        return {};
    return container;
}
std::vector<BluetoothContainerIdentity> BluetoothContainers()
{
    constexpr ULONG flags = CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT;
    // Service nodes (A2DP, AVRCP) and the BTHENUM\\DEV_ remote devnode are
    // siblings on current Windows systems. Their exact container links them.
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        ULONG size{};
        if (CM_Get_Device_ID_List_SizeW(&size, L"BTHENUM", flags) != CR_SUCCESS || !size || size > 1024 * 1024)
            return {};
        std::vector<wchar_t> ids(size);
        auto result = CM_Get_Device_ID_ListW(L"BTHENUM", ids.data(), size, flags);
        if (result == CR_BUFFER_SMALL)
            continue; // A device arrived between sizing and enumeration.
        if (result != CR_SUCCESS)
            return {};
        std::vector<BluetoothContainerIdentity> devices;
        for (auto id = ids.data(); *id; id += wcslen(id) + 1)
        {
            auto address = BluetoothAddressFromInstance(id);
            DEVINST node{};
            if (address && CM_Locate_DevNodeW(&node, id, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS)
                devices.push_back({DeviceContainer(node), address});
        }
        return devices;
    }
    return {};
}
void InspectInstance(std::wstring instance, AudioOutput &output,
                     std::span<BluetoothContainerIdentity const> devices)
{
    for (int depth = 0; depth < 16 && !instance.empty(); ++depth)
    {
        auto upper = Upper(instance);
        if (upper.starts_with(L"BTHHFENUM\\"))
            output.transport = OutputTransport::HandsFree;
        else if (upper.starts_with(L"BTHLE") && output.transport != OutputTransport::HandsFree)
            output.transport = OutputTransport::LowEnergy;
        else if (upper.starts_with(L"BTHENUM\\") && output.transport == OutputTransport::Unknown)
            output.transport = OutputTransport::Classic;
        if (auto address = BluetoothAddressFromInstance(instance))
        {
            output.address = address;
            output.identitySource = L"remote-devnode";
            return;
        }
        DEVINST node{}, parent{};
        if (CM_Locate_DevNodeW(&node, instance.data(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
            break;
        if (auto address = BluetoothAddressForContainer(DeviceContainer(node), devices))
        {
            output.address = address;
            output.identitySource = L"pnp-container";
            if (output.transport != OutputTransport::Unknown && output.transport != OutputTransport::Other)
                return;
        }
        if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS)
            break;
        wchar_t id[MAX_DEVICE_ID_LEN]{};
        if (CM_Get_Device_IDW(parent, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS)
            break;
        instance = id;
    }
}
void InspectDevice(IMMDeviceEnumerator *enumerator, IMMDevice *device, AudioOutput &output,
                   std::span<BluetoothContainerIdentity const> devices)
{
    InspectInstance(PropertyString(device, PKEY_Device_InstanceId), output, devices);
    // Offloaded audio topology can lead to an Intel SST adapter rather than
    // the Bluetooth peripheral. Try the endpoint's software devnode as well.
    // This is only a locator: CM must confirm that the node actually exists;
    // neither an endpoint GUID nor a friendly name is Bluetooth identity.
    CoString endpoint;
    if (SUCCEEDED(device->GetId(&endpoint.value)) && endpoint.value)
        InspectInstance(L"SWD\\MMDEVAPI\\" + std::wstring(endpoint.value), output, devices);
    if (output.address && output.transport != OutputTransport::Unknown && output.transport != OutputTransport::Other)
        return;
    com_ptr<IDeviceTopology> topology;
    if (FAILED(device->Activate(__uuidof(IDeviceTopology), CLSCTX_ALL, nullptr, topology.put_void())))
        return;
    UINT count{};
    if (FAILED(topology->GetConnectorCount(&count)))
        return;
    for (UINT index = 0; index < std::min(count, 16U); ++index)
    {
        com_ptr<IConnector> connector;
        CoString connected;
        if (FAILED(topology->GetConnector(index, connector.put())) ||
            FAILED(connector->GetDeviceIdConnectedTo(&connected.value)))
            continue;
        com_ptr<IMMDevice> hardware;
        if (SUCCEEDED(enumerator->GetDevice(connected.value, hardware.put())))
            InspectInstance(PropertyString(hardware.get(), PKEY_Device_InstanceId), output, devices);
    }
    if (output.transport == OutputTransport::Unknown && !output.address)
        output.transport = OutputTransport::Other;
}
std::optional<bool> MicrophoneActive(IMMDeviceEnumerator *enumerator, uint64_t address,
                      std::span<BluetoothContainerIdentity const> devices)
{
    if (!address)
        return {};
    com_ptr<IMMDeviceCollection> microphones;
    if (FAILED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, microphones.put())))
        return {};
    UINT count{};
    if (FAILED(microphones->GetCount(&count)))
        return {};
    bool incomplete{};
    for (UINT i = 0; i < std::min(count, 64U); ++i)
    {
        com_ptr<IMMDevice> microphone;
        if (FAILED(microphones->Item(i, microphone.put())))
        {
            incomplete = true;
            continue;
        }
        AudioOutput identity;
        InspectDevice(enumerator, microphone.get(), identity, devices);
        if (identity.address != address)
            continue;
        com_ptr<IAudioSessionManager2> manager;
        if (FAILED(microphone->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, manager.put_void())))
        {
            incomplete = true;
            continue;
        }
        com_ptr<IAudioSessionEnumerator> sessions;
        if (FAILED(manager->GetSessionEnumerator(sessions.put())))
        {
            incomplete = true;
            continue;
        }
        int sessionCount{};
        if (FAILED(sessions->GetCount(&sessionCount)))
        {
            incomplete = true;
            continue;
        }
        if (sessionCount > 128)
            incomplete = true;
        for (int j = 0; j < std::min(sessionCount, 128); ++j)
        {
            com_ptr<IAudioSessionControl> session;
            AudioSessionState state{};
            if (FAILED(sessions->GetSession(j, session.put())) || FAILED(session->GetState(&state)))
                incomplete = true;
            else if (state == AudioSessionStateActive)
                return true;
        }
    }
    return incomplete || count > 64 ? std::optional<bool>{} : std::optional<bool>{false};
}
} // namespace
AudioOutput DefaultOutputInfo() noexcept
{
    AudioOutput output;
    try
    {
        com_ptr<IMMDeviceEnumerator> enumerator;
        check_hresult(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                       __uuidof(IMMDeviceEnumerator), enumerator.put_void()));
        com_ptr<IMMDevice> device;
        check_hresult(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, device.put()));
        DWORD state{};
        check_hresult(device->GetState(&state));
        if (!(state & DEVICE_STATE_ACTIVE))
            throw_hresult(HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED));
        CoString id;
        check_hresult(device->GetId(&id.value));
        output.id = id.value;
        output.name = PropertyString(device.get(), PKEY_Device_FriendlyName);
        auto devices = BluetoothContainers();
        InspectDevice(enumerator.get(), device.get(), output, devices);
        auto microphone = MicrophoneActive(enumerator.get(), output.address, devices);
        output.microphoneStateKnown = microphone.has_value();
        output.microphoneActive = microphone.value_or(false);
    }
    catch (...)
    {
        // Keep the endpoint identity even when a vendor topology cannot be resolved.
        Log(L"audio-identity", to_message());
    }
    return output;
}
std::wstring DefaultOutput() noexcept { return DefaultOutputInfo().name; }
AudioDiagnosticSnapshot CaptureAudioDiagnosticSnapshot(AudioOutput const &output, std::span<uint64_t const> connectedAddresses) noexcept
{
    AudioDiagnosticSnapshot snapshot;
    try
    {
        auto text = [](DEVINST node, DEVPROPKEY const &key) -> std::wstring {
            DEVPROPTYPE type{};
            ULONG size{};
            if (CM_Get_DevNode_PropertyW(node, &key, &type, nullptr, &size, 0) != CR_BUFFER_SMALL ||
                type != DEVPROP_TYPE_STRING || size > 32768)
                return {};
            std::vector<wchar_t> value(size / sizeof(wchar_t) + 1);
            if (CM_Get_DevNode_PropertyW(node, &key, &type, reinterpret_cast<BYTE *>(value.data()), &size, 0) != CR_SUCCESS)
                return {};
            return value.data();
        };
        auto containerText = [](GUID const &value) -> std::wstring {
            constexpr GUID shared{0, 0, 0, {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
            if (IsEqualGUID(value, GUID_NULL) || IsEqualGUID(value, shared))
                return {};
            wchar_t result[40]{};
            StringFromGUID2(value, result, 40);
            return result;
        };
        auto add = [&](std::wstring const &instance, std::wstring kind) -> std::optional<size_t> {
            if (instance.empty())
                return {};
            for (size_t i = 0; i < snapshot.nodes.size(); ++i)
                if (Upper(snapshot.nodes[i].identity) == Upper(instance))
                    return i;
            if (snapshot.nodes.size() >= 32)
            {
                snapshot.partial = true;
                return {};
            }
            auto upper = Upper(instance);
            if (BluetoothAddressFromInstance(instance))
                kind = L"remote-device";
            else if (upper.starts_with(L"BTHENUM\\{0000110A-") || upper.starts_with(L"BTHENUM\\{0000110B-"))
                kind = L"a2dp-service";
            AudioSnapshotNode item;
            item.identity = instance;
            item.kind = std::move(kind);
            item.api = L"CM_Get_DevNode_PropertyW";
            DEVINST node{};
            auto status = CM_Locate_DevNodeW(&node, const_cast<PWSTR>(instance.c_str()), CM_LOCATE_DEVNODE_NORMAL);
            if (status != CR_SUCCESS)
            {
                item.api = L"CM_Locate_DevNodeW";
                item.error = status;
                snapshot.partial = true;
            }
            else
            {
                item.container = containerText(DeviceContainer(node));
                item.driverClass = text(node, DEVPKEY_Device_Class);
                item.driverProvider = text(node, DEVPKEY_Device_DriverProvider);
                item.driverVersion = text(node, DEVPKEY_Device_DriverVersion);
            }
            snapshot.nodes.push_back(std::move(item));
            return snapshot.nodes.size() - 1;
        };
        auto chain = [&](std::wstring instance, std::wstring kind) {
            std::optional<size_t> previous;
            for (int depth = 0; depth < 6 && !instance.empty(); ++depth)
            {
                auto index = add(instance, depth ? L"audio-adapter" : kind);
                if (!index)
                    break;
                if (previous && *previous != *index)
                    snapshot.edges.push_back({*previous, *index, L"pnp_parent"});
                previous = index;
                DEVINST node{}, parent{};
                if (CM_Locate_DevNodeW(&node, instance.data(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS ||
                    CM_Get_Parent(&parent, node, 0) != CR_SUCCESS)
                    break;
                wchar_t id[MAX_DEVICE_ID_LEN]{};
                if (CM_Get_Device_IDW(parent, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS)
                    break;
                // Stop at the relevant device/adapter instead of walking the whole PC.
                if (Upper(id).starts_with(L"HTREE\\") || Upper(id).starts_with(L"ROOT\\"))
                    break;
                instance = id;
                if (depth == 5)
                    snapshot.partial = true;
            }
        };
        if (output.id.empty())
        {
            snapshot.partial = true;
        }
        else
        {
            chain(L"SWD\\MMDEVAPI\\" + output.id, L"endpoint");
            com_ptr<IMMDeviceEnumerator> enumerator;
            check_hresult(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                           __uuidof(IMMDeviceEnumerator), enumerator.put_void()));
            com_ptr<IMMDevice> device;
            check_hresult(enumerator->GetDevice(output.id.c_str(), device.put()));
            auto instance = PropertyString(device.get(), PKEY_Device_InstanceId);
            if (!instance.empty())
                chain(instance, L"audio-adapter");
            com_ptr<IDeviceTopology> topology;
            if (SUCCEEDED(device->Activate(__uuidof(IDeviceTopology), CLSCTX_ALL, nullptr, topology.put_void())))
            {
                UINT count{};
                if (SUCCEEDED(topology->GetConnectorCount(&count)))
                    for (UINT i = 0; i < std::min(count, 16U); ++i)
                    {
                        com_ptr<IConnector> connector;
                        CoString connected;
                        com_ptr<IMMDevice> hardware;
                        if (SUCCEEDED(topology->GetConnector(i, connector.put())) &&
                            SUCCEEDED(connector->GetDeviceIdConnectedTo(&connected.value)) && connected.value &&
                            SUCCEEDED(enumerator->GetDevice(connected.value, hardware.put())))
                        {
                            auto hardwareId = PropertyString(hardware.get(), PKEY_Device_InstanceId);
                            auto index = add(hardwareId, L"audio-adapter");
                            if (index && !snapshot.nodes.empty() && *index != 0)
                                snapshot.edges.push_back({0, *index, L"audio_topology"});
                            chain(hardwareId, L"audio-adapter");
                        }
                    }
            }
        }
        constexpr ULONG flags = CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT;
        ULONG count{};
        if (CM_Get_Device_ID_List_SizeW(&count, L"BTHENUM", flags) == CR_SUCCESS && count && count <= 1024 * 1024)
        {
            std::vector<wchar_t> ids(count);
            if (CM_Get_Device_ID_ListW(L"BTHENUM", ids.data(), count, flags) == CR_SUCCESS)
                for (auto id = ids.data(); *id; id += wcslen(id) + 1)
                {
                    DEVINST node{};
                    if (CM_Locate_DevNodeW(&node, id, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
                        continue;
                    auto container = containerText(DeviceContainer(node));
                    bool same = !container.empty() && std::any_of(snapshot.nodes.begin(), snapshot.nodes.end(),
                        [&](auto const &item) { return item.container == container; });
                    auto upper = Upper(id);
                    bool relevant = BluetoothAddressFromInstance(id) || upper.starts_with(L"BTHENUM\\{0000110A-") ||
                                    upper.starts_with(L"BTHENUM\\{0000110B-");
                    auto address = BluetoothAddressFromInstance(id);
                    bool connectedRemote = address && std::find(connectedAddresses.begin(), connectedAddresses.end(), address) != connectedAddresses.end();
                    if (relevant && (same || connectedRemote || (output.address && address == output.address)))
                        chain(id, L"a2dp-service");
                }
            else
                snapshot.partial = true;
        }
        for (size_t i = 0; i < snapshot.nodes.size(); ++i)
            for (size_t j = i + 1; j < snapshot.nodes.size(); ++j)
                if (!snapshot.nodes[i].container.empty() && snapshot.nodes[i].container == snapshot.nodes[j].container)
                    snapshot.edges.push_back({i, j, L"same_container"});
        if (snapshot.edges.size() > 64)
        {
            snapshot.edges.resize(64);
            snapshot.partial = true;
        }
    }
    catch (...) { snapshot.partial = true; }
    return snapshot;
}
void PlayDiagnosticTone(AudioOutput const &output, Cancellation const &cancel, Cancellation const &finished)
{
    if (cancel.IsCanceled() || finished.IsCanceled())
        return;
    init_apartment(apartment_type::multi_threaded);
    struct Apartment { ~Apartment() { uninit_apartment(); } } apartment;
    com_ptr<IMMDeviceEnumerator> enumerator;
    check_hresult(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                   __uuidof(IMMDeviceEnumerator), enumerator.put_void()));
    com_ptr<IMMDevice> device;
    check_hresult(enumerator->GetDevice(output.id.c_str(), device.put()));
    com_ptr<IAudioClient> client;
    check_hresult(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client.put_void()));
    WAVEFORMATEX *format{};
    check_hresult(client->GetMixFormat(&format));
    struct FreeFormat { WAVEFORMATEX *value; ~FreeFormat() { CoTaskMemFree(value); } } freeFormat{format};
    bool floating = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    bool pcm = format->wFormatTag == WAVE_FORMAT_PCM;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22)
    {
        auto extended = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(format);
        floating = IsEqualGUID(extended->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        pcm = IsEqualGUID(extended->SubFormat, KSDATAFORMAT_SUBTYPE_PCM);
    }
    if (!format->nSamplesPerSec || !format->nChannels || format->nChannels > 32 ||
        format->nBlockAlign != format->nChannels * (format->wBitsPerSample / 8) ||
        !((floating && format->wBitsPerSample == 32) ||
          (pcm && (format->wBitsPerSample == 16 || format->wBitsPerSample == 24 || format->wBitsPerSample == 32))))
        throw hresult_error(E_NOTIMPL, L"The target output has an unsupported diagnostic playback format.");
    check_hresult(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                     AUDCLNT_STREAMFLAGS_NOPERSIST, 1000000, 0, format, nullptr));
    Handle audio(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!audio)
        throw_last_error();
    check_hresult(client->SetEventHandle(audio.get()));
    UINT32 capacity{};
    check_hresult(client->GetBufferSize(&capacity));
    com_ptr<IAudioRenderClient> render;
    check_hresult(client->GetService(__uuidof(IAudioRenderClient), render.put_void()));
    const uint64_t total = uint64_t(format->nSamplesPerSec) * 600 / 1000;
    uint64_t written{};
    auto fill = [&](UINT32 count) {
        BYTE *buffer{};
        check_hresult(render->GetBuffer(count, &buffer));
        for (UINT32 frame = 0; frame < count; ++frame)
        {
            double index = static_cast<double>(written + frame);
            double fade = std::min({1.0, index / (format->nSamplesPerSec * .01),
                                   (static_cast<double>(total) - index - 1) / (format->nSamplesPerSec * .01)});
            double value = std::sin(2 * 3.141592653589793 * 660 * index / format->nSamplesPerSec) * .025 * fade;
            for (WORD channel = 0; channel < format->nChannels; ++channel)
            {
                auto destination = buffer + frame * format->nBlockAlign + channel * (format->wBitsPerSample / 8);
                if (floating)
                {
                    float sample = static_cast<float>(value);
                    memcpy(destination, &sample, sizeof(sample));
                }
                else
                {
                    int32_t sample = static_cast<int32_t>(value * (format->wBitsPerSample == 16 ? 32767.0 :
                                                                 format->wBitsPerSample == 24 ? 8388607.0 : 2147483647.0));
                    memcpy(destination, &sample, format->wBitsPerSample / 8);
                }
            }
        }
        check_hresult(render->ReleaseBuffer(count, 0));
        written += count;
    };
    if (cancel.IsCanceled() || finished.IsCanceled())
        return;
    fill(static_cast<UINT32>(std::min<uint64_t>(capacity, total)));
    check_hresult(client->Start());
    struct Stop { IAudioClient *client; ~Stop() { client->Stop(); } } stop{client.get()};
    Log(L"tone-start", std::format(L"endpoint={} rate={} channels={} bits={} frames={}",
                                   PrivacyTag(output.id), format->nSamplesPerSec, format->nChannels,
                                   format->wBitsPerSample, total));
    auto deadline = GetTickCount64() + 3000;
    HANDLE waits[]{cancel.event.get(), finished.event.get(), audio.get()};
    for (;;)
    {
        UINT32 padding{};
        check_hresult(client->GetCurrentPadding(&padding));
        if (written == total && !padding)
            break;
        if (GetTickCount64() >= deadline)
            throw hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT), L"Diagnostic playback did not drain within 3 seconds.");
        auto state = WaitForMultipleObjects(3, waits, FALSE, 100);
        if (state == WAIT_OBJECT_0 || state == WAIT_OBJECT_0 + 1)
        {
            Log(L"tone-canceled", L"Diagnostic playback stopped before completion.");
            return;
        }
        if (state == WAIT_FAILED)
            throw_last_error();
        check_hresult(client->GetCurrentPadding(&padding));
        if (padding > capacity)
            throw hresult_error(E_UNEXPECTED, L"Invalid audio padding.");
        auto count = static_cast<UINT32>(std::min<uint64_t>(capacity - padding, total - written));
        if (count)
            fill(count);
    }
    Log(L"tone-complete", L"600ms of audio rendered and drained on the selected endpoint; other clients may keep A2DP open.");
}
} // namespace Native
