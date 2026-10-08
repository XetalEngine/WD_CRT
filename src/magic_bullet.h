#pragma once
#include "structs.h"

namespace wdgs::magic_bullet
{
    void reset();
    bool probe(const FVector& observer);
    bool retarget_all(const FVector& target, const FVector& observer, std::uint32_t local_pawn, std::uint32_t local_vehicle);
} // namespace wdgs::magic_bullet
