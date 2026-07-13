using System.ComponentModel;
using System.Diagnostics;
using BluetoothAudioCodec.WinUI.Services;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Graphics;

namespace BluetoothAudioCodec.WinUI;

public sealed partial class MainWindow : Window
{
    private readonly BluetoothCodecDetector _detector = new();
    private CancellationTokenSource? _detection;
    private bool _isElevated;
    private bool _isLoaded;

    public MainWindow()
    {
        InitializeComponent();

        ExtendsContentIntoTitleBar = true;
        SetTitleBar(TitleBarGrid);

        Closed += (_, _) => _detection?.Cancel();
    }

    private void ConfigureWindow()
    {
        const int preferredWidth = 860;
        const int preferredHeight = 760;
        const int minimumWidth = 680;
        const int minimumHeight = 600;

        var scale = RootGrid.XamlRoot?.RasterizationScale ?? 1.0;
        var preferredPixelWidth = (int)Math.Round(preferredWidth * scale);
        var preferredPixelHeight = (int)Math.Round(preferredHeight * scale);
        var minimumPixelWidth = (int)Math.Round(minimumWidth * scale);
        var minimumPixelHeight = (int)Math.Round(minimumHeight * scale);

        var displayArea = DisplayArea.GetFromWindowId(AppWindow.Id, DisplayAreaFallback.Primary);
        var workArea = displayArea.WorkArea;
        var width = Math.Min(preferredPixelWidth, Math.Max(minimumPixelWidth, workArea.Width - 64));
        var height = Math.Min(preferredPixelHeight, Math.Max(minimumPixelHeight, workArea.Height - 64));
        var x = workArea.X + Math.Max(0, (workArea.Width - width) / 2);
        var y = workArea.Y + Math.Max(0, (workArea.Height - height) / 2);

        AppWindow.MoveAndResize(new RectInt32(x, y, width, height));
        AppWindow.TitleBar.ButtonBackgroundColor = Colors.Transparent;
        AppWindow.TitleBar.ButtonInactiveBackgroundColor = Colors.Transparent;

        if (AppWindow.Presenter is OverlappedPresenter presenter)
        {
            presenter.PreferredMinimumWidth = minimumPixelWidth;
            presenter.PreferredMinimumHeight = minimumPixelHeight;
        }
    }

    private void OnRootLoaded(object sender, RoutedEventArgs e)
    {
        if (_isLoaded)
        {
            return;
        }

        _isLoaded = true;
        ConfigureWindow();
        _isElevated = BluetoothCodecDetector.IsElevated;

        var endpoint = AudioEndpoint.TryGetDefaultRenderEndpoint();
        DeviceText.Text = endpoint?.FriendlyName ?? "No default output found";

        if (_isElevated)
        {
            SetReadyState();
            return;
        }

        StatusText.Text = "Admin required";
        StatusDot.Fill = GetThemeBrush("SystemFillColorCautionBrush");
        DetectButtonText.Text = "Restart as administrator";
        DetectIcon.Glyph = "\uE7EF";
        ShowMessage(
            InfoBarSeverity.Informational,
            "Administrator access required",
            "Windows only exposes the Bluetooth codec trace to elevated processes.");
    }

    private async void OnDetectClicked(object sender, RoutedEventArgs e)
    {
        if (_detection is not null)
        {
            _detection.Cancel();
            return;
        }

        if (!_isElevated)
        {
            RestartElevated();
            return;
        }

        using var cancellation = new CancellationTokenSource();
        _detection = cancellation;
        SetListeningState();

        try
        {
            var result = await _detector.DetectAsync(
                TimeSpan.FromSeconds(30),
                playTone: true,
                cancellation.Token);

            if (result.Observation is not null)
            {
                ShowObservation(result.Observation);
            }
            else if (result.Canceled)
            {
                SetReadyState();
                ShowMessage(
                    InfoBarSeverity.Informational,
                    "Detection canceled",
                    "No Bluetooth settings were changed.");
            }
            else
            {
                ShowNoObservation(result.Warnings);
            }
        }
        catch (Exception exception)
        {
            StatusText.Text = "Error";
            StatusDot.Fill = GetThemeBrush("SystemFillColorCriticalBrush");
            ShowMessage(
                InfoBarSeverity.Error,
                "Unable to inspect the codec",
                exception.Message);
        }
        finally
        {
            _detection = null;
            DetectionProgress.IsActive = false;
            DetectionProgress.Visibility = Visibility.Collapsed;
            DetectIcon.Visibility = Visibility.Visible;
            DetectIcon.Glyph = "\uE721";
            DetectButtonText.Text = "Detect again";
        }
    }

    private void SetListeningState()
    {
        CodecText.Text = "Listening…";
        ProtocolText.Text = "Waiting for an A2DP stream event";
        StatusText.Text = "Listening";
        StatusDot.Fill = GetThemeBrush("AccentFillColorDefaultBrush");
        MessageBar.IsOpen = false;
        DetectionProgress.Visibility = Visibility.Visible;
        DetectionProgress.IsActive = true;
        DetectIcon.Visibility = Visibility.Collapsed;
        DetectButtonText.Text = "Cancel";
    }

    private void SetReadyState()
    {
        StatusText.Text = "Ready";
        StatusDot.Fill = GetThemeBrush("TextFillColorSecondaryBrush");
        DetectButtonText.Text = "Detect codec";
        DetectIcon.Glyph = "\uE721";
    }

    private void ShowObservation(CodecObservation observation)
    {
        CodecText.Text = observation.Codec;
        ProtocolText.Text = observation.Protocol.Replace(" A2DP", " · A2DP", StringComparison.Ordinal);
        DeviceText.Text = observation.DefaultOutput ?? "No default output found";
        StandardIdText.Text = $"0x{observation.StandardCodecId:X2}";
        VendorIdText.Text = $"0x{observation.VendorId:X8}";
        VendorCodecIdText.Text = $"0x{observation.VendorCodecId:X4}";
        ObservedAtText.Text = observation.ObservedAt.ToLocalTime().ToString("yyyy-MM-dd  HH:mm:ss");
        StatusText.Text = "Detected";
        StatusDot.Fill = GetThemeBrush("SystemFillColorSuccessBrush");

        ShowMessage(
            InfoBarSeverity.Success,
            "Codec detected",
            "The negotiated codec was captured from the Windows Bluetooth audio trace.");
    }

    private void ShowNoObservation(IReadOnlyCollection<string> warnings)
    {
        CodecText.Text = "No event";
        ProtocolText.Text = "Stop playback or reconnect, then try again";
        StatusText.Text = "Try again";
        StatusDot.Fill = GetThemeBrush("SystemFillColorCautionBrush");

        var detail = warnings.Count == 0
            ? "Windows did not open or close an A2DP stream during the 30-second window."
            : warnings.First();

        ShowMessage(
            InfoBarSeverity.Warning,
            "No codec event observed",
            detail + " Headset microphone use may switch Windows to HFP instead.");
    }

    private void RestartElevated()
    {
        try
        {
            var processPath = Environment.ProcessPath
                ?? throw new InvalidOperationException("The application path is unavailable.");

            Process.Start(new ProcessStartInfo(processPath)
            {
                UseShellExecute = true,
                Verb = "runas",
                WorkingDirectory = AppContext.BaseDirectory
            });

            Application.Current.Exit();
        }
        catch (Win32Exception exception) when (exception.NativeErrorCode == 1223)
        {
            ShowMessage(
                InfoBarSeverity.Informational,
                "Elevation canceled",
                "The app is still running without administrator access.");
        }
        catch (Exception exception)
        {
            ShowMessage(
                InfoBarSeverity.Error,
                "Could not restart the app",
                exception.Message);
        }
    }

    private void ShowMessage(InfoBarSeverity severity, string title, string message)
    {
        MessageBar.Severity = severity;
        MessageBar.Title = title;
        MessageBar.Message = message;
        MessageBar.IsOpen = true;
    }

    private static Brush GetThemeBrush(string resourceKey)
    {
        return (Brush)Application.Current.Resources[resourceKey];
    }
}
