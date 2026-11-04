#pragma once
#include <string>
#include <vector>
#include <span>
#include <string_view>
#include <cstdint>
#include <array>
#include <optional>
namespace Native
{
using PlaybackDigest = std::array<std::uint8_t, 32>;
struct PlaybackMediaIdentity
{
    std::optional<PlaybackDigest> title, artist, album;
    std::optional<std::uint32_t> track;
};
inline std::wstring_view PlaybackMediaChange(PlaybackMediaIdentity const &before, PlaybackMediaIdentity const &after)
{
    if (before.title && after.title && before.title != after.title) return L"media-title-changed";
    if (before.artist && after.artist && before.artist != after.artist) return L"media-artist-changed";
    if (before.album && after.album && before.album != after.album) return L"media-album-changed";
    if (before.track && after.track && before.track != after.track) return L"media-track-changed";
    return {};
}
}
