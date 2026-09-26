using BluetoothAudioCodec.WinUI.Services;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

namespace BluetoothAudioCodec.WinUI;

public static class Program
{
    [STAThread]
    public static int Main(string[] args)
    {
        CrashDiagnostics.Initialize();

        if (ElevatedCodecBridge.IsHelperInvocation(args))
        {
            try
            {
                return ElevatedCodecBridge.RunHelperAsync(args)
                    .GetAwaiter()
                    .GetResult();
            }
            catch (Exception exception)
            {
                CrashDiagnostics.TryWrite("helper-entry-point", exception);
                return ElevatedCodecBridge.UnexpectedFailureExitCode;
            }
        }

        WinRT.ComWrappersSupport.InitializeComWrappers();
        Application.Start(_ =>
        {
            var context = new DispatcherQueueSynchronizationContext(
                DispatcherQueue.GetForCurrentThread());
            SynchronizationContext.SetSynchronizationContext(context);
            new App();
        });

        return 0;
    }
}
