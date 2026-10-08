#pragma once

#include "structs.h"

namespace wdgs::anti_sam
{

    struct Status
    {
        bool incoming = false;
        bool flare_sent = false;
        float distance_meters = 0.f;
        float predicted_miss_distance_meters = 0.f;
    };

    // Checks the reusable projectile pool after the caller verifies that the local
    // pawn occupies a rotary vehicle seat. The native vehicle flare action is
    // invoked when a matching incoming SAM projectile is inside the flare radius. The returned
    // warning starts at 1000 m and carries the closest threat's live/predicted
    // distance through short projectile-pool replication gaps.
    Status tick(void* rotary_vehicle, const FVector& observer, bool auto_flare, bool flare_warning);
    void reset();

} // namespace wdgs::anti_sam
