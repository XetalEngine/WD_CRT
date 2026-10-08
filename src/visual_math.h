#pragma once
#include "game.h"
#include <cmath>

namespace visual_math
{
    inline bool finite(const FVector& v)
    {
        return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
    }

    struct Projection
    {
        FVector origin, forward, right, up;
        double scale = 0;

        explicit Projection(const CameraIPC& camera)
        {
            constexpr double rad = 0.017453292519943295;
            const double p = camera.rotation.Pitch * rad, y = camera.rotation.Yaw * rad, r = camera.rotation.Roll * rad;
            const double sp = std::sin(p), cp = std::cos(p), sy = std::sin(y), cy = std::cos(y), sr = std::sin(r), cr = std::cos(r);
            origin = camera.location;
            forward = {cp * cy, cp * sy, sp};
            right = {sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp};
            up = {-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp};
            if (std::isfinite(camera.fov) && camera.fov > 1 && camera.fov < 179)
                scale = screen_width * 0.5 / std::tan(camera.fov * rad * 0.5);
        }

        game::ScreenPoint project(const FVector& world) const
        {
            const FVector d = world - origin;
            const double z = d.X * forward.X + d.Y * forward.Y + d.Z * forward.Z;
            if (!finite(d) || !std::isfinite(z) || z < 1 || scale <= 0)
                return {};
            const double x = screen_width * 0.5 + (d.X * right.X + d.Y * right.Y + d.Z * right.Z) * scale / z;
            const double y = screen_height * 0.5 - (d.X * up.X + d.Y * up.Y + d.Z * up.Z) * scale / z;
            if (!std::isfinite(x) || !std::isfinite(y) || std::fabs(x) > screen_width * 4 || std::fabs(y) > screen_height * 4)
                return {};
            return {static_cast<float>(x), static_cast<float>(y), true};
        }
    };
} // namespace visual_math
