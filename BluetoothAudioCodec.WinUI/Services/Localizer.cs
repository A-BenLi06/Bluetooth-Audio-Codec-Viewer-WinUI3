using Microsoft.Windows.ApplicationModel.Resources;

namespace BluetoothAudioCodec.WinUI.Services;

internal static class Localizer
{
    private static readonly ResourceManager _manager = new();

    public static string GetString(string name)
    {
        var candidate = _manager.MainResourceMap.TryGetValue($"Resources/{name}");
        return candidate?.ValueAsString ?? name;
    }
}
