#pragma once

#include <ymir/core/types.hpp>

#include <optional>
#include <string>

namespace app::gfx {

/// @brief Graphics backend options.
enum class Backend {
    Null,
#if YMIR_PLATFORM_HAS_DIRECT3D
    Direct3D11,
    Direct3D12,
#endif
#if YMIR_PLATFORM_HAS_METAL
    Metal,
#endif
#if YMIR_PLATFORM_HAS_VULKAN
    Vulkan,
#endif
    SDLRenderer,
};

/// @brief A list of all backends available on this host system.
inline constexpr Backend kGraphicsBackends[] = {
    Backend::Null,
#if YMIR_PLATFORM_HAS_DIRECT3D
    Backend::Direct3D11,  Backend::Direct3D12,
#endif
#if YMIR_PLATFORM_HAS_METAL
    Backend::Metal,
#endif
#if YMIR_PLATFORM_HAS_VULKAN
    Backend::Vulkan,
#endif
    Backend::SDLRenderer,
};

/// @brief The preferred default backend for this host system.
inline constexpr Backend kDefaultBackend =
#if YMIR_PLATFORM_HAS_DIRECT3D
    Backend::Direct3D12;
#elif YMIR_PLATFORM_HAS_METAL
    Backend::Metal;
#elif YMIR_PLATFORM_HAS_VULKAN
    Backend::Vulkan;
#else
    Backend::SDLRenderer;
#endif

/// @brief Retrieves a human-readable name for the backend.
/// @param[in] backend the backend type
/// @return the backend name
inline constexpr const char *GraphicsBackendName(Backend backend) {
    switch (backend) {
    default: [[fallthrough]];
    case Backend::Null: return "Null";
#if YMIR_PLATFORM_HAS_DIRECT3D
    case Backend::Direct3D11: return "Direct3D 11";
    case Backend::Direct3D12: return "Direct3D 12";
#endif
#if YMIR_PLATFORM_HAS_METAL
    case Backend::Metal: return "Metal";
#endif
#if YMIR_PLATFORM_HAS_VULKAN
    case Backend::Vulkan: return "Vulkan";
#endif
    case Backend::SDLRenderer: return "SDL Renderer";
    }
}

// -----------------------------------------------------------------------------

/// @brief Stable unique identifier for a graphics adapter based on its PCIe address.
struct AdapterID {
    uint16 bus : 8;
    uint16 device : 5;
    uint16 function : 3;

    constexpr bool operator==(const AdapterID &) const = default;

    /// @brief Converts this identifier to a string in the format "<bus>:<device>.<function>", with each component
    /// represented by a hexadecimal number. For example, "01:00.0".
    /// @return a string representation of this graphics adapater identifier
    std::string ToString() const;

    /// @brief Attempts to parse the entire string as a graphics adapter identifier.
    /// @param[in] str the string to parse
    /// @return the parsed adapter ID or `std::nullopt` if the string does not contain a valid adapter ID
    static std::optional<AdapterID> TryParse(std::string_view str);
};

/// @brief Graphics adapter information.
struct Adapter {
    AdapterID id;     ///< Stable unique identifier for this adapter
    std::string name; ///< Human-readable adapter name

    /// @brief Returns a human-readable name for this adapter in the format "[id] name".
    /// @return a string representation of this graphics adapter
    std::string ToString() const;
};

// -----------------------------------------------------------------------------

/// @brief Graphics presentation modes.
enum class PresentMode {
    VSync,    ///< Enqueues all frames and synchronizes to vertical retrace.
    Mailbox,  ///< Stores in a mailbox the latest frame to be presented. May or may not synchronize to vertical retrace.
    Adaptive, ///< Presents frames without synchronization while enabling VRR on supported displays.
    NoSync,   ///< Presents frames without synchronization.
};

/// @brief Possible outcomes of a frame presentation action.
enum class PresentResult {
    /// @brief The frame was presented successfully.
    Ok,

    /// @brief The frame was presented, but occluded.
    /// Typically occurs when trying to render graphics to a minimized window.
    Occluded,
};

/// @brief Pixel formats.
/// The names follow the naming convention `<bit-layout>_<value-format>`.
/// The bit layout is a sequence of elements describing the layout of the pixel data from least to most significant
/// bits. Each element is composed of a letter followed by digits indicating the number of bits used by the element.
/// The letters represent the following data:
///   R = red
///   G = green
///   B = blue
///   A = alpha
///   X = dummy/unused
///   D = depth
///   S = stencil
/// Note that the dummy element may contain garbage. Pixel shaders must account for that by ignoring the value.
/// The value format indicates how the data is consumed from the CPU side and presented to the GPU:
///   UNORM = unsigned integers on the CPU, normalized floating point values on the GPU
///   UINT = unsigned integers on CPU and GPU
enum class PixelFormat {
    Unknown,

    R8G8B8A8_UNORM,
    R8G8B8X8_UNORM,
    B8G8R8A8_UNORM,
    B8G8R8X8_UNORM,

    // TODO: add more formats as needed
};

/// @brief Returns the size (in bytes) of a pixel in the given format.
/// @param[in] format the pixel format
/// @return the number of bytes per pixel in that format
inline uint64 PixelFormatUnitSize(PixelFormat format) {
    switch (format) {
    case PixelFormat::Unknown: return 0;
    case PixelFormat::R8G8B8A8_UNORM: return 4;
    case PixelFormat::R8G8B8X8_UNORM: return 4;
    case PixelFormat::B8G8R8A8_UNORM: return 4;
    case PixelFormat::B8G8R8X8_UNORM: return 4;
    }
    return 0;
}

/// @brief A point's coordinates in 2D space using floating point values.
struct FPoint2D {
    float x, y;
};

/// @brief A rectangle specification using unsigned integers for the top-left origin coordinate and the dimensions.
struct IRect {
    uint32 x, y;
    uint32 w, h;
};

/// @brief A rectangle specification using floating point values for the top-left origin coordinate and the dimensions.
struct FRect {
    float x, y;
    float w, h;
};

/// @brief RGBA color specification.
struct ColorRGBA {
    float r, g, b, a;
};

// -----------------------------------------------------------------------------

enum class TextureAccess {
    Static,       ///< Texture data lives on the GPU only; updates require a staging buffer (expensive)
    Streaming,    ///< Texture data can be changed at any point
    RenderTarget, ///< Texture can be used as render target
};

enum class TextureFilterMode {
    Nearest,
    Linear,
};

/// @brief A texture identifier, used for operations with textures on a graphics context.
/// This ID is immutable for the lifetime of the texture, even when resized.
using TextureID = uintptr_t;

/// @brief Sentinel value representing an invalid texture ID.
inline constexpr TextureID kInvalidTextureID = -1;

/// @brief Texture format specifications.
struct Texture2DSpec {
    /// @brief Width of the texture.
    uint32 width = 0;

    /// @brief Height of the texture
    uint32 height = 0;

    /// @brief Texel format.
    PixelFormat format = PixelFormat::Unknown;

    /// @brief Texture access mode.
    TextureAccess access = TextureAccess::Static;

    /// @brief Texture magnification and minification filter mode.
    TextureFilterMode filterMode = TextureFilterMode::Linear;

    /// @brief Texture name, for graphics debugging tools.
    std::string name;
};

/// @brief Specifications for a display texture.
struct DisplayTextureSpec {
    /// @brief The texture's identifier.
    TextureID id;

    /// @brief The width of the texture.
    uint32 textureWidth;

    /// @brief The height of the texture.
    uint32 textureHeight;

    /// @brief The width of the rendered display area contained in the texture.
    uint32 renderWidth;

    /// @brief The height of the renderer display area contained in the texture.
    uint32 renderHeight;

    /// @brief The native VDP2 resolution width.
    uint32 nativeWidth;

    /// @brief The native VDP2 resolution height.
    uint32 nativeHeight;
};

} // namespace app::gfx
