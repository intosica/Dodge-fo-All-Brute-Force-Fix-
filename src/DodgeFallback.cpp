#include "DodgeFallback.h"
#include "FFC_API.h"
#include "InputManagerAPI.h"
#include "DelayedDispatcher.h"

#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace DodgeFallback {
    namespace {
        constexpr const char* kConfigDir = "Data/SKSE/Plugins/DodgeAll";
        constexpr const char* kConfigPath = "Data/SKSE/Plugins/DodgeAll/Fallback.json";

        struct Config {
            bool enabled = true;
            // Ignore every other movement source and always move the player ourselves.
            // Only use this if your dodge clips have NO root motion and ApplyImpulse is not working,
            // otherwise you will get double movement.
            bool forceScriptedMovement = false;

            // Scripted movement tuning (game units / seconds). ~70 units = 1 metre.
            float rollDistance = 220.0f;
            float rollDuration = 0.40f;
            float stepDistance = 130.0f;
            float stepDuration = 0.28f;
            float dashDistance = 320.0f;
            float dashDuration = 0.35f;

            // Watchdog: if the player moved less than minDistance this many ms after the dodge
            // started, assume animation/impulse produced no motion and take over.
            bool watchdogEnabled = true;
            int watchdogMs = 180;
            float watchdogMinDistance = 12.0f;

            // Tried in order when the primary graph event is rejected by the behavior graph.
            std::vector<std::string> fallbackEvents{ "SneakSprintStartRoll" };

            int probeCount = 6;  // how many dodges get the verbose graph probe
        };

        Config g_cfg;
        std::atomic<std::uint64_t> g_jobId{ 0 };
        std::atomic<std::uint64_t> g_dodgeSeq{ 0 };
        std::atomic<int> g_probeBudget{ 6 };
        bool g_ffcMessageSent = false;

        const char* KindName(Kind k) {
            switch (k) {
            case Kind::Roll: return "Roll";
            case Kind::Step: return "Step";
            default: return "Dash";
            }
        }

        float KindDistance(Kind k) {
            switch (k) {
            case Kind::Roll: return g_cfg.rollDistance;
            case Kind::Step: return g_cfg.stepDistance;
            default: return g_cfg.dashDistance;
            }
        }

        float KindDuration(Kind k) {
            switch (k) {
            case Kind::Roll: return g_cfg.rollDuration;
            case Kind::Step: return g_cfg.stepDuration;
            default: return g_cfg.dashDuration;
            }
        }

        // ------------------------------------------------------------------
        // Config
        // ------------------------------------------------------------------
        void WriteDefaultConfig() {
            std::error_code ec;
            std::filesystem::create_directories(kConfigDir, ec);
            std::ofstream ofs(kConfigPath);
            if (!ofs.is_open()) {
                logger::warn("[DodgeFallback] could not write default config to {}", kConfigPath);
                return;
            }
            ofs << R"({
  "enabled": true,
  "forceScriptedMovement": false,
  "rollDistance": 220.0,
  "rollDuration": 0.40,
  "stepDistance": 130.0,
  "stepDuration": 0.28,
  "dashDistance": 320.0,
  "dashDuration": 0.35,
  "watchdogEnabled": true,
  "watchdogMs": 180,
  "watchdogMinDistance": 12.0,
  "fallbackEvents": [ "SneakSprintStartRoll" ],
  "probeCount": 6
}
)";
        }

        void LoadConfig() {
            g_cfg = Config{};
            std::ifstream ifs(kConfigPath);
            if (!ifs.is_open()) {
                logger::info("[DodgeFallback] no {} found, writing defaults", kConfigPath);
                WriteDefaultConfig();
                g_probeBudget.store(g_cfg.probeCount);
                return;
            }

            rapidjson::IStreamWrapper isw(ifs);
            rapidjson::Document doc;
            doc.ParseStream(isw);
            if (doc.HasParseError() || !doc.IsObject()) {
                logger::warn("[DodgeFallback] {} is not valid JSON, using defaults", kConfigPath);
                g_probeBudget.store(g_cfg.probeCount);
                return;
            }

            auto getF = [&](const char* key, float& out) {
                if (doc.HasMember(key) && doc[key].IsNumber()) out = doc[key].GetFloat();
            };
            auto getI = [&](const char* key, int& out) {
                if (doc.HasMember(key) && doc[key].IsNumber()) out = doc[key].GetInt();
            };
            auto getB = [&](const char* key, bool& out) {
                if (doc.HasMember(key) && doc[key].IsBool()) out = doc[key].GetBool();
            };

            getB("enabled", g_cfg.enabled);
            getB("forceScriptedMovement", g_cfg.forceScriptedMovement);
            getF("rollDistance", g_cfg.rollDistance);
            getF("rollDuration", g_cfg.rollDuration);
            getF("stepDistance", g_cfg.stepDistance);
            getF("stepDuration", g_cfg.stepDuration);
            getF("dashDistance", g_cfg.dashDistance);
            getF("dashDuration", g_cfg.dashDuration);
            getB("watchdogEnabled", g_cfg.watchdogEnabled);
            getI("watchdogMs", g_cfg.watchdogMs);
            getF("watchdogMinDistance", g_cfg.watchdogMinDistance);
            getI("probeCount", g_cfg.probeCount);

            if (doc.HasMember("fallbackEvents") && doc["fallbackEvents"].IsArray()) {
                g_cfg.fallbackEvents.clear();
                for (auto& v : doc["fallbackEvents"].GetArray()) {
                    if (v.IsString()) g_cfg.fallbackEvents.emplace_back(v.GetString());
                }
            }

            // Sanity clamps so a typo can't launch the player into orbit.
            g_cfg.rollDuration = std::clamp(g_cfg.rollDuration, 0.05f, 2.0f);
            g_cfg.stepDuration = std::clamp(g_cfg.stepDuration, 0.05f, 2.0f);
            g_cfg.dashDuration = std::clamp(g_cfg.dashDuration, 0.05f, 2.0f);
            g_cfg.rollDistance = std::clamp(g_cfg.rollDistance, 0.0f, 1200.0f);
            g_cfg.stepDistance = std::clamp(g_cfg.stepDistance, 0.0f, 1200.0f);
            g_cfg.dashDistance = std::clamp(g_cfg.dashDistance, 0.0f, 1200.0f);
            g_cfg.watchdogMs = std::clamp(g_cfg.watchdogMs, 50, 1500);
            g_probeBudget.store(g_cfg.probeCount);
        }

        // ------------------------------------------------------------------
        // Direction helpers
        // ------------------------------------------------------------------

        // 0 = neutral, 1 = F, 2 = FR, 3 = R, 4 = BR, 5 = B, 6 = BL, 7 = L, 8 = FL
        // (same numbering the rest of the mod uses for DirecionalCycleMoveset)
        int DirectionFromInput() {
            const auto* controls = RE::PlayerControls::GetSingleton();
            if (!controls) return 0;
            const auto& v = controls->data.moveInputVec;  // x = right, y = forward
            if (v.x * v.x + v.y * v.y < 0.04f) return 0;
            float deg = std::atan2(v.x, v.y) * 57.29578f;  // clockwise from forward
            if (deg < 0.0f) deg += 360.0f;
            const int sector = static_cast<int>(std::floor((deg + 22.5f) / 45.0f)) % 8;
            return sector + 1;
        }

        // Accepts either spelling of the graph variable: the plugin uses the misspelt
        // "DirecionalCycleMoveset"; a Pandora patch may have used the correct spelling.
        int DirectionFromGraph(RE::Actor* actor) {
            std::int32_t v = 0;
            if (actor->GetGraphVariableInt("DirecionalCycleMoveset", v) && v > 0) return v;
            v = 0;
            if (actor->GetGraphVariableInt("DirectionalCycleMoveset", v) && v > 0) return v;
            return 0;
        }

        void LocalVector(int dir, Kind kind, float& right, float& forward) {
            constexpr float k = 0.70710678f;
            switch (dir) {
            case 1: right = 0.0f;  forward = 1.0f;  break;
            case 2: right = k;     forward = k;     break;
            case 3: right = 1.0f;  forward = 0.0f;  break;
            case 4: right = k;     forward = -k;    break;
            case 5: right = 0.0f;  forward = -1.0f; break;
            case 6: right = -k;    forward = -k;    break;
            case 7: right = -1.0f; forward = 0.0f;  break;
            case 8: right = -k;    forward = k;     break;
            default:  // neutral: step back, roll/dash forward
                right = 0.0f;
                forward = (kind == Kind::Step) ? -1.0f : 1.0f;
                break;
            }
        }

        // ------------------------------------------------------------------
        // Scripted movement
        // ------------------------------------------------------------------
        struct Job {
            std::uint64_t id = 0;
            float dirX = 0.0f;
            float dirY = 0.0f;
            float distance = 0.0f;
            float duration = 0.3f;
            float prevOffset = 0.0f;
            std::chrono::steady_clock::time_point t0;
        };

        float EaseOut(float u) { return 1.0f - (1.0f - u) * (1.0f - u); }

        void Tick(const std::shared_ptr<Job>& job);

        void ScheduleTick(const std::shared_ptr<Job>& job) {
            // ~125 Hz upper bound; the actual step uses wall-clock time so frame rate doesn't matter.
            Utils::DelayedDispatcher::Get().PostDelayed(std::chrono::milliseconds(8), [job]() {
                if (const auto* tasks = SKSE::GetTaskInterface()) {
                    tasks->AddTask([job]() { Tick(job); });
                }
            });
        }

        void Tick(const std::shared_ptr<Job>& job) {
            if (job->id != g_jobId.load()) return;  // superseded or cancelled

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player || !player->Is3DLoaded() || player->IsDead() || player->IsInKillMove() ||
                player->IsInRagdollState()) {
                logger::info("[DodgeFallback] scripted move aborted (player unavailable/dead/ragdoll)");
                return;
            }

            const float elapsed =
                std::chrono::duration<float>(std::chrono::steady_clock::now() - job->t0).count();
            const float u = std::clamp(elapsed / job->duration, 0.0f, 1.0f);
            const float offset = job->distance * EaseOut(u);
            const float step = offset - job->prevOffset;
            job->prevOffset = offset;

            if (step > 0.001f) {
                auto pos = player->GetPosition();
                pos.x += job->dirX * step;
                pos.y += job->dirY * step;
                player->SetPosition(pos, true);
            }

            if (u < 1.0f) {
                ScheduleTick(job);
            } else {
                logger::info("[DodgeFallback] scripted move finished ({:.0f} units)", job->distance);
            }
        }

        void StartScripted(Kind kind, float fraction, const char* reason) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player || !player->Is3DLoaded()) return;

            int dir = DirectionFromInput();
            if (dir == 0) dir = DirectionFromGraph(player);

            float right = 0.0f;
            float forward = 0.0f;
            LocalVector(dir, kind, right, forward);

            const float yaw = player->GetAngleZ();
            const float sinY = std::sin(yaw);
            const float cosY = std::cos(yaw);

            auto job = std::make_shared<Job>();
            job->id = ++g_jobId;
            // Skyrim yaw is clockwise from +Y: forward = (sin, cos), right = (cos, -sin)
            job->dirX = right * cosY + forward * sinY;
            job->dirY = -right * sinY + forward * cosY;
            fraction = std::clamp(fraction, 0.2f, 1.0f);
            job->distance = KindDistance(kind) * fraction;
            job->duration = std::max(0.05f, KindDuration(kind) * fraction);
            job->t0 = std::chrono::steady_clock::now();

            logger::info("[DodgeFallback] SCRIPTED {} move: dir={} dist={:.0f} dur={:.2f}s reason: {}",
                KindName(kind), dir, job->distance, job->duration, reason);

            ScheduleTick(job);
        }

        void LogApiState() {
            HMODULE ffc = GetModuleHandleW(L"ApplyImpulse.dll");
            logger::info("[DodgeFallback] ApplyImpulse.dll loaded={} export(GetFFCAPI)={} api={}",
                ffc != nullptr, ffc && GetProcAddress(ffc, "GetFFCAPI") != nullptr, FFC_API::_API != nullptr);

            HMODULE im = GetModuleHandleW(L"InputManager.dll");
            logger::info("[DodgeFallback] InputManager.dll loaded={} export(GetInputManagerAPI)={} api={}",
                im != nullptr, im && GetProcAddress(im, "GetInputManagerAPI") != nullptr,
                InputManagerAPI::_API != nullptr);
        }
    }  // namespace

    // ----------------------------------------------------------------------
    // Public API
    // ----------------------------------------------------------------------
    void EnsureAPIs() {
        if (!FFC_API::_API) {
            if (!FFC_API::RequestAPIDirect() && !g_ffcMessageSent) {
                // asynchronous path: ApplyImpulse answers with kMessage_ProvideAPI, handled in plugin.cpp
                FFC_API::RequestAPI();
                g_ffcMessageSent = true;
            }
        }
        if (!InputManagerAPI::_API) {
            InputManagerAPI::RequestAPIDirect();
        }
    }

    void Init() {
        LoadConfig();
        logger::info("[DodgeFallback] enabled={} forceScripted={} watchdog={}({}ms, min {:.0f}u) fallbackEvents={}",
            g_cfg.enabled, g_cfg.forceScriptedMovement, g_cfg.watchdogEnabled, g_cfg.watchdogMs,
            g_cfg.watchdogMinDistance, g_cfg.fallbackEvents.size());
        EnsureAPIs();
        LogApiState();
    }

    void ProbeOnce(RE::Actor* actor) {
        if (!actor) return;
        if (g_probeBudget.fetch_sub(1) <= 0) return;

        bool b = false;
        std::int32_t i = 0;
        logger::info("[DodgeFallback][probe] graph var 'isDodgingCMF' exists={}  (false => behavior patch NOT loaded in player graph)",
            actor->GetGraphVariableBool("isDodgingCMF", b));
        i = -1;
        const bool e1 = actor->GetGraphVariableInt("DirecionalCycleMoveset", i);
        logger::info("[DodgeFallback][probe] graph var 'DirecionalCycleMoveset' exists={} value={}", e1, i);
        i = -1;
        const bool e2 = actor->GetGraphVariableInt("DirectionalCycleMoveset", i);
        logger::info("[DodgeFallback][probe] graph var 'DirectionalCycleMoveset' exists={} value={}", e2, i);
        LogApiState();
    }

    bool SendAnimation(RE::Actor* actor, Kind kind, const char* primaryEvent) {
        if (!actor) return false;

        EnsureAPIs();
        ProbeOnce(actor);

        const bool ok = actor->NotifyAnimationGraph(primaryEvent);
        logger::info("[DodgeFallback] {} NotifyAnimationGraph('{}') accepted={}", KindName(kind), primaryEvent, ok);
        if (ok) return true;

        if (!g_cfg.enabled) return false;

        logger::warn("[DodgeFallback] graph REJECTED '{}': the event is not in the player's behavior graph. "
                     "The Pandora patch that adds it is not active in the game's output.", primaryEvent);

        for (const auto& ev : g_cfg.fallbackEvents) {
            if (ev.empty()) continue;
            const bool ok2 = actor->NotifyAnimationGraph(ev.c_str());
            logger::info("[DodgeFallback] fallback event '{}' accepted={}", ev, ok2);
            if (ok2) return true;
        }
        return false;
    }

    void CancelScripted() {
        ++g_jobId;
        ++g_dodgeSeq;
    }

    void Supervise(RE::Actor* actor, Kind kind, bool animAccepted, bool impulseIssued) {
        if (!g_cfg.enabled || !actor || !actor->IsPlayerRef()) return;

        const std::uint64_t seq = ++g_dodgeSeq;
        ++g_jobId;  // a new dodge always supersedes a running scripted move

        if (g_cfg.forceScriptedMovement) {
            StartScripted(kind, 1.0f, "forceScriptedMovement=true");
            return;
        }

        const bool expectMotion = animAccepted || impulseIssued;
        if (!expectMotion) {
            StartScripted(kind, 1.0f, "no animation accepted and no impulse issued");
            return;
        }

        if (!g_cfg.watchdogEnabled) return;

        const auto startPos = actor->GetPosition();
        const float startX = startPos.x;
        const float startY = startPos.y;
        const int waitMs = g_cfg.watchdogMs;
        const float minDist = g_cfg.watchdogMinDistance;

        Utils::DelayedDispatcher::Get().PostDelayed(std::chrono::milliseconds(waitMs),
            [seq, kind, startX, startY, waitMs, minDist]() {
                const auto* tasks = SKSE::GetTaskInterface();
                if (!tasks) return;
                tasks->AddTask([seq, kind, startX, startY, waitMs, minDist]() {
                    if (seq != g_dodgeSeq.load()) return;  // newer dodge / cancelled

                    auto* player = RE::PlayerCharacter::GetSingleton();
                    if (!player || !player->Is3DLoaded() || player->IsDead()) return;

                    const auto pos = player->GetPosition();
                    const float dx = pos.x - startX;
                    const float dy = pos.y - startY;
                    const float moved = std::sqrt(dx * dx + dy * dy);

                    if (moved >= minDist) {
                        logger::info("[DodgeFallback] watchdog: {} moved {:.0f} units, native movement OK",
                            KindName(kind), moved);
                        return;
                    }

                    const float spent = (waitMs / 1000.0f) / std::max(0.05f, KindDuration(kind));
                    logger::warn("[DodgeFallback] watchdog: only {:.1f} units after {}ms - animation/impulse produced no motion, taking over",
                        moved, waitMs);
                    StartScripted(kind, 1.0f - spent, "watchdog: no displacement");
                });
            });
    }
}  // namespace DodgeFallback
