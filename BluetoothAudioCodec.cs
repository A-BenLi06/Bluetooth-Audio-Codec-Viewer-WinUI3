#:property TargetFramework=net10.0
#:property PublishAot=false
#:package Microsoft.Diagnostics.Tracing.TraceEvent@3.2.4

using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Runtime.Versioning;
using System.Text;
using System.Text.Json;
using Microsoft.Diagnostics.Tracing;
using Microsoft.Diagnostics.Tracing.Session;

return BluetoothAudioCodecApp.Run(args);

internal static class BluetoothAudioCodecApp
{
    private static readonly Guid BthA2dpSessionProvider =
        new("8776ad1e-5022-4451-a566-f47e708b9075");

    private const string BthA2dpEventProvider = "Microsoft.Windows.Bluetooth.BthA2dp";
    private const string BthA2dpStreamingEvent = "A2dpStreaming";

    public static int Run(string[] args)
    {
        var parseResult = Options.TryParse(args);
        if (parseResult.Error is not null)
        {
            Console.Error.WriteLine($"Error: {parseResult.Error}");
            Console.Error.WriteLine("Use --help to see the supported options.");
            return 2;
        }

        var options = parseResult.Value!;
        if (options.ShowHelp)
        {
            PrintHelp();
            return 0;
        }

        if (!OperatingSystem.IsWindows())
        {
            Console.Error.WriteLine("Error: this program only supports Windows.");
            return 2;
        }

        if (TraceEventSession.IsElevated() != true)
        {
            Console.Error.WriteLine(
                "Error: ETW Bluetooth tracing requires an elevated terminal. " +
                "Open PowerShell as Administrator and run the command again.");
            return 2;
        }

        var defaultEndpoint = AudioEndpoint.TryGetDefaultRenderEndpoint();
        if (!options.Json)
        {
            PrintIntroduction(options, defaultEndpoint);
        }

        var observations = new List<CodecObservation>();
        var parseErrors = new List<string>();
        var sessionName = $"BluetoothAudioCodec-{Environment.ProcessId}";

        try
        {
            using var session = new TraceEventSession(
                sessionName,
                TraceEventSessionOptions.Create);

            session.StopOnDispose = true;

            session.Source.Dynamic.AddCallbackForProviderEvent(
                BthA2dpEventProvider,
                BthA2dpStreamingEvent,
                traceEvent =>
                {
                    try
                    {
                        var standardCodecId = checked((byte)ReadUnsignedPayload(
                            traceEvent,
                            "A2dpStandardCodecId",
                            fallbackIndex: 3));
                        var vendorId = ReadUnsignedPayload(
                            traceEvent,
                            "A2dpVendorId",
                            fallbackIndex: 4);
                        var vendorCodecId = ReadUnsignedPayload(
                            traceEvent,
                            "A2dpVendorCodecId",
                            fallbackIndex: 5);

                        var observation = new CodecObservation(
                            Protocol: "Bluetooth Classic A2DP",
                            Codec: CodecCatalog.Resolve(
                                standardCodecId,
                                vendorId,
                                vendorCodecId),
                            StandardCodecId: standardCodecId,
                            VendorId: vendorId,
                            VendorCodecId: vendorCodecId,
                            ObservedAt: traceEvent.TimeStamp.ToUniversalTime(),
                            DefaultOutput: defaultEndpoint?.FriendlyName);

                        observations.Add(observation);

                        if (!options.Json)
                        {
                            PrintObservation(observation);
                        }

                        if (!options.Watch)
                        {
                            session.Source.StopProcessing();
                        }
                    }
                    catch (Exception exception)
                    {
                        parseErrors.Add(exception.Message);
                    }
                });

            session.EnableProvider(
                BthA2dpSessionProvider,
                TraceEventLevel.Verbose,
                matchAnyKeywords: 0);

            using var stopWorker = new CancellationTokenSource();
            var timeoutWorker = StartTimeoutWorker(
                session,
                options,
                stopWorker.Token);

            ConsoleCancelEventHandler cancelHandler = (_, eventArgs) =>
            {
                eventArgs.Cancel = true;
                session.Source.StopProcessing();
            };

            Console.CancelKeyPress += cancelHandler;
            try
            {
                session.Source.Process();
            }
            finally
            {
                Console.CancelKeyPress -= cancelHandler;
                stopWorker.Cancel();
                try
                {
                    timeoutWorker.GetAwaiter().GetResult();
                }
                catch (OperationCanceledException)
                {
                    // Normal completion after an event or Ctrl+C.
                }
            }
        }
        catch (Exception exception)
        {
            if (options.Json)
            {
                WriteJson(new
                {
                    success = false,
                    error = exception.Message,
                    observations
                });
            }
            else
            {
                Console.Error.WriteLine($"Error: unable to collect the Bluetooth trace: {exception.Message}");
            }

            return 2;
        }

        if (options.Json)
        {
            WriteJson(new
            {
                success = observations.Count > 0,
                defaultOutput = defaultEndpoint?.FriendlyName,
                observations,
                parseErrors
            });
        }
        else if (observations.Count == 0)
        {
            PrintNoObservation(options, parseErrors);
        }

        return observations.Count > 0 ? 0 : 1;
    }

    private static Task StartTimeoutWorker(
        TraceEventSession session,
        Options options,
        CancellationToken cancellationToken)
    {
        return Task.Run(async () =>
        {
            if (!options.NoTone)
            {
                await Task.Delay(TimeSpan.FromMilliseconds(750), cancellationToken);
                DiagnosticTone.Play();
            }

            var remaining = options.Timeout -
                (options.NoTone ? TimeSpan.Zero : TimeSpan.FromMilliseconds(750));

            if (remaining > TimeSpan.Zero)
            {
                await Task.Delay(remaining, cancellationToken);
            }

            session.Source.StopProcessing();
        }, cancellationToken);
    }

    private static ulong ReadUnsignedPayload(
        TraceEvent traceEvent,
        string fieldName,
        int fallbackIndex)
    {
        var payloadNames = traceEvent.PayloadNames;
        var index = Array.FindIndex(
            payloadNames,
            name => string.Equals(name, fieldName, StringComparison.OrdinalIgnoreCase));

        if (index < 0)
        {
            index = fallbackIndex;
        }

        if (index >= payloadNames.Length)
        {
            throw new InvalidDataException(
                $"The {BthA2dpStreamingEvent} event does not contain {fieldName}. " +
                $"Payload fields: {string.Join(", ", payloadNames)}");
        }

        var value = traceEvent.PayloadValue(index);
        if (value is null)
        {
            throw new InvalidDataException(
                $"The {fieldName} payload value is empty.");
        }

        return Convert.ToUInt64(value, CultureInfo.InvariantCulture);
    }

    private static void PrintIntroduction(
        Options options,
        AudioEndpoint? defaultEndpoint)
    {
        Console.WriteLine("Windows Bluetooth audio codec inspector");
        Console.WriteLine("---------------------------------------");
        Console.WriteLine(
            $"Default output : {defaultEndpoint?.FriendlyName ?? "Unavailable"}");
        Console.WriteLine($"Listening      : {options.Timeout.TotalSeconds:0} seconds");
        Console.WriteLine();
        Console.WriteLine(
            "The active A2DP codec is reported when Windows opens or closes the " +
            "Bluetooth audio stream.");

        if (options.NoTone)
        {
            Console.WriteLine(
                "Start and then stop audio playback, or reconnect the headset, " +
                "while this program is listening.");
        }
        else
        {
            Console.WriteLine(
                "A short, quiet tone will be played through the default output. " +
                "If the headset is not the default output, play and stop audio on it manually.");
        }

        Console.WriteLine("Press Ctrl+C to stop.");
        Console.WriteLine();
    }

    private static void PrintObservation(CodecObservation observation)
    {
        Console.WriteLine("Codec observed");
        Console.WriteLine($"  Protocol          : {observation.Protocol}");
        Console.WriteLine($"  Codec             : {observation.Codec}");
        Console.WriteLine(
            $"  Standard codec ID : 0x{observation.StandardCodecId:X2}");
        Console.WriteLine($"  Vendor ID         : 0x{observation.VendorId:X8}");
        Console.WriteLine(
            $"  Vendor codec ID   : 0x{observation.VendorCodecId:X4}");
        Console.WriteLine(
            $"  Time (UTC)        : {observation.ObservedAt:O}");
        Console.WriteLine();
    }

    private static void PrintNoObservation(
        Options options,
        IReadOnlyCollection<string> parseErrors)
    {
        Console.WriteLine("No A2DP codec event was observed.");
        Console.WriteLine(
            "Stop all playback before rerunning, or reconnect the headset while " +
            $"the {options.Timeout.TotalSeconds:0}-second listener is active.");
        Console.WriteLine(
            "If the headset microphone is in use, Windows may be using HFP " +
            "(CVSD/mSBC) instead of A2DP. Bluetooth LE Audio uses LC3 and is not " +
            "reported by the A2DP provider.");

        foreach (var error in parseErrors.Distinct(StringComparer.Ordinal))
        {
            Console.WriteLine($"Trace parse warning: {error}");
        }
    }

    private static void PrintHelp()
    {
        Console.WriteLine(
            """
            Windows Bluetooth audio codec inspector

            Usage:
              dotnet run BluetoothAudioCodec.cs
              dotnet run BluetoothAudioCodec.cs -- [options]

            Options:
              --timeout <seconds>  Listening time from 5 through 300 (default: 30).
              --no-tone            Do not play the diagnostic tone.
              --watch              Report every codec event until the timeout expires.
              --json               Emit one JSON result object.
              -h, --help           Show this help.

            Requirements:
              - Windows 10 or Windows 11
              - .NET 10 SDK
              - An elevated PowerShell or Windows Terminal session

            The first run restores Microsoft.Diagnostics.Tracing.TraceEvent from NuGet.
            """);
    }

    private static void WriteJson(object value)
    {
        Console.WriteLine(JsonSerializer.Serialize(
            value,
            new JsonSerializerOptions
            {
                WriteIndented = true,
                PropertyNamingPolicy = JsonNamingPolicy.CamelCase
            }));
    }
}

internal sealed record CodecObservation(
    string Protocol,
    string Codec,
    byte StandardCodecId,
    ulong VendorId,
    ulong VendorCodecId,
    DateTime ObservedAt,
    string? DefaultOutput);

internal static class CodecCatalog
{
    private static readonly IReadOnlyDictionary<byte, string> StandardCodecs =
        new Dictionary<byte, string>
        {
            [0x00] = "SBC",
            [0x01] = "MPEG-1/2 Audio (MP3)",
            [0x02] = "MPEG-2/4 AAC",
            [0x03] = "MPEG-D USAC",
            [0x04] = "ATRAC family"
        };

    private static readonly IReadOnlyDictionary<(ulong Vendor, ulong Codec), string>
        VendorCodecs = new Dictionary<(ulong, ulong), string>
        {
            [(0x004F, 0x0001)] = "Qualcomm/CSR aptX Classic",
            [(0x00D7, 0x0024)] = "Qualcomm/CSR aptX HD",
            [(0x000A, 0x0002)] = "Qualcomm/CSR aptX Low Latency",
            [(0x00D7, 0x0002)] = "Qualcomm/CSR aptX Low Latency",
            [(0x000A, 0x0001)] = "Qualcomm/CSR FastStream",
            [(0x000A, 0x0104)] = "Qualcomm/CSR True Wireless Stereo v3 AAC",
            [(0x000A, 0x0105)] = "Qualcomm/CSR True Wireless Stereo v3 MP3",
            [(0x000A, 0x0106)] = "Qualcomm/CSR True Wireless Stereo v3 aptX",
            [(0x012D, 0x00AA)] = "Sony LDAC",
            [(0x0075, 0x0102)] = "Samsung HD",
            [(0x0075, 0x0103)] = "Samsung Scalable Codec",
            [(0x053A, 0x484C)] = "Savitech LHDC"
        };

    public static string Resolve(
        byte standardCodecId,
        ulong vendorId,
        ulong vendorCodecId)
    {
        if (StandardCodecs.TryGetValue(standardCodecId, out var standardName))
        {
            return standardName;
        }

        if (standardCodecId != 0xFF)
        {
            return $"Unknown standard codec (0x{standardCodecId:X2})";
        }

        return VendorCodecs.TryGetValue((vendorId, vendorCodecId), out var vendorName)
            ? vendorName
            : $"Unknown vendor codec (vendor 0x{vendorId:X8}, codec 0x{vendorCodecId:X4})";
    }
}

internal sealed record Options(
    TimeSpan Timeout,
    bool NoTone,
    bool Watch,
    bool Json,
    bool ShowHelp)
{
    public static ParseResult TryParse(string[] args)
    {
        var timeout = TimeSpan.FromSeconds(30);
        var noTone = false;
        var watch = false;
        var json = false;
        var showHelp = false;

        for (var index = 0; index < args.Length; index++)
        {
            switch (args[index])
            {
                case "--timeout":
                    if (++index >= args.Length ||
                        !int.TryParse(
                            args[index],
                            NumberStyles.None,
                            CultureInfo.InvariantCulture,
                            out var seconds) ||
                        seconds is < 5 or > 300)
                    {
                        return new ParseResult(
                            null,
                            "--timeout must be an integer from 5 through 300.");
                    }

                    timeout = TimeSpan.FromSeconds(seconds);
                    break;

                case "--no-tone":
                    noTone = true;
                    break;

                case "--watch":
                    watch = true;
                    break;

                case "--json":
                    json = true;
                    break;

                case "-h":
                case "--help":
                    showHelp = true;
                    break;

                default:
                    return new ParseResult(
                        null,
                        $"unknown option '{args[index]}'.");
            }
        }

        return new ParseResult(
            new Options(timeout, noTone, watch, json, showHelp),
            null);
    }
}

internal sealed record ParseResult(Options? Value, string? Error);

internal static class DiagnosticTone
{
    private const uint SoundMemory = 0x0004;
    private const uint SoundNoDefault = 0x0002;

    public static void Play()
    {
        var wave = CreateWave(
            frequency: 660,
            duration: TimeSpan.FromMilliseconds(600),
            sampleRate: 44_100,
            amplitude: 1_200);

        var pinnedWave = GCHandle.Alloc(wave, GCHandleType.Pinned);
        try
        {
            _ = PlaySound(
                pinnedWave.AddrOfPinnedObject(),
                IntPtr.Zero,
                SoundMemory | SoundNoDefault);
        }
        finally
        {
            pinnedWave.Free();
        }
    }

    private static byte[] CreateWave(
        double frequency,
        TimeSpan duration,
        int sampleRate,
        short amplitude)
    {
        var sampleCount = checked((int)(duration.TotalSeconds * sampleRate));
        var dataSize = checked(sampleCount * sizeof(short));

        using var stream = new MemoryStream(capacity: 44 + dataSize);
        using var writer = new BinaryWriter(stream, Encoding.ASCII, leaveOpen: true);

        writer.Write(Encoding.ASCII.GetBytes("RIFF"));
        writer.Write(36 + dataSize);
        writer.Write(Encoding.ASCII.GetBytes("WAVE"));
        writer.Write(Encoding.ASCII.GetBytes("fmt "));
        writer.Write(16);
        writer.Write((short)1);
        writer.Write((short)1);
        writer.Write(sampleRate);
        writer.Write(sampleRate * sizeof(short));
        writer.Write((short)sizeof(short));
        writer.Write((short)16);
        writer.Write(Encoding.ASCII.GetBytes("data"));
        writer.Write(dataSize);

        var fadeSamples = Math.Max(1, sampleRate / 100);
        for (var sampleIndex = 0; sampleIndex < sampleCount; sampleIndex++)
        {
            var fadeIn = Math.Min(1.0, sampleIndex / (double)fadeSamples);
            var fadeOut = Math.Min(
                1.0,
                (sampleCount - sampleIndex - 1) / (double)fadeSamples);
            var envelope = Math.Min(fadeIn, fadeOut);
            var angle = 2 * Math.PI * frequency * sampleIndex / sampleRate;
            var sample = (short)(Math.Sin(angle) * amplitude * envelope);
            writer.Write(sample);
        }

        writer.Flush();
        return stream.ToArray();
    }

    [DllImport("winmm.dll", EntryPoint = "PlaySoundW")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PlaySound(
        IntPtr sound,
        IntPtr module,
        uint flags);
}

internal sealed record AudioEndpoint(string FriendlyName)
{
    private static readonly Guid MMDeviceEnumeratorClassId =
        new("BCDE0395-E52F-467C-8E3D-C4579291692E");

    [SupportedOSPlatform("windows")]
    public static AudioEndpoint? TryGetDefaultRenderEndpoint()
    {
        IMMDeviceEnumerator? enumerator = null;
        IMMDevice? device = null;
        IPropertyStore? properties = null;

        try
        {
            var enumeratorType = Type.GetTypeFromCLSID(
                MMDeviceEnumeratorClassId,
                throwOnError: true)!;
            enumerator = (IMMDeviceEnumerator)Activator.CreateInstance(enumeratorType)!;
            Marshal.ThrowExceptionForHR(enumerator.GetDefaultAudioEndpoint(
                EDataFlow.Render,
                ERole.Multimedia,
                out device));
            Marshal.ThrowExceptionForHR(device.OpenPropertyStore(
                StorageAccessMode.Read,
                out properties));

            var key = new PropertyKey(
                new Guid("a45c254e-df1c-4efd-8020-67d146a850e0"),
                14);
            Marshal.ThrowExceptionForHR(properties.GetValue(ref key, out var value));

            try
            {
                var name = value.Type == VariantType.String
                    ? Marshal.PtrToStringUni(value.Pointer)
                    : null;
                return string.IsNullOrWhiteSpace(name)
                    ? null
                    : new AudioEndpoint(name);
            }
            finally
            {
                _ = PropVariantClear(ref value);
            }
        }
        catch
        {
            return null;
        }
        finally
        {
            ReleaseComObject(properties);
            ReleaseComObject(device);
            ReleaseComObject(enumerator);
        }
    }

    [SupportedOSPlatform("windows")]
    private static void ReleaseComObject(object? value)
    {
        if (value is not null && Marshal.IsComObject(value))
        {
            _ = Marshal.ReleaseComObject(value);
        }
    }

    [DllImport("ole32.dll")]
    private static extern int PropVariantClear(ref PropVariant value);

    private enum EDataFlow
    {
        Render,
        Capture,
        All
    }

    private enum ERole
    {
        Console,
        Multimedia,
        Communications
    }

    private enum StorageAccessMode
    {
        Read
    }

    private enum VariantType : ushort
    {
        String = 31
    }

    [StructLayout(LayoutKind.Sequential)]
    private readonly record struct PropertyKey(Guid FormatId, uint PropertyId);

    [StructLayout(LayoutKind.Explicit)]
    private struct PropVariant
    {
        [FieldOffset(0)]
        public VariantType Type;

        [FieldOffset(8)]
        public IntPtr Pointer;
    }

    [ComImport]
    [Guid("A95664D2-9614-4F35-A746-DE8DB63617E6")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IMMDeviceEnumerator
    {
        [PreserveSig]
        int EnumAudioEndpoints(
            EDataFlow dataFlow,
            uint stateMask,
            out IntPtr devices);

        [PreserveSig]
        int GetDefaultAudioEndpoint(
            EDataFlow dataFlow,
            ERole role,
            out IMMDevice device);

        [PreserveSig]
        int GetDevice(
            [MarshalAs(UnmanagedType.LPWStr)] string id,
            out IMMDevice device);

        [PreserveSig]
        int RegisterEndpointNotificationCallback(IntPtr client);

        [PreserveSig]
        int UnregisterEndpointNotificationCallback(IntPtr client);
    }

    [ComImport]
    [Guid("D666063F-1587-4E43-81F1-B948E807363F")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IMMDevice
    {
        [PreserveSig]
        int Activate(
            ref Guid interfaceId,
            uint classContext,
            IntPtr activationParameters,
            out IntPtr instance);

        [PreserveSig]
        int OpenPropertyStore(
            StorageAccessMode accessMode,
            out IPropertyStore properties);

        [PreserveSig]
        int GetId(out IntPtr id);

        [PreserveSig]
        int GetState(out uint state);
    }

    [ComImport]
    [Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99")]
    [InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    private interface IPropertyStore
    {
        [PreserveSig]
        int GetCount(out uint propertyCount);

        [PreserveSig]
        int GetAt(uint propertyIndex, out PropertyKey key);

        [PreserveSig]
        int GetValue(ref PropertyKey key, out PropVariant value);

        [PreserveSig]
        int SetValue(ref PropertyKey key, ref PropVariant value);

        [PreserveSig]
        int Commit();
    }
}
