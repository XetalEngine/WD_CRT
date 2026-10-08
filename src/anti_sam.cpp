#include "stdafx.h"
#include "anti_sam.h"
#include "engine_funcs.h"
#include "projectile_subsystem.h"
#include "classes.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <string>
#include <unordered_set>

namespace wdgs::anti_sam
{
    namespace
    {

        // Rotary vehicles can translate several metres between 33 ms scans while the
        // pilot yaws.  Keep a generous acquisition radius so a valid missile is not
        // dropped simply because the helicopter moved across the old boundary.
        // The game stops replicating/rendering projectile actors past roughly 1000 m;
        // keeping the detector at that same boundary avoids pretending that a farther
        // range can provide data the client never receives.
        constexpr double kWarningRadiusMeters = 1000.0;
        constexpr double kFlareRadiusMeters = 100.0;
        constexpr double kFlareCorridorMeters = 50.0;
        constexpr double kMinimumDistanceDropMeters = 0.02;
        constexpr double kMinimumSpeed = 1000.0;
        constexpr double kMaximumSpeed = 200000.0;
        constexpr double kImmediateClosingSpeed = 0.0; // any measured closing component
        // The pool advances guided rounds from the launch/flying state (2) to the
        // guided/terminal state (3) while the same instance is still moving.  The
        // Stinger trace showed the exact transition at 67 m -> 17 m; accepting only
        // state 2 discarded the second sample and the approach streak never reached
        // the confirmation threshold.
        constexpr std::uint8_t kLaunchState = 2;
        constexpr std::uint8_t kGuidedState = 3;
        constexpr std::uint64_t kScanIntervalMs = 33;
        constexpr std::uint64_t kTrackContinuityMs = 2500;
        constexpr std::uint64_t kTrackRetentionMs = 5000;
        constexpr std::uint64_t kWarningHoldMs = 4000;
        constexpr std::uint64_t kFlareCooldownMs = 650;
        // One countermeasure action emits a defensive burst.  When two guided rounds
        // arrive in the same wave, do not queue a second delayed ProcessEvent after
        // the first burst; wait for the wave to clear before arming another action.
        constexpr std::uint64_t kFlareWaveHoldMs = 1200;
        constexpr std::uint64_t kFlareFeedbackMs = 500;

        struct ThreatTrack
        {
            double distance_meters = 0.0;
            double flight_time = 0.0;
            double closing_speed_mps = 0.0;
            std::uint64_t last_seen_ms = 0;
            std::uint8_t consecutive_approach_samples = 0;
        };

        struct LatchedThreat
        {
            std::uintptr_t address = 0;
            double distance_meters = 0.0;
            double closing_speed_mps = 0.0;
            double predicted_miss_distance_meters = 0.0;
            std::uint64_t sampled_ms = 0;
            std::uint8_t receding_samples = 0;
        };

        FNameValue g_sam_projectile_name{};
        bool g_sam_name_ready = false;
        // The data asset pointer is stable across pool slots and remains usable during
        // short FName-pool/ProcessEvent gaps.  Keep it as a secondary identity so a
        // single failed decode cannot hide an incoming round.
        std::unordered_set<std::uintptr_t> g_sam_projectile_data;
        Status g_last_status{};
        std::uint64_t g_next_scan_ms = 0;
        std::uint64_t g_next_flare_ms = 0;
        std::uint64_t g_flare_wave_until_ms = 0;
        std::uint64_t g_flare_feedback_until_ms = 0;
        std::uint64_t g_last_threat_seen_ms = 0;
        std::unordered_map<std::uintptr_t, ThreatTrack> g_threat_tracks;
        LatchedThreat g_latched_threat{};

        bool finite_vector(const FVector& value)
        {
            return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
        }

        constexpr const char* kSamProjectileDataName = "DA_72mm_ProjectileData";

        bool is_expected_name(FNameValue value)
        {
            if (value.ComparisonIndex == 0)
                return false;
            return FName::ToString(value.ComparisonIndex, value.Number) ==
                   kSamProjectileDataName;
        }

        bool same_name(const projectile_subsystem::Instance& projectile)
        {
            // Projectile data assets use the same base FName while some pool builds
            // carry a per-instance Number suffix.  The comparison index is the stable
            // identity needed here; requiring Number would silently drop valid SAM
            // rounds even though their decoded name is the expected asset.
            if (projectile.data != 0 && g_sam_projectile_data.find(projectile.data) != g_sam_projectile_data.end())
                return true;
            return projectile.data_name.ComparisonIndex != 0 &&
                   g_sam_projectile_name.ComparisonIndex != 0 &&
                   projectile.data_name.ComparisonIndex == g_sam_projectile_name.ComparisonIndex;
        }

        bool resolve_sam_name(const projectile_subsystem::Snapshot* pool = nullptr)
        {
            if (g_sam_name_ready)
                return g_sam_projectile_name.ComparisonIndex != 0;
            g_sam_projectile_name = engine_funcs::conv_string_to_name(FString(L"DA_72mm_ProjectileData"));
            // A transient ProcessEvent failure can return a non-zero garbage FName.
            // Validate the decoded value before making it permanent.
            if (is_expected_name(g_sam_projectile_name))
            {
                g_sam_name_ready = true;
                return true;
            }
            g_sam_projectile_name = {};

            // Some menu/loading transitions leave KismetStringLibrary unresolved for
            // a few frames.  Learn the same comparison index directly from a live
            // pool entry instead of disabling the detector for the whole flight.
            if (pool && pool->valid())
            {
                for (std::uint32_t slot = 0; slot < pool->allocated; ++slot)
                {
                    projectile_subsystem::Instance projectile{};
                    if (!projectile_subsystem::read_instance(*pool, slot, projectile) || projectile.data_name.ComparisonIndex == 0)
                        continue;
                    if (is_expected_name(projectile.data_name))
                    {
                        g_sam_projectile_name = projectile.data_name;
                        g_sam_name_ready = true;
                        if (projectile.data != 0)
                            g_sam_projectile_data.insert(projectile.data);
                        return true;
                    }
                }
            }
            return g_sam_projectile_name.ComparisonIndex != 0;
        }

        bool incoming(const projectile_subsystem::Instance& projectile, const FVector& observer, double& distance_meters, double& direct_closing_speed_mps, double& predicted_miss_distance_meters)
        {
            distance_meters = 0.0;
            direct_closing_speed_mps = 0.0;
            predicted_miss_distance_meters = 0.0;
            if (!same_name(projectile) || (projectile.state != kLaunchState && projectile.state != kGuidedState) || !std::isfinite(projectile.flight_time) || projectile.flight_time <= 0.0 || !finite_vector(projectile.location) || !finite_vector(projectile.velocity) || !finite_vector(observer))
                return false;

            const FVector from_observer = projectile.location - observer;
            const double distance = from_observer.Length();
            const double speed_squared =
                projectile.velocity.X * projectile.velocity.X +
                projectile.velocity.Y * projectile.velocity.Y +
                projectile.velocity.Z * projectile.velocity.Z;
            const double speed = std::sqrt(speed_squared);
            if (!std::isfinite(distance) || distance <= 0.0 || distance > kWarningRadiusMeters * 100.0 || !std::isfinite(speed) || speed < kMinimumSpeed || speed > kMaximumSpeed)
                return false;

            const double radial_dot =
                from_observer.X * projectile.velocity.X +
                from_observer.Y * projectile.velocity.Y +
                from_observer.Z * projectile.velocity.Z;
            if (std::isfinite(radial_dot) && distance > 0.0)
            {
                const double closing_speed = -radial_dot / distance;
                if (std::isfinite(closing_speed) && closing_speed > kImmediateClosingSpeed)
                {
                    direct_closing_speed_mps = closing_speed / 100.0;
                }
            }

            // Use the current velocity to estimate the closest point of approach.
            // A projectile that is merely passing 50 m beside the helicopter can still
            // be inside the 100 m trigger radius, but it should not consume flares.
            double time_to_closest = -radial_dot / speed_squared;
            if (!std::isfinite(time_to_closest) || time_to_closest < 0.0)
                time_to_closest = 0.0;
            time_to_closest = (std::min)(time_to_closest, 3.0);
            const FVector closest_point = from_observer + projectile.velocity * time_to_closest;
            predicted_miss_distance_meters = closest_point.Length() / 100.0;
            if (!std::isfinite(predicted_miss_distance_meters))
                return false;

            // Guided rounds can turn sharply between two pool samples.  A radial
            // velocity/closest-point test rejects the Stinger exactly at the state
            // 2 -> 3 transition even though the measured distance is falling.  Keep
            // the candidate gate physical (finite, fast, in range) and let the
            // monotonic distance tracker below establish that it is incoming.
            distance_meters = distance / 100.0;
            return true;
        }

        bool confirm_monotonic_approach(const projectile_subsystem::Instance& projectile, double distance_meters, std::uint64_t now, double& closing_speed_mps)
        {
            closing_speed_mps = 0.0;
            ThreatTrack& track = g_threat_tracks[projectile.address];
            if (track.last_seen_ms == 0 || now - track.last_seen_ms > kTrackContinuityMs || projectile.flight_time < track.flight_time)
            {
                track.distance_meters = distance_meters;
                track.flight_time = projectile.flight_time;
                track.closing_speed_mps = 0.0;
                track.last_seen_ms = now;
                track.consecutive_approach_samples = 1;
                return false;
            }

            const bool distance_progressed =
                distance_meters + kMinimumDistanceDropMeters < track.distance_meters;
            const bool flight_progressed = projectile.flight_time > track.flight_time;
            if (distance_progressed)
            {
                const double elapsed_seconds =
                    static_cast<double>(now - track.last_seen_ms) / 1000.0;
                if (elapsed_seconds > 0.0)
                {
                    const double measured_closing_speed =
                        (track.distance_meters - distance_meters) / elapsed_seconds;
                    if (std::isfinite(measured_closing_speed) && measured_closing_speed > 0.0)
                    {
                        track.closing_speed_mps = track.closing_speed_mps > 0.0
                                                      ? track.closing_speed_mps * 0.35 + measured_closing_speed * 0.65
                                                      : measured_closing_speed;
                    }
                }
                track.consecutive_approach_samples = static_cast<std::uint8_t>((std::min)(static_cast<int>(track.consecutive_approach_samples) + 1, 255));
                track.distance_meters = distance_meters;
                track.flight_time = projectile.flight_time;
                track.last_seen_ms = now;
            }
            else if (flight_progressed || distance_meters > track.distance_meters + 0.25)
            {
                // A fresh sample that does not get closer breaks the approach streak.
                track.distance_meters = distance_meters;
                track.flight_time = projectile.flight_time;
                track.last_seen_ms = now;
                track.consecutive_approach_samples = 1;
            }

            // Identical replicated samples do not reset a valid streak; they simply
            // expire if the server stops advancing the projectile for too long.
            const bool confirmed = track.consecutive_approach_samples >= 2 &&
                                   now - track.last_seen_ms <= kTrackContinuityMs;
            if (confirmed)
                closing_speed_mps = track.closing_speed_mps;
            return confirmed;
        }

        void prune_tracks(std::uint64_t now)
        {
            for (auto it = g_threat_tracks.begin(); it != g_threat_tracks.end();)
            {
                if (now - it->second.last_seen_ms > kTrackRetentionMs)
                    it = g_threat_tracks.erase(it);
                else
                    ++it;
            }
        }

    } // namespace

    Status tick(void* rotary_vehicle, const FVector& observer, bool auto_flare, bool flare_warning)
    {
        if (!rotary_vehicle || (!auto_flare && !flare_warning))
            return {};
        const std::uint64_t now = frame_ticks;
        if (now < g_next_scan_ms)
            return g_last_status;
        g_next_scan_ms = now + kScanIntervalMs;

        Status status{};
        status.flare_sent = now < g_flare_feedback_until_ms;
        const auto preserve_recent_warning = [&]()
        {
            if (g_last_status.incoming && now - g_last_threat_seen_ms <= kWarningHoldMs)
            {
                status.incoming = true;
                status.distance_meters = g_last_status.distance_meters;
                status.predicted_miss_distance_meters =
                    g_last_status.predicted_miss_distance_meters;
            }
            g_last_status = status;
            return g_last_status;
        };
        projectile_subsystem::Snapshot pool{};
        if (!projectile_subsystem::acquire(pool))
        {
            // Keep the last live warning through a single pool/seat refresh gap.
            // Hard yaw and replication can invalidate the pool view for one scan;
            // clearing the snapshot here made the on-screen warning blink out.
            return preserve_recent_warning();
        }
        if (!resolve_sam_name(&pool))
        {
            return preserve_recent_warning();
        }
        // Projectile name tracing was only for Stinger/SAM identification.
        // Keep the helper available for a future probe, but keep runtime logging off.
        std::uintptr_t selected_address = 0;
        double selected_closing_speed_mps = 0.0;
        for (std::uint32_t slot = 0; slot < pool.allocated; ++slot)
        {
            projectile_subsystem::Instance projectile{};
            double distance_meters = 0.0;
            double direct_closing_speed_mps = 0.0;
            double predicted_miss_distance_meters = 0.0;
            if (!projectile_subsystem::read_instance(pool, slot, projectile))
                continue;
            if (projectile.data != 0 && projectile.data_name.ComparisonIndex == g_sam_projectile_name.ComparisonIndex)
                g_sam_projectile_data.insert(projectile.data);
            if (!incoming(projectile, observer, distance_meters, direct_closing_speed_mps, predicted_miss_distance_meters))
                continue;
            double tracked_closing_speed_mps = 0.0;
            const bool monotonic_approach = confirm_monotonic_approach(projectile, distance_meters, now, tracked_closing_speed_mps);
            // The old diagnostic pass happened to provide a second observation.
            // Preserve reliability without the logs: a physically closing sample
            // is immediately actionable, while curved paths still use two samples.
            const bool guided_state_fallback =
                projectile.state == kGuidedState &&
                projectile.external_movement != 0 &&
                distance_meters <= kWarningRadiusMeters &&
                predicted_miss_distance_meters + 5.0 < distance_meters;
            if (direct_closing_speed_mps <= 0.0 && !monotonic_approach && !guided_state_fallback)
            {
                // Guided rounds can report a tangent/outgoing velocity for one
                // sample while the steering update catches up. Keep a confirmed
                // threat latched until three clearly receding samples arrive.
                if (projectile.address == g_latched_threat.address && distance_meters > g_latched_threat.distance_meters + 2.0)
                {
                    g_latched_threat.receding_samples = static_cast<std::uint8_t>((std::min)(static_cast<int>(g_latched_threat.receding_samples) + 1, 255));
                }
                continue;
            }
            if (!status.incoming || distance_meters < status.distance_meters)
            {
                status.incoming = true;
                status.distance_meters = static_cast<float>(distance_meters);
                status.predicted_miss_distance_meters = static_cast<float>(predicted_miss_distance_meters);
                selected_address = projectile.address;
                selected_closing_speed_mps = (std::max)(direct_closing_speed_mps, tracked_closing_speed_mps);
            }
        }
        prune_tracks(now);
        if (g_latched_threat.receding_samples >= 3)
            g_latched_threat = {};

        if (status.incoming)
        {
            g_last_threat_seen_ms = now;
            g_latched_threat.address = selected_address;
            g_latched_threat.distance_meters = status.distance_meters;
            g_latched_threat.closing_speed_mps = selected_closing_speed_mps;
            g_latched_threat.predicted_miss_distance_meters =
                status.predicted_miss_distance_meters;
            g_latched_threat.sampled_ms = now;
            g_latched_threat.receding_samples = 0;
        }
        else if (g_latched_threat.address != 0 && now - g_latched_threat.sampled_ms <= kWarningHoldMs)
        {
            // Active-bit replication can omit a guided round for several scans.
            // Keep its confirmed trajectory alive and advance the displayed/
            // flare distance with the last measured closing speed.
            const double elapsed_seconds =
                static_cast<double>(now - g_latched_threat.sampled_ms) / 1000.0;
            const double projected_distance = (std::max)(1.0, g_latched_threat.distance_meters - g_latched_threat.closing_speed_mps * elapsed_seconds);
            status.incoming = true;
            status.distance_meters = static_cast<float>(projected_distance);
            status.predicted_miss_distance_meters = static_cast<float>(g_latched_threat.predicted_miss_distance_meters);
        }
        else if (g_last_status.incoming && now - g_last_threat_seen_ms <= kWarningHoldMs)
        {
            status.incoming = true;
            status.distance_meters = g_last_status.distance_meters;
            status.predicted_miss_distance_meters =
                g_last_status.predicted_miss_distance_meters;
        }
        else
        {
            g_latched_threat = {};
        }

        if (auto_flare && status.incoming && status.distance_meters <= kFlareRadiusMeters && status.predicted_miss_distance_meters <= kFlareCorridorMeters && now >= g_next_flare_ms && now >= g_flare_wave_until_ms && engine_funcs::input_fire_flares(rotary_vehicle))
        {
            g_next_flare_ms = now + kFlareCooldownMs;
            g_flare_wave_until_ms = now + kFlareWaveHoldMs;
            g_flare_feedback_until_ms = now + kFlareFeedbackMs;
            status.flare_sent = true;
        }
        g_last_status = status;
        return g_last_status;
    }

    void reset()
    {
        g_sam_projectile_name = {};
        g_sam_name_ready = false;
        g_sam_projectile_data.clear();
        g_last_status = {};
        g_next_scan_ms = 0;
        g_next_flare_ms = 0;
        g_flare_wave_until_ms = 0;
        g_flare_feedback_until_ms = 0;
        g_last_threat_seen_ms = 0;
        g_threat_tracks.clear();
        g_latched_threat = {};
    }

} // namespace wdgs::anti_sam
