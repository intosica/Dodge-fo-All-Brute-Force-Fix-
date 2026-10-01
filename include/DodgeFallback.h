#pragma once

// ============================================================================
// DodgeFallback - "brute force" safety net for the player dodge.
//
// Problem it solves: the plugin fires the graph event (BF_RollDodgeStart etc.)
// and asks ApplyImpulse.dll to move the actor, but if EITHER of those silently
// does nothing (behavior patch not applied, event unknown to the graph, API DLL
// not loaded / export not found, root motion missing from the clip, etc.) the
// player just loses stamina and nothing happens.
//
// This module:
//   1. Logs exactly which layer failed (check DodgeAll.log).
//   2. Tries fallback graph events if the primary one is rejected.
//   3. Moves the player itself (per-frame position stepping) when no other
//      movement source produced displacement.
// All behaviour is tunable in Data/SKSE/Plugins/DodgeAll/Fallback.json
// ============================================================================

namespace DodgeFallback {

    enum class Kind { Roll, Step, Dash };

    // Call once on kDataLoaded: loads config, (re)acquires APIs, dumps environment info.
    void Init();

    // Retry acquiring the ApplyImpulse (FFC) and InputManager APIs. Cheap; safe to call often.
    void EnsureAPIs();

    // Sends the primary graph event. If the graph rejects it, tries the configured
    // fallback events. Returns true if ANY event was accepted by the graph.
    bool SendAnimation(RE::Actor* actor, Kind kind, const char* primaryEvent);

    // Call right after the dodge was started (animation event sent + impulse requested).
    // Decides whether scripted movement is needed and starts it (now or via watchdog).
    //   animAccepted  - result of SendAnimation
    //   impulseIssued - true if ApplyImpulse (FFC) was actually asked to move the actor
    void Supervise(RE::Actor* actor, Kind kind, bool animAccepted, bool impulseIssued);

    // Stops any running scripted movement / pending watchdog (e.g. movement-cancel).
    void CancelScripted();

    // One-shot diagnostic dump for the first few dodges (graph variables, API state).
    void ProbeOnce(RE::Actor* actor);
}
