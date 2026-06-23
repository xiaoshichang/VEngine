#include "Editor/Core/EditorBuiltinResources.h"

#include "Engine/Runtime/Math/Math.h"

#include <algorithm>
#include <cmath>

namespace ve::editor
{
    namespace
    {
        constexpr UInt32 AtlasWidth = 128;
        constexpr UInt32 AtlasHeight = 64;
        constexpr UInt32 IconSize = 64;

        void BlendPixel(BuiltinGizmoIconAtlas& atlas, Int32 x, Int32 y, Float32 alpha)
        {
            if (x < 0 || y < 0 || x >= static_cast<Int32>(atlas.width) || y >= static_cast<Int32>(atlas.height))
            {
                return;
            }

            alpha = Clamp(alpha, 0.0f, 1.0f);
            const SizeT index = (static_cast<SizeT>(y) * atlas.rowPitch) + (static_cast<SizeT>(x) * 4);
            const UInt8 sourceAlpha = static_cast<UInt8>(alpha * 255.0f);
            atlas.pixels[index + 0] = 255;
            atlas.pixels[index + 1] = 255;
            atlas.pixels[index + 2] = 255;
            atlas.pixels[index + 3] = (std::max)(atlas.pixels[index + 3], sourceAlpha);
        }

        void DrawDisc(BuiltinGizmoIconAtlas& atlas, Float32 centerX, Float32 centerY, Float32 radius, Float32 alpha)
        {
            const Int32 minX = static_cast<Int32>(std::floor(centerX - radius - 1.0f));
            const Int32 maxX = static_cast<Int32>(std::ceil(centerX + radius + 1.0f));
            const Int32 minY = static_cast<Int32>(std::floor(centerY - radius - 1.0f));
            const Int32 maxY = static_cast<Int32>(std::ceil(centerY + radius + 1.0f));
            for (Int32 y = minY; y <= maxY; ++y)
            {
                for (Int32 x = minX; x <= maxX; ++x)
                {
                    const Float32 dx = (static_cast<Float32>(x) + 0.5f) - centerX;
                    const Float32 dy = (static_cast<Float32>(y) + 0.5f) - centerY;
                    const Float32 coverage = Clamp(radius + 0.5f - Sqrt((dx * dx) + (dy * dy)), 0.0f, 1.0f);
                    BlendPixel(atlas, x, y, coverage * alpha);
                }
            }
        }

        void DrawLine(BuiltinGizmoIconAtlas& atlas, Float32 ax, Float32 ay, Float32 bx, Float32 by, Float32 thickness)
        {
            const Float32 dx = bx - ax;
            const Float32 dy = by - ay;
            const Float32 lengthSquared = (dx * dx) + (dy * dy);
            if (lengthSquared <= Math::DefaultEpsilon)
            {
                return;
            }

            const Float32 radius = thickness * 0.5f;
            const Int32 minX = static_cast<Int32>(std::floor((std::min)(ax, bx) - radius - 1.0f));
            const Int32 maxX = static_cast<Int32>(std::ceil((std::max)(ax, bx) + radius + 1.0f));
            const Int32 minY = static_cast<Int32>(std::floor((std::min)(ay, by) - radius - 1.0f));
            const Int32 maxY = static_cast<Int32>(std::ceil((std::max)(ay, by) + radius + 1.0f));

            for (Int32 y = minY; y <= maxY; ++y)
            {
                for (Int32 x = minX; x <= maxX; ++x)
                {
                    const Float32 px = static_cast<Float32>(x) + 0.5f;
                    const Float32 py = static_cast<Float32>(y) + 0.5f;
                    const Float32 t = Clamp(((px - ax) * dx + (py - ay) * dy) / lengthSquared, 0.0f, 1.0f);
                    const Float32 closestX = ax + (dx * t);
                    const Float32 closestY = ay + (dy * t);
                    const Float32 distanceX = px - closestX;
                    const Float32 distanceY = py - closestY;
                    const Float32 coverage = Clamp(radius + 0.5f - Sqrt((distanceX * distanceX) + (distanceY * distanceY)), 0.0f, 1.0f);
                    BlendPixel(atlas, x, y, coverage);
                }
            }
        }

        void DrawCircle(BuiltinGizmoIconAtlas& atlas, Float32 centerX, Float32 centerY, Float32 radius, Float32 thickness)
        {
            const Int32 minX = static_cast<Int32>(std::floor(centerX - radius - thickness - 1.0f));
            const Int32 maxX = static_cast<Int32>(std::ceil(centerX + radius + thickness + 1.0f));
            const Int32 minY = static_cast<Int32>(std::floor(centerY - radius - thickness - 1.0f));
            const Int32 maxY = static_cast<Int32>(std::ceil(centerY + radius + thickness + 1.0f));
            const Float32 halfThickness = thickness * 0.5f;
            for (Int32 y = minY; y <= maxY; ++y)
            {
                for (Int32 x = minX; x <= maxX; ++x)
                {
                    const Float32 dx = (static_cast<Float32>(x) + 0.5f) - centerX;
                    const Float32 dy = (static_cast<Float32>(y) + 0.5f) - centerY;
                    const Float32 distance = Sqrt((dx * dx) + (dy * dy));
                    const Float32 coverage = Clamp(halfThickness + 0.5f - std::abs(distance - radius), 0.0f, 1.0f);
                    BlendPixel(atlas, x, y, coverage);
                }
            }
        }

        void DrawCameraIcon(BuiltinGizmoIconAtlas& atlas)
        {
            constexpr Float32 x = 0.0f;
            DrawLine(atlas, x + 17.0f, 24.0f, x + 37.0f, 24.0f, 4.0f);
            DrawLine(atlas, x + 37.0f, 24.0f, x + 37.0f, 40.0f, 4.0f);
            DrawLine(atlas, x + 37.0f, 40.0f, x + 17.0f, 40.0f, 4.0f);
            DrawLine(atlas, x + 17.0f, 40.0f, x + 17.0f, 24.0f, 4.0f);
            DrawLine(atlas, x + 39.0f, 28.0f, x + 50.0f, 20.0f, 4.0f);
            DrawLine(atlas, x + 39.0f, 36.0f, x + 50.0f, 44.0f, 4.0f);
            DrawLine(atlas, x + 50.0f, 20.0f, x + 50.0f, 44.0f, 4.0f);
            DrawDisc(atlas, x + 24.0f, 32.0f, 3.0f, 0.82f);
        }

        void DrawLightIcon(BuiltinGizmoIconAtlas& atlas)
        {
            constexpr Float32 x = 64.0f;
            DrawCircle(atlas, x + 32.0f, 32.0f, 13.0f, 4.0f);
            DrawLine(atlas, x + 32.0f, 10.0f, x + 32.0f, 18.0f, 4.0f);
            DrawLine(atlas, x + 32.0f, 46.0f, x + 32.0f, 54.0f, 4.0f);
            DrawLine(atlas, x + 10.0f, 32.0f, x + 18.0f, 32.0f, 4.0f);
            DrawLine(atlas, x + 46.0f, 32.0f, x + 54.0f, 32.0f, 4.0f);
            DrawLine(atlas, x + 16.0f, 16.0f, x + 21.0f, 21.0f, 3.5f);
            DrawLine(atlas, x + 43.0f, 43.0f, x + 48.0f, 48.0f, 3.5f);
            DrawLine(atlas, x + 48.0f, 16.0f, x + 43.0f, 21.0f, 3.5f);
            DrawLine(atlas, x + 21.0f, 43.0f, x + 16.0f, 48.0f, 3.5f);
        }
    } // namespace

    BuiltinGizmoIconAtlas GenerateBuiltinGizmoIconAtlas()
    {
        BuiltinGizmoIconAtlas atlas = {};
        atlas.width = AtlasWidth;
        atlas.height = AtlasHeight;
        atlas.rowPitch = AtlasWidth * 4;
        atlas.pixels.resize(static_cast<SizeT>(atlas.rowPitch) * atlas.height, 0);

        DrawCameraIcon(atlas);
        DrawLightIcon(atlas);
        return atlas;
    }

    BuiltinGizmoIconUvRect GetBuiltinGizmoIconUvRect(BuiltinGizmoIcon icon) noexcept
    {
        const UInt32 tileIndex = icon == BuiltinGizmoIcon::Light ? 1u : 0u;
        const Float32 minU = static_cast<Float32>(tileIndex * IconSize) / static_cast<Float32>(AtlasWidth);
        const Float32 maxU = static_cast<Float32>((tileIndex + 1u) * IconSize) / static_cast<Float32>(AtlasWidth);
        return BuiltinGizmoIconUvRect{minU, 0.0f, maxU, 1.0f};
    }
} // namespace ve::editor
