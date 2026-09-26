using Microsoft.UI.Xaml;
using BluetoothAudioCodec.WinUI.Services;

namespace BluetoothAudioCodec.WinUI;

public partial class App : Application
{
    private Window? _window;

    public App()
    {
        InitializeComponent();
        UnhandledException += (_, eventArgs) =>
            CrashDiagnostics.TryWrite("xaml-unhandled", eventArgs.Exception);
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        _window = new MainWindow();
        _window.Activate();
    }
}
