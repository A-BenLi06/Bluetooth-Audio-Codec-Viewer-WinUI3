using System.Globalization;
using System.Runtime.InteropServices;
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
    private readonly ElevatedCodecBridge _codecBridge = new();
    private CancellationTokenSource? _detection;
    private bool _isLoaded;

    public MainWindow()
    {
        InitializeComponent();

        ExtendsContentIntoTitleBar = true;
        SetTitleBar(TitleBarGrid);
        ConfigureWindow(GetWindowScale());

        Closed += (_, _) => _detection?.Cancel();
    }

    private void ConfigureWindow(double scale)
    {
        const int preferredWidth = 860;
        const int preferredHeight = 760;
        const int minimumWidth = 680;
        const int minimumHeight = 600;

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

    private double GetWindowScale()
    {
        var windowHandle = WinRT.Interop.WindowNative.GetWindowHandle(this);
        var dpi = GetDpiForWindow(windowHandle);
        return dpi == 0 ? 1.0 : dpi / 96.0;
    }

    private void OnRootLoaded(object sender, RoutedEventArgs e)
    {
        if (_isLoaded)
        {
            return;
        }

        _isLoaded = true;

        var endpoint = AudioEndpoint.TryGetDefaultRenderEndpoint();
        DeviceText.Text = endpoint?.FriendlyName ?? Localizer.GetString("DeviceNone");
        ProtocolText.Text = Localizer.GetString("ProtocolDefault");

        SetReadyState();
    }

    private async void OnDetectClicked(object sender, RoutedEventArgs e)
    {
        if (_detection is not null)
        {
            _detection.Cancel();
            return;
        }

        using var cancellation = new CancellationTokenSource();
        _detection = cancellation;
        SetListeningState();

        try
        {
            var result = await _codecBridge.DetectAsync(
                TimeSpan.FromSeconds(30),
                playTone: true,
                cancellation.Token);

            if (result.Observation is not null)
            {
                ShowObservation(result.Observation);
            }
            else if (result.Canceled)
            {
                ShowDetectionCanceled();
            }
            else
            {
                ShowNoObservation(result.Warnings);
            }
        }
        catch (OperationCanceledException) when (cancellation.IsCancellationRequested)
        {
            ShowDetectionCanceled();
        }
        catch (ElevationCanceledException)
        {
            SetReadyState();
            ShowMessage(
                InfoBarSeverity.Informational,
                Localizer.GetString("MessageElevationCanceledTitle"),
                Localizer.GetString("MessageElevationCanceledBody"));
        }
        catch (Exception exception)
        {
            StatusText.Text = Localizer.GetString("StatusError");
            StatusDot.Fill = GetThemeBrush("SystemFillColorCriticalBrush");
            ShowMessage(
                InfoBarSeverity.Error,
                Localizer.GetString("MessageUnableTitle"),
                exception.Message);
        }
        finally
        {
            _detection = null;
            DetectionProgress.IsActive = false;
            DetectionProgress.Visibility = Visibility.Collapsed;
            DetectIcon.Visibility = Visibility.Visible;
            DetectIcon.Glyph = "\uE721";
            DetectButton.IsEnabled = true;
            DetectButtonText.Text = Localizer.GetString("DetectButtonAgain");
        }
    }

    private void SetListeningState()
    {
        CodecText.Text = Localizer.GetString("CodecListening");
        ProtocolText.Text = Localizer.GetString("ProtocolWaiting");
        StatusText.Text = Localizer.GetString("StatusListening");
        StatusDot.Fill = GetThemeBrush("AccentFillColorDefaultBrush");
        MessageBar.IsOpen = false;
        DetectionProgress.Visibility = Visibility.Visible;
        DetectionProgress.IsActive = true;
        DetectIcon.Visibility = Visibility.Collapsed;
        DetectButtonText.Text = Localizer.GetString("DetectButtonCancel");
    }

    private void SetReadyState()
    {
        StatusText.Text = Localizer.GetString("StatusReady");
        StatusDot.Fill = GetThemeBrush("TextFillColorSecondaryBrush");
        DetectButton.IsEnabled = true;
        DetectButtonText.Text = Localizer.GetString("DetectButtonInitial");
        DetectIcon.Glyph = "\uE721";
    }

    private void ShowObservation(CodecObservation observation)
    {
        CodecText.Text = observation.Codec;
        ProtocolText.Text = observation.Protocol.Replace(" A2DP", " · A2DP", StringComparison.Ordinal);
        DeviceText.Text = observation.DefaultOutput ?? Localizer.GetString("DeviceNone");
        StandardIdText.Text = $"0x{observation.StandardCodecId:X2}";
        VendorIdText.Text = $"0x{observation.VendorId:X8}";
        VendorCodecIdText.Text = $"0x{observation.VendorCodecId:X4}";
        ObservedAtText.Text = observation.ObservedAt.ToLocalTime().ToString("yyyy-MM-dd  HH:mm:ss");
        StatusText.Text = Localizer.GetString("StatusDetected");
        StatusDot.Fill = GetThemeBrush("SystemFillColorSuccessBrush");

        ShowMessage(
            InfoBarSeverity.Success,
            Localizer.GetString("MessageCodecDetectedTitle"),
            Localizer.GetString("MessageCodecDetectedBody"));
    }

    private void ShowNoObservation(IReadOnlyCollection<string> warnings)
    {
        CodecText.Text = Localizer.GetString("CodecNoEvent");
        ProtocolText.Text = Localizer.GetString("ProtocolRetryHint");
        StatusText.Text = Localizer.GetString("StatusTryAgain");
        StatusDot.Fill = GetThemeBrush("SystemFillColorCautionBrush");

        var detail = warnings.Count == 0
            ? Localizer.GetString("MessageNoEventDefaultBody")
            : warnings.First();

        ShowMessage(
            InfoBarSeverity.Warning,
            Localizer.GetString("MessageNoEventTitle"),
            string.Format(CultureInfo.CurrentCulture, Localizer.GetString("MessageNoEventBodyFormat"), detail));
    }

    private void ShowDetectionCanceled()
    {
        SetReadyState();
        ShowMessage(
            InfoBarSeverity.Informational,
            Localizer.GetString("MessageDetectionCanceledTitle"),
            Localizer.GetString("MessageDetectionCanceledBody"));
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

    [DllImport("user32.dll")]
    private static extern uint GetDpiForWindow(IntPtr windowHandle);
}
