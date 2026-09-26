using System.Globalization;
using Windows.Devices.Bluetooth;
using Windows.Devices.Enumeration;
using Windows.Media.Devices;

namespace BluetoothAudioCodec.WinUI.Services;

internal sealed class BluetoothAudioDeviceWatcher : IDisposable
{
    private const string IsConnectedProperty = "System.Devices.Aep.IsConnected";
    private const string MajorClassProperty = "System.Devices.Aep.Bluetooth.Cod.Major";
    private const string DisplayNameProperty = "System.ItemNameDisplay";

    private static readonly string[] RequestedProperties =
    [
        IsConnectedProperty,
        MajorClassProperty,
        DisplayNameProperty
    ];

    private readonly Lock _gate = new();
    private readonly Dictionary<string, DeviceState> _devices =
        new(StringComparer.Ordinal);
    private DeviceWatcher? _bluetoothWatcher;
    private DeviceWatcher? _audioRenderWatcher;
    private bool _bluetoothEnumerationCompleted;
    private bool _bluetoothUnavailable;
    private bool _started;
    private bool _disposed;

    public event EventHandler<BluetoothAudioStatus>? StatusChanged;

    public void Start()
    {
        lock (_gate)
        {
            ObjectDisposedException.ThrowIf(_disposed, this);
            if (_started)
            {
                return;
            }

            _started = true;
        }

        StartBluetoothWatcher();
        StartAudioRenderWatcher();
        PublishCurrentStatus();
    }

    public void Dispose()
    {
        DeviceWatcher? bluetoothWatcher;
        DeviceWatcher? audioRenderWatcher;

        lock (_gate)
        {
            if (_disposed)
            {
                return;
            }

            _disposed = true;
            bluetoothWatcher = _bluetoothWatcher;
            audioRenderWatcher = _audioRenderWatcher;
            _bluetoothWatcher = null;
            _audioRenderWatcher = null;
            _devices.Clear();
        }

        if (bluetoothWatcher is not null)
        {
            bluetoothWatcher.Added -= OnBluetoothDeviceAdded;
            bluetoothWatcher.Updated -= OnBluetoothDeviceUpdated;
            bluetoothWatcher.Removed -= OnBluetoothDeviceRemoved;
            bluetoothWatcher.EnumerationCompleted -= OnBluetoothEnumerationCompleted;
            bluetoothWatcher.Stopped -= OnBluetoothWatcherStopped;
            TryStop(bluetoothWatcher, "bluetooth-watcher-stop");
        }

        if (audioRenderWatcher is not null)
        {
            audioRenderWatcher.Added -= OnAudioRenderChanged;
            audioRenderWatcher.Updated -= OnAudioRenderUpdated;
            audioRenderWatcher.Removed -= OnAudioRenderRemoved;
            TryStop(audioRenderWatcher, "audio-watcher-stop");
        }

        StatusChanged = null;
    }

    private void StartBluetoothWatcher()
    {
        try
        {
            var selector = BluetoothDevice.GetDeviceSelectorFromPairingState(true);
            var watcher = DeviceInformation.CreateWatcher(
                selector,
                RequestedProperties,
                DeviceInformationKind.AssociationEndpoint);

            watcher.Added += OnBluetoothDeviceAdded;
            watcher.Updated += OnBluetoothDeviceUpdated;
            watcher.Removed += OnBluetoothDeviceRemoved;
            watcher.EnumerationCompleted += OnBluetoothEnumerationCompleted;
            watcher.Stopped += OnBluetoothWatcherStopped;

            lock (_gate)
            {
                if (_disposed)
                {
                    return;
                }

                _bluetoothWatcher = watcher;
            }

            watcher.Start();
        }
        catch (Exception exception)
        {
            CrashDiagnostics.TryWrite("bluetooth-watcher-start", exception);
            lock (_gate)
            {
                _bluetoothUnavailable = true;
            }
        }
    }

    private void StartAudioRenderWatcher()
    {
        try
        {
            var watcher = DeviceInformation.CreateWatcher(
                MediaDevice.GetAudioRenderSelector());
            watcher.Added += OnAudioRenderChanged;
            watcher.Updated += OnAudioRenderUpdated;
            watcher.Removed += OnAudioRenderRemoved;

            lock (_gate)
            {
                if (_disposed)
                {
                    return;
                }

                _audioRenderWatcher = watcher;
            }

            watcher.Start();
        }
        catch (Exception exception)
        {
            CrashDiagnostics.TryWrite("audio-watcher-start", exception);
        }
    }

    private void OnBluetoothDeviceAdded(
        DeviceWatcher sender,
        DeviceInformation device)
    {
        var state = new DeviceState(
            GetDisplayName(device.Name, device.Properties),
            GetBoolean(device.Properties, IsConnectedProperty),
            GetUInt16(device.Properties, MajorClassProperty));

        lock (_gate)
        {
            if (_disposed)
            {
                return;
            }

            _devices[device.Id] = state;
        }

        PublishCurrentStatus();
    }

    private void OnBluetoothDeviceUpdated(
        DeviceWatcher sender,
        DeviceInformationUpdate update)
    {
        lock (_gate)
        {
            if (_disposed)
            {
                return;
            }

            _devices.TryGetValue(update.Id, out var existing);
            _devices[update.Id] = new DeviceState(
                GetDisplayName(existing?.Name, update.Properties),
                update.Properties.ContainsKey(IsConnectedProperty)
                    ? GetBoolean(update.Properties, IsConnectedProperty) ?? false
                    : existing?.IsConnected,
                update.Properties.ContainsKey(MajorClassProperty)
                    ? GetUInt16(update.Properties, MajorClassProperty)
                    : existing?.MajorClass);
        }

        PublishCurrentStatus();
    }

    private void OnBluetoothDeviceRemoved(
        DeviceWatcher sender,
        DeviceInformationUpdate update)
    {
        lock (_gate)
        {
            if (_disposed)
            {
                return;
            }

            _devices.Remove(update.Id);
        }

        PublishCurrentStatus();
    }

    private void OnBluetoothEnumerationCompleted(DeviceWatcher sender, object args)
    {
        lock (_gate)
        {
            if (_disposed)
            {
                return;
            }

            _bluetoothEnumerationCompleted = true;
        }

        PublishCurrentStatus();
    }

    private void OnBluetoothWatcherStopped(DeviceWatcher sender, object args)
    {
        lock (_gate)
        {
            if (_disposed)
            {
                return;
            }

            _bluetoothUnavailable = true;
        }

        PublishCurrentStatus();
    }

    private void OnAudioRenderChanged(DeviceWatcher sender, DeviceInformation device)
    {
        PublishCurrentStatus();
    }

    private void OnAudioRenderUpdated(
        DeviceWatcher sender,
        DeviceInformationUpdate update)
    {
        PublishCurrentStatus();
    }

    private void OnAudioRenderRemoved(
        DeviceWatcher sender,
        DeviceInformationUpdate update)
    {
        PublishCurrentStatus();
    }

    private void PublishCurrentStatus()
    {
        BluetoothAudioStatus status;
        EventHandler<BluetoothAudioStatus>? handler;

        lock (_gate)
        {
            if (_disposed)
            {
                return;
            }

            var connectedNames = _devices.Values
                .Where(device =>
                    device.IsConnected == true &&
                    device.MajorClass == (ushort)BluetoothMajorClass.AudioVideo)
                .Select(device => device.Name)
                .Where(name => !string.IsNullOrWhiteSpace(name))
                .Select(name => name!)
                .Distinct(StringComparer.CurrentCultureIgnoreCase)
                .Order(StringComparer.CurrentCultureIgnoreCase)
                .ToArray();

            var connectedCount = _devices.Values.Count(device =>
                device.IsConnected == true &&
                device.MajorClass == (ushort)BluetoothMajorClass.AudioVideo);

            var state = connectedCount > 0
                ? BluetoothAudioConnectionState.Connected
                : _bluetoothUnavailable
                    ? BluetoothAudioConnectionState.Unavailable
                    : _bluetoothEnumerationCompleted
                        ? BluetoothAudioConnectionState.Disconnected
                        : BluetoothAudioConnectionState.Checking;

            status = new BluetoothAudioStatus(state, connectedNames);
            handler = StatusChanged;
        }

        handler?.Invoke(this, status);
    }

    private static string? GetDisplayName(
        string? fallback,
        IReadOnlyDictionary<string, object> properties)
    {
        return properties.TryGetValue(DisplayNameProperty, out var value) &&
            value is string name &&
            !string.IsNullOrWhiteSpace(name)
                ? name
                : string.IsNullOrWhiteSpace(fallback) ? null : fallback;
    }

    private static bool? GetBoolean(
        IReadOnlyDictionary<string, object> properties,
        string propertyName)
    {
        if (!properties.TryGetValue(propertyName, out var value) || value is null)
        {
            return null;
        }

        try
        {
            return Convert.ToBoolean(value, CultureInfo.InvariantCulture);
        }
        catch (FormatException)
        {
            return null;
        }
        catch (InvalidCastException)
        {
            return null;
        }
    }

    private static ushort? GetUInt16(
        IReadOnlyDictionary<string, object> properties,
        string propertyName)
    {
        if (!properties.TryGetValue(propertyName, out var value) || value is null)
        {
            return null;
        }

        try
        {
            return Convert.ToUInt16(value, CultureInfo.InvariantCulture);
        }
        catch (FormatException)
        {
            return null;
        }
        catch (InvalidCastException)
        {
            return null;
        }
        catch (OverflowException)
        {
            return null;
        }
    }

    private static void TryStop(DeviceWatcher watcher, string stage)
    {
        try
        {
            if (watcher.Status is DeviceWatcherStatus.Started or
                DeviceWatcherStatus.EnumerationCompleted)
            {
                watcher.Stop();
            }
        }
        catch (Exception exception)
        {
            CrashDiagnostics.TryWrite(stage, exception);
        }
    }

    private sealed record DeviceState(
        string? Name,
        bool? IsConnected,
        ushort? MajorClass);
}

internal enum BluetoothAudioConnectionState
{
    Checking,
    Connected,
    Disconnected,
    Unavailable
}

internal sealed record BluetoothAudioStatus(
    BluetoothAudioConnectionState State,
    IReadOnlyList<string> ConnectedDeviceNames);
