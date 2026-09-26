using System.Text;

namespace BluetoothAudioCodec.WinUI.Services;

internal static class CrashDiagnostics
{
    private const long MaximumLogBytes = 1024 * 1024;
    private const int MaximumExceptionCharacters = 32 * 1024;
    private static readonly Lock FileLock = new();
    private static int _initialized;

    public static string LogPath { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "BluetoothAudioCodecViewer",
        "Logs",
        "runtime.log");

    public static void Initialize()
    {
        if (Interlocked.Exchange(ref _initialized, 1) != 0)
        {
            return;
        }

        AppDomain.CurrentDomain.UnhandledException += (_, eventArgs) =>
        {
            var exception = eventArgs.ExceptionObject as Exception
                ?? new InvalidOperationException(
                    $"Unhandled non-exception object: {eventArgs.ExceptionObject}");
            TryWrite("app-domain-unhandled", exception);
        };

        TaskScheduler.UnobservedTaskException += (_, eventArgs) =>
            TryWrite("unobserved-task", eventArgs.Exception);
    }

    public static void TryWrite(string stage, Exception exception)
    {
        try
        {
            var details = exception.ToString();
            if (details.Length > MaximumExceptionCharacters)
            {
                details = details[..MaximumExceptionCharacters] +
                    Environment.NewLine + "[exception text truncated]";
            }

            var entry = string.Create(
                System.Globalization.CultureInfo.InvariantCulture,
                $"[{DateTimeOffset.UtcNow:O}] pid={Environment.ProcessId} stage={stage}{Environment.NewLine}{details}{Environment.NewLine}{Environment.NewLine}");

            lock (FileLock)
            {
                var directory = Path.GetDirectoryName(LogPath)
                    ?? throw new InvalidOperationException(
                        "The diagnostic log directory is unavailable.");
                Directory.CreateDirectory(directory);

                if (File.Exists(LogPath) &&
                    new FileInfo(LogPath).Length >= MaximumLogBytes)
                {
                    File.Move(LogPath, LogPath + ".previous", overwrite: true);
                }

                File.AppendAllText(LogPath, entry, new UTF8Encoding(false));
            }
        }
        catch
        {
            // Diagnostics must never turn a recoverable failure into a crash.
        }
    }
}
