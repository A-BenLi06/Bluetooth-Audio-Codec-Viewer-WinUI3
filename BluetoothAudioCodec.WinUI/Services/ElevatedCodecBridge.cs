using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO.Pipes;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Text.Json;

namespace BluetoothAudioCodec.WinUI.Services;

internal sealed class ElevatedCodecBridge
{
    private const string HelperSwitch = "--elevated-helper";
    private const string PipeSwitch = "--pipe";
    private const string TokenSwitch = "--token";
    private const string TimeoutSwitch = "--timeout-seconds";
    private const string PlayToneSwitch = "--play-tone";
    private const int HelperConnectTimeoutMilliseconds = 15_000;

    private readonly BluetoothCodecDetector _detector = new();

    public static bool IsHelperInvocation(IReadOnlyList<string> args)
    {
        return args.Count > 0 &&
            string.Equals(args[0], HelperSwitch, StringComparison.Ordinal);
    }

    public async Task<CodecDetectionResult> DetectAsync(
        TimeSpan timeout,
        bool playTone,
        CancellationToken cancellationToken)
    {
        if (BluetoothCodecDetector.IsElevated)
        {
            return await _detector.DetectAsync(timeout, playTone, cancellationToken);
        }

        var pipeName = $"BluetoothAudioCodec-{Environment.ProcessId}-{Guid.NewGuid():N}";
        var token = Convert.ToHexString(RandomNumberGenerator.GetBytes(32));

        using var pipe = new NamedPipeServerStream(
            pipeName,
            PipeDirection.InOut,
            maxNumberOfServerInstances: 1,
            PipeTransmissionMode.Byte,
            PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);

        using var helperProcess = StartElevatedHelper(
            pipeName,
            token,
            timeout,
            playTone);

        using var connectCancellation = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken);
        connectCancellation.CancelAfter(HelperConnectTimeoutMilliseconds);

        try
        {
            await pipe.WaitForConnectionAsync(connectCancellation.Token);
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            throw new TimeoutException(
                "The administrator helper did not connect within 15 seconds.");
        }

        using var reader = new StreamReader(pipe, leaveOpen: true);
        using var writer = new StreamWriter(pipe, leaveOpen: true)
        {
            AutoFlush = true
        };

        using var cancellationRegistration = cancellationToken.Register(
            () => TrySendCancellation(writer, token));

        var responseTimeout = timeout + TimeSpan.FromSeconds(15);
        var responseLine = await reader.ReadLineAsync()
            .WaitAsync(responseTimeout);

        if (string.IsNullOrWhiteSpace(responseLine))
        {
            throw new InvalidDataException(
                "The administrator helper closed without returning a result.");
        }

        var response = JsonSerializer.Deserialize<ElevatedCodecResponse>(responseLine)
            ?? throw new InvalidDataException(
                "The administrator helper returned an empty response.");

        if (!CryptographicOperations.FixedTimeEquals(
                Convert.FromHexString(token),
                Convert.FromHexString(response.Token)))
        {
            throw new InvalidDataException(
                "The administrator helper response could not be authenticated.");
        }

        if (!string.IsNullOrWhiteSpace(response.Error))
        {
            throw new InvalidOperationException(response.Error);
        }

        return new CodecDetectionResult(
            response.Observation,
            AudioEndpoint.TryGetDefaultRenderEndpoint(),
            response.Warnings,
            response.Canceled);
    }

    public static async Task<int> RunHelperAsync(IReadOnlyList<string> args)
    {
        ElevatedHelperArguments helperArguments;
        try
        {
            helperArguments = ParseHelperArguments(args);
        }
        catch
        {
            return 2;
        }

        using var pipe = new NamedPipeClientStream(
            serverName: ".",
            helperArguments.PipeName,
            PipeDirection.InOut,
            PipeOptions.Asynchronous,
            TokenImpersonationLevel.Identification);

        try
        {
            await pipe.ConnectAsync(HelperConnectTimeoutMilliseconds);
        }
        catch
        {
            return 3;
        }

        using var reader = new StreamReader(pipe, leaveOpen: true);
        using var writer = new StreamWriter(pipe, leaveOpen: true)
        {
            AutoFlush = true
        };
        using var detectionCancellation = new CancellationTokenSource();

        var cancellationMonitor = MonitorCancellationAsync(
            reader,
            helperArguments.Token,
            detectionCancellation);

        ElevatedCodecResponse response;
        try
        {
            if (!BluetoothCodecDetector.IsElevated)
            {
                throw new InvalidOperationException(
                    "The codec helper was not started with administrator access.");
            }

            var detector = new BluetoothCodecDetector();
            var result = await detector.DetectAsync(
                helperArguments.Timeout,
                helperArguments.PlayTone,
                detectionCancellation.Token);

            response = new ElevatedCodecResponse(
                helperArguments.Token,
                result.Observation,
                result.Warnings.ToArray(),
                result.Canceled,
                Error: null);
        }
        catch (Exception exception)
        {
            response = new ElevatedCodecResponse(
                helperArguments.Token,
                Observation: null,
                Warnings: [],
                Canceled: detectionCancellation.IsCancellationRequested,
                Error: exception.Message);
        }

        try
        {
            await writer.WriteLineAsync(JsonSerializer.Serialize(response));
        }
        catch
        {
            return 4;
        }
        finally
        {
            await detectionCancellation.CancelAsync();
            try
            {
                await cancellationMonitor;
            }
            catch
            {
                // The UI may close the pipe after receiving the response.
            }
        }

        return response.Error is null ? 0 : 5;
    }

    private static Process StartElevatedHelper(
        string pipeName,
        string token,
        TimeSpan timeout,
        bool playTone)
    {
        var executablePath = GetCurrentExecutablePath();
        var startInfo = new ProcessStartInfo
        {
            FileName = executablePath,
            UseShellExecute = true,
            Verb = "runas",
            WorkingDirectory = Path.GetDirectoryName(executablePath)
                ?? AppContext.BaseDirectory
        };

        startInfo.ArgumentList.Add(HelperSwitch);
        startInfo.ArgumentList.Add(PipeSwitch);
        startInfo.ArgumentList.Add(pipeName);
        startInfo.ArgumentList.Add(TokenSwitch);
        startInfo.ArgumentList.Add(token);
        startInfo.ArgumentList.Add(TimeoutSwitch);
        startInfo.ArgumentList.Add(
            Math.Ceiling(timeout.TotalSeconds).ToString(CultureInfo.InvariantCulture));

        if (playTone)
        {
            startInfo.ArgumentList.Add(PlayToneSwitch);
        }

        try
        {
            return Process.Start(startInfo)
                ?? throw new InvalidOperationException(
                    "Windows did not create the administrator helper process.");
        }
        catch (Win32Exception exception) when (exception.NativeErrorCode == 1223)
        {
            throw new ElevationCanceledException(exception);
        }
    }

    private static async Task MonitorCancellationAsync(
        StreamReader reader,
        string expectedToken,
        CancellationTokenSource detectionCancellation)
    {
        var commandLine = await reader.ReadLineAsync();
        if (string.IsNullOrWhiteSpace(commandLine))
        {
            await detectionCancellation.CancelAsync();
            return;
        }

        var command = JsonSerializer.Deserialize<ElevatedCodecCommand>(commandLine);
        if (command is not null &&
            string.Equals(command.Action, "cancel", StringComparison.Ordinal) &&
            TokensMatch(expectedToken, command.Token))
        {
            await detectionCancellation.CancelAsync();
        }
    }

    private static void TrySendCancellation(StreamWriter writer, string token)
    {
        try
        {
            writer.WriteLine(JsonSerializer.Serialize(
                new ElevatedCodecCommand(token, "cancel")));
        }
        catch
        {
            // The helper may already have returned its result and closed.
        }
    }

    private static ElevatedHelperArguments ParseHelperArguments(
        IReadOnlyList<string> args)
    {
        if (!IsHelperInvocation(args))
        {
            throw new ArgumentException("The helper switch is missing.");
        }

        var pipeName = ReadValue(args, PipeSwitch);
        var token = ReadValue(args, TokenSwitch);
        var timeoutText = ReadValue(args, TimeoutSwitch);

        if (string.IsNullOrWhiteSpace(pipeName) ||
            pipeName.Length > 128 ||
            !pipeName.All(character => char.IsAsciiLetterOrDigit(character) ||
                character is '-' or '_'))
        {
            throw new ArgumentException("The helper pipe name is invalid.");
        }

        if (token.Length != 64 || !token.All(Uri.IsHexDigit))
        {
            throw new ArgumentException("The helper token is invalid.");
        }

        if (!double.TryParse(
                timeoutText,
                NumberStyles.Number,
                CultureInfo.InvariantCulture,
                out var timeoutSeconds) ||
            timeoutSeconds is < 1 or > 60)
        {
            throw new ArgumentException("The helper timeout is invalid.");
        }

        return new ElevatedHelperArguments(
            pipeName,
            token,
            TimeSpan.FromSeconds(timeoutSeconds),
            args.Any(argument => string.Equals(
                argument,
                PlayToneSwitch,
                StringComparison.Ordinal)));
    }

    private static string ReadValue(IReadOnlyList<string> args, string name)
    {
        for (var index = 1; index < args.Count - 1; index++)
        {
            if (string.Equals(args[index], name, StringComparison.Ordinal))
            {
                return args[index + 1];
            }
        }

        throw new ArgumentException($"The {name} argument is missing.");
    }

    private static bool TokensMatch(string expected, string candidate)
    {
        try
        {
            return CryptographicOperations.FixedTimeEquals(
                Convert.FromHexString(expected),
                Convert.FromHexString(candidate));
        }
        catch (FormatException)
        {
            return false;
        }
    }

    private static string GetCurrentExecutablePath()
    {
        var processPath = Environment.ProcessPath;
        if (!string.IsNullOrWhiteSpace(processPath) && File.Exists(processPath))
        {
            return processPath;
        }

        using var currentProcess = Process.GetCurrentProcess();
        processPath = currentProcess.MainModule?.FileName;
        if (!string.IsNullOrWhiteSpace(processPath) && File.Exists(processPath))
        {
            return processPath;
        }

        throw new InvalidOperationException(
            "The application executable path is unavailable.");
    }

    private sealed record ElevatedHelperArguments(
        string PipeName,
        string Token,
        TimeSpan Timeout,
        bool PlayTone);

    private sealed record ElevatedCodecCommand(string Token, string Action);

    private sealed record ElevatedCodecResponse(
        string Token,
        CodecObservation? Observation,
        string[] Warnings,
        bool Canceled,
        string? Error);
}

internal sealed class ElevationCanceledException : Exception
{
    public ElevationCanceledException(Exception innerException)
        : base("Administrator approval was canceled.", innerException)
    {
    }
}
