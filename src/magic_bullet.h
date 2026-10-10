#pragma once
#include "structs.h"
#include "prediction.h"
#ifdef WD_TEST
#include "projectile_subsystem.h"
#endif

namespace wdgs::magic_bullet
{
    void reset();
#ifdef WD_TEST
    bool test_retarget(const projectile_subsystem::Instance& round, const FVector& target, std::uint32_t pawn, std::uint32_t vehicle);
#endif
    bool probe(const FVector& observer);
    bool retarget_all(const FVector& target, const FVector& observer, std::uint32_t local_pawn, std::uint32_t local_vehicle, bool once = false, bool report = false, const prediction::Input* trajectory = nullptr);
} // namespace wdgs::magic_bullet
