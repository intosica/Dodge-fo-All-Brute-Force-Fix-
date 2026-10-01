#include "Events.h"
#include "Settings.h"
#include "DelayedDispatcher.h"
#include "Manager.h"
#include "FFC_API.h"
#include "Serialization.h"
#include "DodgeFallback.h"


namespace NPCDodgeManager {
	const Settings::NPCDodgeRule* GetRuleForNPC(RE::Actor* npc);
	void AttemptDashGapClose(RE::Actor* npc, RE::Actor* target, const Settings::NPCDodgeRule* rule);
	bool TryRandomDodgeFromEvent(RE::Actor* attacker, std::string_view eventName);
	bool IsNPCDodgeGloballyAllowed(RE::Actor* a_actor);
	void ClearNPCDodgeState(RE::Actor* a_actor);
}

bool ProcessAndExecuteDodgeImpulse(RE::Actor* actor, int movementDir, const Settings::DistanceSettings& config, bool legacyUpwardEnabled = false, float legacyUpwardBoost = 0.0f) {
	if (!actor || config.mode == 0) return false; // Modo 0 = Animation (Usa o Root Motion nativo da engine)

	float localX = 0.0f;
	float localY = 0.0f;
	float localZ = 0.0f;

	if (config.mode == 1) { // Modo 1 = Simple Mode
		switch (movementDir) {
		case 1: localY = config.simpleForwardBack; break; // Forward
		case 2: localX = config.simpleLeftRight;  localY = config.simpleForwardBack;  break; // Forward-Right
		case 3: localX = config.simpleLeftRight;  break; // Right
		case 4: localX = config.simpleLeftRight;  localY = -config.simpleForwardBack; break; // Backward-Right
		case 5: localY = -config.simpleForwardBack; break; // Backward
		case 6: localX = -config.simpleLeftRight; localY = -config.simpleForwardBack; break; // Backward-Left
		case 7: localX = -config.simpleLeftRight; break; // Left
		case 8: localX = -config.simpleLeftRight; localY = config.simpleForwardBack;  break; // Forward-Left
		default: break; // Neutral
		}
		if (legacyUpwardEnabled && movementDir == 0) {
			localZ = legacyUpwardBoost;
		}
	}
	else if (config.mode == 2) { // Modo 2 = Complex Mode
		if (movementDir >= 0 && movementDir <= 8) {
			localX = config.complexX[movementDir];
			localY = config.complexY[movementDir];
			localZ = config.complexZ[movementDir];
		}
	}

	// Invocação corrigida através da interface vtable da DLL externa (FFC_API)
	if (FFC_API::_API) {
		FFC_API::_API->ApplyCustomVelocityImpulse(actor, localX, localY, localZ, config.duration,false);
		return true;
	}
	else {
		SKSE::log::warn("[DodgeMod] FFC_API não inicializada! Pulando impulso físico.");
		return false;
	}
}

namespace VisionTracker {
	std::shared_mutex trackerMutex;

	// Mapeia: TargetFormID -> Map<WatcherFormID, Depth>
	std::unordered_map<RE::FormID, std::unordered_map<RE::FormID, int>> visionObserversOf;
	std::unordered_map<RE::FormID, std::unordered_map<RE::FormID, int>> areaObserversOf;

	void AddTracker(bool isVision, RE::FormID watcher, RE::FormID target, int depth) {
		std::unique_lock lock(trackerMutex);
		auto& tracker = isVision ? visionObserversOf : areaObserversOf;
		tracker[target][watcher] = depth;
	}

	void RemoveTracker(bool isVision, RE::FormID watcher, RE::FormID target) {
		std::unique_lock lock(trackerMutex);
		auto& tracker = isVision ? visionObserversOf : areaObserversOf;
		tracker[target].erase(watcher);

		if (tracker[target].empty()) {
			tracker.erase(target);
		}
	}

	// Retorna uma lista de observadores que estejam dentro do limite de Depth exigido
	std::vector<RE::FormID> GetValidObservers(bool isVision, RE::FormID target, int maxDepth) {
		std::shared_lock lock(trackerMutex);
		std::vector<RE::FormID> result;
		const auto& tracker = isVision ? visionObserversOf : areaObserversOf;

		auto it = tracker.find(target);
		if (it != tracker.end()) {
			for (const auto& [watcher, depth] : it->second) {
				if (depth <= maxDepth) {
					result.push_back(watcher);
				}
			}
		}
		return result;
	}

	int GetObserverDepth(bool isVision, RE::FormID watcher, RE::FormID target) {
		std::shared_lock lock(trackerMutex);
		const auto& tracker = isVision ? visionObserversOf : areaObserversOf;
		auto it = tracker.find(target);
		if (it != tracker.end()) {
			auto wIt = it->second.find(watcher);
			if (wIt != it->second.end()) return wIt->second;
		}
		return -1;
	}
}

namespace TimeStop {
	std::atomic<uint32_t> g_TimeStopSessionID{ 0 };

	void ResetSession() {
		g_TimeStopSessionID++;
	}


}

float ResolveGlobalFloat(RE::FormID globalID, float fallback) {
	if (globalID == 0) return fallback;
	auto global = RE::TESForm::LookupByID<RE::TESGlobal>(globalID);
	return global ? global->value : fallback;
}

float ResolveActorValueFloat(RE::Actor* actor, const std::string& actorValueName, float fallback) {
	if (!actor || actorValueName.empty()) return fallback;
	const auto actorValue = RE::ActorValueList::LookupActorValueByName(actorValueName.c_str());
	if (actorValue == RE::ActorValue::kNone) return fallback;
	if (auto owner = actor->AsActorValueOwner()) {
		return owner->GetActorValue(actorValue);
	}
	return fallback;
}

float ResolveDodgeValue(RE::Actor* actor, float fixedValue, Settings::DodgeValueSource source, const std::string& actorValueName, RE::FormID globalID) {
	switch (source) {
	case Settings::DodgeValueSource::kActorValue:
		return ResolveActorValueFloat(actor, actorValueName, fixedValue);
	case Settings::DodgeValueSource::kGlobal:
		return ResolveGlobalFloat(globalID, fixedValue);
	case Settings::DodgeValueSource::kFixed:
	default:
		return fixedValue;
	}
}

namespace DodgeTimerManager {
	enum class EvasionKind {
		Roll,
		Step,
		Dash
	};

	struct EvasionWindow {
		std::chrono::steady_clock::time_point startTime;
		EvasionKind kind = EvasionKind::Roll;
		bool perfectCounted = false;
		bool playerMovementCancellation = false;
		bool movementUnlocked = false;
		bool movementStopRequested = false;
	};

	std::unordered_map<RE::FormID, EvasionWindow> g_dodgeWindows;
	std::shared_mutex g_dodgeMutex;
	std::atomic_bool g_playerHasMovementIntent{ false };

	const Settings::DodgeTypeSettings& GetSettingsForKind(RE::Actor* actor, EvasionKind kind) {
		if (actor && !actor->IsPlayerRef()) {
			if (auto rule = NPCDodgeManager::GetRuleForNPC(actor)) {
				if (kind == EvasionKind::Roll) return rule->rollSettings;
				if (kind == EvasionKind::Step) return rule->stepSettings;
				return rule->dashSettings;
			}
			if (kind == EvasionKind::Roll) return Settings::NPCRollSettings;
			if (kind == EvasionKind::Step) return Settings::NPCStepSettings;
			return Settings::NPCDashSettings;
		}

		if (kind == EvasionKind::Roll) return Settings::PlayerRollSettings;
		if (kind == EvasionKind::Step) return Settings::PlayerStepSettings;
		return Settings::PlayerDashSettings;
	}

	EvasionKind GetActiveKind(RE::FormID a_formID, EvasionKind fallback = EvasionKind::Roll) {
		std::shared_lock lock(g_dodgeMutex);
		auto it = g_dodgeWindows.find(a_formID);
		return it != g_dodgeWindows.end() ? it->second.kind : fallback;
	}

	int GetActiveInvulnerabilityMethod(RE::Actor* actor) {
		if (!actor) return 0;
		return GetSettingsForKind(actor, GetActiveKind(actor->GetFormID())).invulnerabilityMethod;
	}

	bool CanCancelActiveDodgeWithAttack(RE::Actor* actor, bool isPowerAttack) {
		if (!actor) return false;
		const auto& settings = GetSettingsForKind(actor, GetActiveKind(actor->GetFormID()));
		bool cancelEnabled = isPowerAttack ? settings.cancelDodgeWithPowerAttacks : settings.cancelDodgeWithAttacks;
		if (actor->IsPlayerRef()) {
			RE::FormID perkID = isPowerAttack ? Settings::PerkCancelDodgeWithPowerAttacks : Settings::PerkCancelDodgeWithAttacks;
			if (cancelEnabled && perkID != 0) {
				auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(perkID);
				if (!perk || !actor->HasPerk(perk)) cancelEnabled = false;
			}
		}
		return cancelEnabled;
	}

	bool TryCancelPlayerDodgeFromMovement() {
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) return false;

		{
			std::unique_lock lock(g_dodgeMutex);
			auto it = g_dodgeWindows.find(player->GetFormID());
			if (it == g_dodgeWindows.end() || !it->second.playerMovementCancellation ||
				!it->second.movementUnlocked || it->second.movementStopRequested) {
				return false;
			}

			it->second.movementStopRequested = true;
			g_dodgeWindows.erase(it);
		}

		DodgeFallback::CancelScripted();
		player->SetGraphVariableBool("isDodgingCMF", false);
		player->SetGraphVariableBool("isPerfDodgingCMF", false);
		player->NotifyAnimationGraph("BF_DodgeStop");
		return true;
	}

	void UpdatePlayerMovementIntent(bool a_hasMovement) {
		g_playerHasMovementIntent.store(a_hasMovement);
		if (a_hasMovement) {
			TryCancelPlayerDodgeFromMovement();
		}
	}

	void FinishPlayerEvasion(RE::Actor* a_actor) {
		if (!a_actor || !a_actor->IsPlayerRef()) return;

		{
			std::unique_lock lock(g_dodgeMutex);
			g_dodgeWindows.erase(a_actor->GetFormID());
		}
		a_actor->SetGraphVariableBool("isDodgingCMF", false);
		a_actor->SetGraphVariableBool("isPerfDodgingCMF", false);
	}

	void StartEvasion(RE::FormID a_formID, EvasionKind a_kind) {
		if (auto actor = RE::TESForm::LookupByID<RE::Actor>(a_formID)) {
			bool isPlayer = actor->IsPlayerRef();
			if (!isPlayer && !NPCDodgeManager::IsNPCDodgeGloballyAllowed(actor)) {
				NPCDodgeManager::ClearNPCDodgeState(actor);
				return;
			}

			const auto& settings = GetSettingsForKind(actor, a_kind);
			const bool movementCancellationEnabled = isPlayer && settings.cancelDodgeWithMovement;
			const bool movementUsesCustomTime = settings.movementLockMethod == 0;
			const float movementLockDuration = movementCancellationEnabled && movementUsesCustomTime
				? std::max(0.0f, ResolveDodgeValue(actor, settings.movementLockDuration, settings.movementLockDurationSource, settings.movementLockDurationActorValue, settings.movementLockDurationGlobal))
				: 0.0f;

			auto startTime = std::chrono::steady_clock::now();
			{
				std::unique_lock lock(g_dodgeMutex);
				g_dodgeWindows[a_formID] = {
					startTime,
					a_kind,
					false,
					movementCancellationEnabled,
					movementCancellationEnabled && movementUsesCustomTime && movementLockDuration <= 0.0f,
					false
				};
			}

			bool invulnerabilityEnabled = settings.invulnerabilityEnabled;
			bool perfectEnabled = settings.enablePerfectDodge;
			int method = settings.invulnerabilityMethod;
			float customDuration = ResolveDodgeValue(actor, settings.invincibilityDuration, settings.invincibilityDurationSource, settings.invincibilityDurationActorValue, settings.invincibilityDurationGlobal);
			float perfectDuration = ResolveDodgeValue(actor, settings.perfectDodgeWindow, settings.perfectDodgeWindowSource, settings.perfectDodgeWindowActorValue, settings.perfectDodgeWindowGlobal);

			actor->SetGraphVariableBool("isPerfDodgingCMF", perfectEnabled);
			actor->SetGraphVariableBool("isDodgingCMF", invulnerabilityEnabled);

			auto actorHandle = actor->CreateRefHandle();

			if (movementCancellationEnabled && movementUsesCustomTime) {
				if (movementLockDuration <= 0.0f) {
					if (g_playerHasMovementIntent.load()) {
						TryCancelPlayerDodgeFromMovement();
					}
				}
				else {
					Utils::DelayedDispatcher::Get().PostDelayed(std::chrono::milliseconds(static_cast<int>(movementLockDuration * 1000)), [actorHandle, a_formID, startTime]() {
						SKSE::GetTaskInterface()->AddTask([actorHandle, a_formID, startTime]() {
							{
								std::unique_lock lock(g_dodgeMutex);
								auto it = g_dodgeWindows.find(a_formID);
								if (it == g_dodgeWindows.end() || it->second.startTime != startTime || !it->second.playerMovementCancellation) return;
								it->second.movementUnlocked = true;
							}
							if (actorHandle && actorHandle.get() && g_playerHasMovementIntent.load()) {
								TryCancelPlayerDodgeFromMovement();
							}
							});
						});
				}
			}

			if (perfectEnabled) {
				Utils::DelayedDispatcher::Get().PostDelayed(std::chrono::milliseconds(static_cast<int>(perfectDuration * 1000)), [actorHandle, a_formID, startTime]() {
					SKSE::GetTaskInterface()->AddTask([actorHandle, a_formID, startTime]() {
						{
							std::shared_lock lock(g_dodgeMutex);
							auto it = g_dodgeWindows.find(a_formID);
							if (it != g_dodgeWindows.end() && it->second.startTime != startTime) {
								return; // Uma esquiva mais nova sobrescreveu esta janela
							}
						}
						if (actorHandle && actorHandle.get()) {
							actorHandle.get()->SetGraphVariableBool("isPerfDodgingCMF", false);
						}
						});
					});
			}

			if (invulnerabilityEnabled && method == 0) {
				Utils::DelayedDispatcher::Get().PostDelayed(std::chrono::milliseconds(static_cast<int>(customDuration * 1000)), [actorHandle, a_formID, startTime]() {
					SKSE::GetTaskInterface()->AddTask([actorHandle, a_formID, startTime]() {
						{
							std::shared_lock lock(g_dodgeMutex);
							auto it = g_dodgeWindows.find(a_formID);
							if (it != g_dodgeWindows.end() && it->second.startTime != startTime) {
								return;
							}
						}
						if (actorHandle && actorHandle.get()) {
							actorHandle.get()->SetGraphVariableBool("isDodgingCMF", false);
						}
						});
					});
			}

			// Limpeza de segurança do mapa de janelas baseada no tempo máximo de vida útil
			float timeStopDuration = settings.stopTimeOnPerfectDodge ? ResolveDodgeValue(actor, settings.timeStopDuration, settings.timeStopDurationSource, settings.timeStopDurationActorValue, settings.timeStopDurationGlobal) : 0.0f;
			float maxLifetime = std::max({ perfectEnabled ? perfectDuration : 0.0f, (invulnerabilityEnabled && method == 0 ? customDuration : 0.0f), timeStopDuration });
			if (a_kind == EvasionKind::Dash) {
				maxLifetime = std::max(maxLifetime, 3.0f);
			}
			if (movementCancellationEnabled) {
				maxLifetime = std::max(maxLifetime, 10.0f);
			}
			Utils::DelayedDispatcher::Get().PostDelayed(std::chrono::milliseconds(static_cast<int>(maxLifetime * 1000)), [actorHandle, a_formID, startTime, movementCancellationEnabled]() {
				SKSE::GetTaskInterface()->AddTask([actorHandle, a_formID, startTime, movementCancellationEnabled]() {
					bool removedExpiredWindow = false;
					{
						std::unique_lock lock(g_dodgeMutex);
						auto it = g_dodgeWindows.find(a_formID);
						if (it != g_dodgeWindows.end() && it->second.startTime == startTime) {
							g_dodgeWindows.erase(it);
							removedExpiredWindow = true;
						}
					}
					if (removedExpiredWindow && movementCancellationEnabled && actorHandle && actorHandle.get()) {
						actorHandle.get()->SetGraphVariableBool("isDodgingCMF", false);
						actorHandle.get()->SetGraphVariableBool("isPerfDodgingCMF", false);
					}
					});
				});
		}
	}

	void StartDodge(RE::FormID a_formID, bool a_isRoll) {
		StartEvasion(a_formID, a_isRoll ? EvasionKind::Roll : EvasionKind::Step);
	}

	void StartDash(RE::FormID a_formID) {
		StartEvasion(a_formID, EvasionKind::Dash);
	}

	DodgeState GetDodgeState(RE::FormID a_formID) {
		auto actor = RE::TESForm::LookupByID<RE::Actor>(a_formID);
		if (!actor) return DodgeState::None;
		if (!actor->IsPlayerRef() && !NPCDodgeManager::IsNPCDodgeGloballyAllowed(actor)) {
			NPCDodgeManager::ClearNPCDodgeState(actor);
			return DodgeState::None;
		}

		std::chrono::steady_clock::time_point startTime;
		bool hasTimer = false;
		EvasionKind activeKind = EvasionKind::Roll;

		{
			std::shared_lock lock(g_dodgeMutex);
			auto it = g_dodgeWindows.find(a_formID);
			if (it != g_dodgeWindows.end()) {
				startTime = it->second.startTime;
				hasTimer = true;
				activeKind = it->second.kind;
			}
		}

		auto now = std::chrono::steady_clock::now();
		float durationSec = hasTimer ? std::chrono::duration<float>(now - startTime).count() : 999.0f;

		// LER ESTADO DO PERF DODGE (Grafo + Nossa Janela de Tempo Customizada)
		bool isPerfDodgingGraph = false;
		actor->GetGraphVariableBool("isPerfDodgingCMF", isPerfDodgingGraph);
		const auto& activeSettings = GetSettingsForKind(actor, activeKind);
		float perfectWindow = ResolveDodgeValue(actor, activeSettings.perfectDodgeWindow, activeSettings.perfectDodgeWindowSource, activeSettings.perfectDodgeWindowActorValue, activeSettings.perfectDodgeWindowGlobal);

		// PRIORIDADE 1: Se o timer indicar que está na janela ou o grafo marcar verdadeiro, é Perfect Dodge
		if (isPerfDodgingGraph || (hasTimer && durationSec <= perfectWindow)) {
			bool perfectEnabled = activeSettings.enablePerfectDodge;
			if (perfectEnabled && Settings::PerkEnablePerfectDodge != 0) {
				if (actor->IsPlayerRef()) {
					auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(Settings::PerkEnablePerfectDodge);
					if (!perk || !actor->HasPerk(perk)) perfectEnabled = false;
				}
			}
			if (perfectEnabled) {
				return DodgeState::Perfect;
			}
		}

		// PRIORIDADE 2: Verificação da Invulnerabilidade Normal
		if (!activeSettings.invulnerabilityEnabled) return DodgeState::None;

		int method = activeSettings.invulnerabilityMethod;
		float customDuration = ResolveDodgeValue(actor, activeSettings.invincibilityDuration, activeSettings.invincibilityDurationSource, activeSettings.invincibilityDurationActorValue, activeSettings.invincibilityDurationGlobal);

		if (method == 1) {
			// Método 1: Tempo de Animação - Verifica estritamente a variável de grafo "isDodgingCMF"
			bool isDodgingGraph = false;
			if (actor->GetGraphVariableBool("isDodgingCMF", isDodgingGraph) && isDodgingGraph) {
				return DodgeState::Invulnerable;
			}
		}
		else {
			// Método 0: Custom Time - Verifica se o tempo decorrido respeita a duração configurada
			if (hasTimer && durationSec <= customDuration) {
				return DodgeState::Invulnerable;
			}
		}

		return DodgeState::None;
	}

	void RemoveWindow(RE::FormID a_formID) {
		std::unique_lock lock(g_dodgeMutex);
		g_dodgeWindows.erase(a_formID);
	}

	//void CleanupExpiredWindows() {
	//	std::unique_lock lock(g_dodgeMutex);
	//	if (g_dodgeWindows.empty()) return;

	//	auto now = std::chrono::steady_clock::now();
	//	for (auto it = g_dodgeWindows.begin(); it != g_dodgeWindows.end(); ) {
	//		float durationSec = std::chrono::duration<float>(now - it->second).count();
	//		float maxLifetime = Settings::TimeStopDuration;

	//		if (auto actor = RE::TESForm::LookupByID<RE::Actor>(it->first)) {
	//			bool isPlayer = actor->IsPlayerRef();
	//			int method = 0;
	//			float customDuration = 0.5f;

	//			if (isPlayer) {
	//				method = Settings::DodgeInvulnerabilityMethod;
	//				customDuration = Settings::DodgeInvincibilityDuration;
	//			}
	//			else {
	//				auto rule = NPCDodgeManager::GetRuleForNPC(actor);
	//				method = rule ? rule->invulnerabilityMethod : Settings::NPCInvulnerabilityMethod;
	//				customDuration = rule ? rule->invincibilityDuration : Settings::NPCInvincibilityDuration;
	//			}

	//			// Quando for Custom Time, altera o isDodgingCMF para 0 assim que atingir o fim do tempo customizado
	//			if (method == 0) {
	//				maxLifetime = std::max(maxLifetime, customDuration);
	//				if (durationSec >= customDuration) {
	//					actor->SetGraphVariableBool("isDodgingCMF", false);
	//				}
	//			}
	//		}

	//		if (durationSec > Settings::TimeStopDuration) {
	//			if (auto actor = RE::TESForm::LookupByID<RE::Actor>(it->first)) {
	//				actor->NotifyAnimationGraph("StopTimeEndCMF");
	//			}
	//		}

	//		if (durationSec > maxLifetime) {
	//			it = g_dodgeWindows.erase(it);
	//		}
	//		else {
	//			++it;
	//		}
	//	}
	//}

	// Função auxiliar que você deve chamar no seu Hook de Dano/Hit 
	bool TryTriggerPerfectDodge(RE::Actor* a_actor) {
		if (!a_actor) return false;
		RE::FormID a_formID = a_actor->GetFormID();
		DodgeState state = GetDodgeState(a_formID);

		if (state == DodgeState::Perfect) {
			auto activeKind = GetActiveKind(a_formID);
			const auto& activeSettings = GetSettingsForKind(a_actor, activeKind);
			bool shouldCountPerfect = false;
			{
				std::unique_lock lock(g_dodgeMutex);
				auto it = g_dodgeWindows.find(a_formID);
				if (it != g_dodgeWindows.end() && !it->second.perfectCounted) {
					it->second.perfectCounted = true;
					shouldCountPerfect = true;
				}
			}

			if (shouldCountPerfect) {
				Tracking::RecordPerfectDodge(a_actor);
			}

			bool stopTime = activeSettings.stopTimeOnPerfectDodge;
			if (stopTime && Settings::PerkStopTimeOnPerfectDodge != 0) {
				auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(Settings::PerkStopTimeOnPerfectDodge);
				if (!perk || !a_actor->HasPerk(perk)) stopTime = false;
			}

			if (stopTime) {
				a_actor->NotifyAnimationGraph("StopTimeStartCMF");

				auto timeStopStart = std::chrono::steady_clock::now();
				{
					std::unique_lock lock(g_dodgeMutex);
					g_dodgeWindows[a_formID] = { timeStopStart, activeKind, true };
				}

				auto actorHandle = a_actor->CreateRefHandle();
				float duration = ResolveDodgeValue(a_actor, activeSettings.timeStopDuration, activeSettings.timeStopDurationSource, activeSettings.timeStopDurationActorValue, activeSettings.timeStopDurationGlobal);

				// Agenda de forma reativa o término do congelamento de tempo e a limpeza final do mapa
				Utils::DelayedDispatcher::Get().PostDelayed(std::chrono::milliseconds(static_cast<int>(duration * 1000)), [actorHandle, a_formID, timeStopStart]() {
					SKSE::GetTaskInterface()->AddTask([actorHandle, a_formID, timeStopStart]() {
						if (actorHandle && actorHandle.get()) {
							actorHandle.get()->NotifyAnimationGraph("StopTimeEndCMF");
						}
						std::unique_lock lock(g_dodgeMutex);
						auto it = g_dodgeWindows.find(a_formID);
						if (it != g_dodgeWindows.end() && it->second.startTime == timeStopStart) {
							g_dodgeWindows.erase(it);
						}
						});
					});
			}
			else {
				RemoveWindow(a_formID);
			}
			return true;
		}

		return false;
	}

	void ResetAll() {
		std::unique_lock lock(g_dodgeMutex);
		g_dodgeWindows.clear();
		logger::info("DodgeTimerManager::ResetAll - Todos os timers de esquiva foram limpos.");
	}
}

void PlayerMovementInputListener::DispatchMovementIntent() {
	const bool hasMovement = forwardPressed || backPressed || leftPressed || rightPressed || leftStickMoving;
	if (hasMovement == lastMovementIntent) return;

	lastMovementIntent = hasMovement;
	DodgeTimerManager::UpdatePlayerMovementIntent(hasMovement);
}

void PlayerMovementInputListener::Reset() {
	forwardPressed = false;
	backPressed = false;
	leftPressed = false;
	rightPressed = false;
	leftStickMoving = false;
	lastMovementIntent = false;
	DodgeTimerManager::UpdatePlayerMovementIntent(false);
}

RE::BSEventNotifyControl PlayerMovementInputListener::ProcessEvent(RE::InputEvent* const* a_event,
	RE::BSTEventSource<RE::InputEvent*>*) {
	if (!a_event || !*a_event) return RE::BSEventNotifyControl::kContinue;

	bool movementChanged = false;
	for (auto* event = *a_event; event; event = event->next) {
		if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
			auto* thumbstick = event->AsThumbstickEvent();
			if (!thumbstick || !thumbstick->IsLeft()) continue;

			constexpr float kMovementDeadzone = 0.25f;
			const bool isMoving = (thumbstick->xValue * thumbstick->xValue + thumbstick->yValue * thumbstick->yValue) >
				(kMovementDeadzone * kMovementDeadzone);
			if (leftStickMoving != isMoving) {
				leftStickMoving = isMoving;
				movementChanged = true;
			}
		}
		else if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
			auto* button = event->AsButtonEvent();
			if (!button || (!button->IsDown() && !button->IsUp())) continue;

			const auto* userEvents = RE::UserEvents::GetSingleton();
			if (!userEvents) continue;

			bool* movementState = nullptr;
			const auto& userEvent = button->GetUserEvent();
			if (userEvent == userEvents->forward) movementState = &forwardPressed;
			else if (userEvent == userEvents->back) movementState = &backPressed;
			else if (userEvent == userEvents->strafeLeft) movementState = &leftPressed;
			else if (userEvent == userEvents->strafeRight) movementState = &rightPressed;

			if (movementState) {
				const bool isPressed = button->IsPressed();
				if (*movementState != isPressed) {
					*movementState = isPressed;
					movementChanged = true;
				}
			}
		}
	}

	if (movementChanged) DispatchMovementIntent();
	return RE::BSEventNotifyControl::kContinue;
}

namespace Idles {

	inline RE::TESIdleForm* GetIdleByFormID(RE::FormID formID, const std::string& pluginName) {
		if (auto dataHandler = RE::TESDataHandler::GetSingleton()) {
			auto form = dataHandler->LookupForm(formID, pluginName);
			if (form) return form->As<RE::TESIdleForm>();
		}
		return nullptr;
	}

	void InitIdles() {
		const std::string skyrim = "Skyrim.esm"; 
		DodgeRoll = GetIdleByFormID(0x13AC8, skyrim);
		DodgeStop = GetIdleByFormID(0x10DE0F, skyrim);
	}

	inline void PlayIdleAnimation(RE::Actor* actor, RE::TESIdleForm* idle) {
		if (actor && idle) {
			if (auto* processManager = actor->GetActorRuntimeData().currentProcess) {
				processManager->PlayIdle(actor, idle, actor);
			}
			else {
				SKSE::log::error("Não foi possível obter o AIProcess (currentProcess) do ator.");
			}
		}
	}

}

inline std::array blockedMenus = {
	RE::DialogueMenu::MENU_NAME,    RE::JournalMenu::MENU_NAME,    RE::MapMenu::MENU_NAME,
	RE::StatsMenu::MENU_NAME,       RE::ContainerMenu::MENU_NAME,  RE::InventoryMenu::MENU_NAME,
	RE::MagicMenu::MENU_NAME,
	RE::TweenMenu::MENU_NAME,       RE::TrainingMenu::MENU_NAME,   RE::TutorialMenu::MENU_NAME,
	RE::LockpickingMenu::MENU_NAME, RE::SleepWaitMenu::MENU_NAME,  RE::LevelUpMenu::MENU_NAME,
	RE::Console::MENU_NAME,         RE::BookMenu::MENU_NAME,       RE::CreditsMenu::MENU_NAME,
	RE::LoadingMenu::MENU_NAME,     RE::MessageBoxMenu::MENU_NAME, RE::MainMenu::MENU_NAME,
	RE::RaceSexMenu::MENU_NAME,     RE::FavoritesMenu::MENU_NAME
};

bool IsAnyMenuOpen() {
	const auto ui = RE::UI::GetSingleton();
	for (const auto a_name : blockedMenus) {
		if (ui->IsMenuOpen(a_name)) {
			return true;
		}
	}
	return false;
}

void StopCastingForDodge(RE::Actor* actor) {
	if (!actor) return;

	actor->InterruptCast(false);
	actor->NotifyAnimationGraph("CastStop");
}

// --- Gerenciador de Recursos e Eventos da Animação ---
bool ConsumeResource(RE::Actor* actor, int type, float cost) {
	if (cost <= 0.0f) return true; // Esquiva grátis

	RE::ActorValue av = RE::ActorValue::kStamina; // Default = 1
	if (type == 0) av = RE::ActorValue::kHealth;
	else if (type == 2) av = RE::ActorValue::kMagicka;

	float currentVal = actor->AsActorValueOwner()->GetActorValue(av);
	if (currentVal >= cost) {
		// Causa "Dano" no atributo para consumir sem afetar o max pool permanentemente
		actor->AsActorValueOwner()->DamageActorValue(av, cost);
		return true;
	}
	return false;
}


namespace NPCDodgeManager {

	bool IsNPCDodgeGloballyAllowed(RE::Actor* a_actor) {
		if (!a_actor || a_actor->IsPlayerRef() || a_actor->IsDead()) {
			return false;
		}

		if (!Settings::NPCDodgeEnabled) {
			return false;
		}

		if (Settings::NPCDisableDodgePerk != 0) {
			auto disablePerk = RE::TESForm::LookupByID<RE::BGSPerk>(Settings::NPCDisableDodgePerk);
			if (disablePerk && a_actor->HasPerk(disablePerk)) {
				return false;
			}
		}

		return true;
	}

	void ClearNPCDodgeState(RE::Actor* a_actor) {
		if (!a_actor || a_actor->IsPlayerRef()) {
			return;
		}

		DodgeTimerManager::RemoveWindow(a_actor->GetFormID());
		a_actor->SetGraphVariableBool("isDodgingCMF", false);
		a_actor->SetGraphVariableBool("isPerfDodgingCMF", false);
	}
	
	bool HasHeavyBodyArmor(RE::Actor* a_actor) {
		if (!a_actor) return false;
		auto armor = a_actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kBody);
		if (armor && armor->IsHeavyArmor()) {
			return true;
		}
		return false;
	}

	bool UsesMeleeWeapon(RE::Actor* a_actor) {
		if (!a_actor) return false;

		auto IsMeleeObject = [](RE::TESForm* obj) {
			auto weapon = obj ? obj->As<RE::TESObjectWEAP>() : nullptr;
			return weapon && weapon->IsMelee();
			};

		return IsMeleeObject(a_actor->GetEquippedObject(false)) || IsMeleeObject(a_actor->GetEquippedObject(true));
	}

	// Função matemática real para avaliar o roll dos dados
	bool EvaluateDodgeChance(RE::Actor* npc, const Settings::ChanceSettings& s) {
		float hpMax = npc->GetActorValueMax(RE::ActorValue::kHealth);
		float hpCur = npc->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
		float missingHpPct = hpMax > 0.0f ? (1.0f - (hpCur / hpMax)) : 0.0f;

		float aggro = npc->AsActorValueOwner()->GetActorValue(RE::ActorValue::kAggression);
		float sneakSkill = npc->AsActorValueOwner()->GetActorValue(RE::ActorValue::kSneak);
		float staminaMax = npc->GetActorValueMax(RE::ActorValue::kStamina);

		// Novos modificadores físicos
		float weight = npc->GetWeight();
		float equippedWeight = npc->GetEquippedWeight(); 
		float carryWeight = npc->GetActorValueMax(RE::ActorValue::kCarryWeight);
		float inventoryWeight = npc->AsActorValueOwner()->GetActorValue(RE::ActorValue::kInventoryWeight);
		float encumbrancePct = carryWeight > 0.0f ? (inventoryWeight / carryWeight) : 0.0f;

		// Fórmula de Poder
		float power = s.baseChance
			+ (missingHpPct * 100.0f * s.healthMult)
			+ (aggro * s.aggressionMult)
			+ (sneakSkill * s.skillMult)
			+ (staminaMax * s.maxStaminaMult)
			+ (weight * s.weightMult)
			+ (equippedWeight * s.equippedWeightMult) 
			+ (encumbrancePct * 100.0f * s.encumbranceMult);

		if (power < 0.0f) power = 0.0f; // Evita probabilidade negativa de quebrar a matemática

		float chance = (power / (power + s.globalDifficulty)) * 100.0f;

		static std::mt19937 gen(std::random_device{}());
		std::uniform_real_distribution<float> dis(0.0f, 100.0f);
		return dis(gen) <= chance;
	}

	bool EvaluateFlatChance(float chance) {
		if (chance <= 0.0f) return false;
		if (chance >= 100.0f) return true;

		static std::mt19937 gen(std::random_device{}());
		std::uniform_real_distribution<float> dis(0.0f, 100.0f);
		return dis(gen) <= chance;
	}

	bool IsEventInList(std::string_view eventName, const std::vector<std::string>& events) {
		return std::any_of(events.begin(), events.end(), [&](const std::string& item) {
			return eventName == item;
			});
	}

	// Retorna se o NPC deve desviar e qual o tipo forçado (se houver regra)
	const Settings::NPCDodgeRule* GetRuleForNPC(RE::Actor* npc) {
		if (!IsNPCDodgeGloballyAllowed(npc)) return nullptr;
		for (const auto& rule : Settings::NPCRules) {
			if (auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(rule.perkID)) {
				if (npc->HasPerk(perk)) return &rule;
			}
		}
		return nullptr;
	}

	// valida fisicamente se o NPC tem condições de esquivar
	bool IsValidToDodge(RE::Actor* a_actor, bool allowStagger) {
		if (!a_actor || a_actor->IsDead()) return false;

		const auto state = a_actor->AsActorState();
		if (!state) return false;

		// 1. Barreiras de Estado Físico (Animações Incompatíveis)
		if (a_actor->IsInKillMove() ||
			state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal ||
			state->GetFlyState() != RE::FLY_STATE::kNone) {
			return false;
		}

		// 2. Barreira de Combate (Deve estar com arma sacada, magia ou punhos erguidos)
		if (state->GetWeaponState() != RE::WEAPON_STATE::kDrawn) {
			return false;
		}

		// 3. Barreira de Queda/Ragdoll
		if (state->GetKnockState() != RE::KNOCK_STATE_ENUM::kNormal) {
			return false;
		}

		// 4. Barreira de Stagger (Atordoamento) lendo o Behavior Graph da Engine
		if (!allowStagger && a_actor->IsStaggering()) {
			return false;
		}

		return true;
	}

	std::pair<bool, const Settings::NPCDodgeRule*> ShouldNPCDodge(RE::Actor* npc) {
		if (!IsNPCDodgeGloballyAllowed(npc)) {
			ClearNPCDodgeState(npc);
			return { false, nullptr };
		}

		auto rule = GetRuleForNPC(npc);
		bool allowStagger = rule ? rule->allowStaggerDodge : Settings::NPCAllowStaggerDodge;

		// Passamos a permissão para a validação física
		if (!IsValidToDodge(npc, allowStagger)) {
			return { false, nullptr };
		}

		return { EvaluateDodgeChance(npc, rule ? rule->chance : Settings::NPCGlobalChance), rule };
	}

	// Converte a direção (1-8) em um vetor de movimento local
	RE::NiPoint3 GetDodgeVector(int direction, float distance = 150.0f) {
		RE::NiPoint3 ret(0.f, 0.f, 0.f);
		float diag = distance * 0.7071f; // aproximação de distance / sqrt(2)

		switch (direction) {
		case 1: ret.y = distance; break;                 // Forward
		case 2: ret.x = diag; ret.y = diag; break;       // Forward-Right
		case 3: ret.x = distance; break;                 // Right
		case 4: ret.x = diag; ret.y = -diag; break;      // Backward-Right
		case 5: ret.y = -distance; break;                // Backward
		case 6: ret.x = -diag; ret.y = -diag; break;     // Backward-Left
		case 7: ret.x = -distance; break;                // Left
		case 8: ret.x = -diag; ret.y = diag; break;      // Forward-Left
		}
		return ret;
	}

	// Calcula a posição absoluta baseada na posição/rotação atual do ator
	RE::NiPoint3 GetAbsPos(RE::Actor* a_actor, RE::NiPoint3 a_offset) {
		float angle = a_actor->data.angle.z;
		float sinA = std::sin(angle);
		float cosA = std::cos(angle);

		RE::NiPoint3 pos = a_actor->GetPosition();
		pos.x += a_offset.x * cosA - a_offset.y * sinA;
		pos.y += a_offset.x * sinA + a_offset.y * cosA;
		pos.z += a_offset.z;
		return pos;
	}

	// Dtry Valor code base
	// Verifica se existe chão/objeto sólido embaixo da posição desejada (Evita quedas em precipícios)
	bool object_exists(RE::Actor* a_actor, RE::NiPoint3& a_pos, float a_range = 75.0f)
	{
		if (!a_actor) return false;

		RE::NiPoint3 rayStart = a_pos;
		RE::NiPoint3 rayEnd = a_pos;
		rayStart.z += a_range;
		rayEnd.z -= a_range;

		auto havokWorldScale = RE::bhkWorld::GetWorldScale();
		RE::bhkPickData pick_data;
		pick_data.rayInput.from = rayStart * havokWorldScale;
		pick_data.rayInput.to = rayEnd * havokWorldScale;

		// Correção de segurança: Busca o mundo físico da célula do próprio Ator, evitando crashes
		auto parentCell = a_actor->GetParentCell();
		if (!parentCell || !parentCell->GetbhkWorld()) {
			return false;
		}

		parentCell->GetbhkWorld()->PickObject(pick_data);

		if (pick_data.rayOutput.HasHit()) {
			RE::NiPoint3 hitpos = rayStart + (rayEnd - rayStart) * pick_data.rayOutput.hitFraction;
			a_pos = hitpos;  // Atualiza a posição tridimensional com a altura real do chão encontrado
			return true;
		}
		return false;
	}

	// Dtry Valor code base
	// Projeta um raio horizontal para detectar obstáculos físicos como paredes, pilares ou atores
	RE::TESObjectREFR* cast_ray(RE::Actor* a_actor, RE::NiPoint3 a_rayEnd, float a_castPos, float* ret_rayDist)
	{
		auto havokWorldScale = RE::bhkWorld::GetWorldScale();
		RE::bhkPickData pick_data;
		RE::NiPoint3 rayStart = a_actor->GetPosition();
		float castHeight = a_actor->GetHeight() * a_castPos;
		rayStart.z += castHeight;  // Conjura o raio a partir da altura proporcional do corpo do ator

		pick_data.rayInput.from = rayStart * havokWorldScale;
		pick_data.rayInput.to = a_rayEnd * havokWorldScale;

		/* Configuração do filtro de colisão do Havok ignorando o próprio ator */
		RE::CFilter actorFilter;
		a_actor->GetCollisionFilterInfo(actorFilter);
		pick_data.rayInput.filterInfo.filter = (actorFilter.filter & 0xFFFF0000) | static_cast<uint32_t>(RE::COL_LAYER::kCharController);

		a_actor->GetParentCell()->GetbhkWorld()->PickObject(pick_data);

		if (pick_data.rayOutput.HasHit()) {
			RE::NiPoint3 hitpos = rayStart + (a_rayEnd - rayStart) * pick_data.rayOutput.hitFraction;
			if (ret_rayDist) {
				*ret_rayDist = hitpos.GetDistance(rayStart);
			}

			auto collidable = pick_data.rayOutput.rootCollidable;
			if (collidable) {
				RE::TESObjectREFR* ref = RE::TESHavokUtilities::FindCollidableRef(*collidable);
				if (ref) {
					return ref;
				}
			}
		}
		return nullptr;
	}


		

	bool CanGoTo(RE::Actor* a_actor, RE::NiPoint3 a_dest, float a_dodgeDist) {
		bool canNavigate = false;
		bool noObstacle = true;
		RE::NiPoint3 raycast_dest = a_dest;
		RE::NiPoint3 nav_dest = a_dest;

		bool gotoNavdest = false;
		float expect_dist = a_actor->GetPosition().GetDistance(a_dest);

		/* 1. Validação Primária via Skyrim NavMesh Interna */
		if (a_actor->UpdateNavPos(a_actor->GetPosition(), nav_dest, 4.0f, a_actor->GetBoundRadius())
			&& abs(nav_dest.GetDistance(a_actor->GetPosition()) - expect_dist) < 50.0f) {

			RE::NiPoint3 nav_dest_raycast = nav_dest;
			// Verifica fisicamente com raio vertical se há suporte sólido na Navmesh (Segurança de altura: 50 unidades)
			if (object_exists(a_actor, nav_dest_raycast, 50.0f)) {
				gotoNavdest = true;
				canNavigate = true;
			}
		}

		/* 2. Sistema de redundância (Fallback) caso a NavMesh falhe/falte dados */
		if (!gotoNavdest) {
			// Varre uma distância vertical maior (75 unidades) para aceitar superfícies inclinadas e rampas físicas
			canNavigate = object_exists(a_actor, raycast_dest, 75.0f);
		}

		/* 3. Escaneamento de Obstáculos no Percurso (Raios Horizontais) */
		if (canNavigate) {
			RE::NiPoint3 dest = gotoNavdest ? nav_dest : raycast_dest;

			float obstacleDist = 0.0f;
			float actorHeight = a_actor->GetHeight();

			// Define a distância mínima tolerável baseada no tamanho real da esquiva executada (com margem segura de 90%)
			float permissibleDist = a_dodgeDist * 0.9f;

			// Executa 3 varreduras absolutas em alturas diferentes do corpo para evitar colisões cegas
			RE::NiPoint3 rayDest = dest;

			// Raio 1: Altura dos Tornozelos/Joelhos (25%)
			rayDest.z = dest.z + (actorHeight * 0.25f);
			noObstacle &= (cast_ray(a_actor, rayDest, 0.25f, &obstacleDist) == nullptr || obstacleDist >= permissibleDist);

			// Raio 2: Altura da Cintura (50%)
			rayDest.z = dest.z + (actorHeight * 0.50f);
			noObstacle &= (cast_ray(a_actor, rayDest, 0.50f, &obstacleDist) == nullptr || obstacleDist >= permissibleDist);

			// Raio 3: Altura do Peito (75%)
			rayDest.z = dest.z + (actorHeight * 0.75f);
			noObstacle &= (cast_ray(a_actor, rayDest, 0.75f, &obstacleDist) == nullptr || obstacleDist >= permissibleDist);
		}

		return canNavigate && noObstacle;
	}

	void AttemptDodge(RE::Actor* npc, RE::Actor* attacker, const Settings::NPCDodgeRule* rule) {
		if (!IsNPCDodgeGloballyAllowed(npc)) {
			ClearNPCDodgeState(npc);
			return;
		}

		int dodgeType = rule ? rule->dodgeType : Settings::NPCDodgeType;
		bool rollOnlyLight = rule ? rule->rollOnlyLightArmor : Settings::NPCRollOnlyLightArmor;
		bool useSmartDodge = rule ? rule->smartDodge : Settings::NPCSmartDodge;

		if (rollOnlyLight && HasHeavyBodyArmor(npc)) {
			if (dodgeType == 0 || dodgeType == 2) dodgeType = 1;
		}
		else if (dodgeType == 0) {
			static std::mt19937 gen(std::random_device{}());
			std::uniform_int_distribution<int> dis(1, 2);
			dodgeType = dis(gen);
		}

		const auto& dodgeSettings = (dodgeType == 2)
			? (rule ? rule->rollSettings : Settings::NPCRollSettings)
			: (rule ? rule->stepSettings : Settings::NPCStepSettings);
		bool isAttacking = npc->AsActorState()->GetAttackState() != RE::ATTACK_STATE_ENUM::kNone;
		if (isAttacking && !dodgeSettings.cancelAttackWithDodge) return;

		float cost = ResolveDodgeValue(npc, dodgeSettings.cost, dodgeSettings.costSource, dodgeSettings.costActorValue, dodgeSettings.costGlobal);
		int typeCost = dodgeSettings.costType;
		if (!ConsumeResource(npc, typeCost, cost)) return;

		std::vector<int> directions = { 1, 2, 3, 4, 5, 6, 7, 8 };
		std::random_device rd; std::mt19937 gen(rd());
		std::shuffle(directions.begin(), directions.end(), gen);

		int safeDirection = 0;
		if (useSmartDodge) {
			for (int dir : directions) {
				float dist = (dodgeType == 2) ? 150.0f : 70.0f;
				RE::NiPoint3 dest = GetAbsPos(npc, GetDodgeVector(dir, dist));
				if (CanGoTo(npc, dest, dist)) { safeDirection = dir; break; }
			}
		}

		bool isDodging = false;
		npc->GetGraphVariableBool("isDodgingCMF", isDodging);
		if (isDodging) {
			if (dodgeSettings.canSpamDodge) {
				npc->NotifyAnimationGraph("BF_DodgeStop");
				npc->SetGraphVariableBool("isDodgingCMF", false);
			}
			else return;
		}

		npc->SetGraphVariableInt("DirecionalCycleMoveset", safeDirection);
		if (dodgeType == 2) npc->NotifyAnimationGraph("BF_RollDodgeStart");
		else npc->NotifyAnimationGraph("BF_StepDodgeStart");
		DodgeTimerManager::StartDodge(npc->GetFormID(), dodgeType == 2);

		const Settings::DistanceSettings& distConfig = (dodgeType == 2)
			? (rule ? rule->rollDistance : Settings::NPCGlobalRollDistance)
			: (rule ? rule->stepDistance : Settings::NPCGlobalStepDistance);
		ProcessAndExecuteDodgeImpulse(npc, safeDirection, distConfig);
	}

	void AttemptDashGapClose(RE::Actor* npc, RE::Actor* target, const Settings::NPCDodgeRule* rule) {
		if (!IsNPCDodgeGloballyAllowed(npc)) {
			ClearNPCDodgeState(npc);
			return;
		}
		if (!npc || !target || npc == target || !UsesMeleeWeapon(npc)) return;

		bool dashEnabled = rule ? rule->dashGapCloseEnabled : Settings::NPCDashGapCloseEnabled;
		if (!dashEnabled) return;

		bool allowStagger = rule ? rule->allowStaggerDodge : Settings::NPCAllowStaggerDodge;
		if (!IsValidToDodge(npc, allowStagger)) return;

		if (!EvaluateDodgeChance(npc, rule ? rule->dashChance : Settings::NPCGlobalDashChance)) return;

		bool isDodging = false;
		npc->GetGraphVariableBool("isDodgingCMF", isDodging);
		const auto& dashSettings = rule ? rule->dashSettings : Settings::NPCDashSettings;
		bool isAttacking = npc->AsActorState()->GetAttackState() != RE::ATTACK_STATE_ENUM::kNone;
		if (isAttacking && !dashSettings.cancelAttackWithDodge) return;

		if (isDodging) {
			if (dashSettings.canSpamDodge) {
				npc->NotifyAnimationGraph("BF_DodgeStop");
				npc->SetGraphVariableBool("isDodgingCMF", false);
			}
			else {
				return;
			}
		}

		float cost = ResolveDodgeValue(npc, dashSettings.cost, dashSettings.costSource, dashSettings.costActorValue, dashSettings.costGlobal);
		int typeCost = dashSettings.costType;
		if (!ConsumeResource(npc, typeCost, cost)) return;

		npc->SetGraphVariableInt("DirecionalCycleMoveset", 1);
		npc->NotifyAnimationGraph("BF_DashStart");
		DodgeTimerManager::StartDash(npc->GetFormID());

		const Settings::DistanceSettings& distConfig = rule ? rule->dashDistance : Settings::NPCGlobalDashDistance;
		ProcessAndExecuteDodgeImpulse(npc, 1, distConfig);
	}

	bool TryRandomDodgeFromEvent(RE::Actor* attacker, std::string_view eventName) {
		if (!attacker || attacker->IsDead()) return false;

		std::unordered_set<RE::FormID> observers;
		for (auto id : VisionTracker::GetValidObservers(false, attacker->GetFormID(), 1)) observers.insert(id);
		for (auto id : VisionTracker::GetValidObservers(true, attacker->GetFormID(), 1)) observers.insert(id);

		bool handledByRandomConfig = false;
		for (RE::FormID watcherID : observers) {
			auto* watcherForm = RE::TESForm::LookupByID(watcherID);
			if (!watcherForm) continue;

			auto* targetNpc = watcherForm->As<RE::Actor>();
			if (!targetNpc || targetNpc == attacker || targetNpc->IsDead() || targetNpc->IsPlayerRef()) continue;
			if (!IsNPCDodgeGloballyAllowed(targetNpc)) {
				ClearNPCDodgeState(targetNpc);
				continue;
			}

			auto rule = GetRuleForNPC(targetNpc);
			bool enabled = rule ? rule->randomDodgeEnabled : Settings::NPCRandomDodgeEnabled;
			if (!enabled) continue;

			const auto& eventList = rule ? rule->randomDodgeEvents : Settings::NPCRandomDodgeEvents;
			if (!IsEventInList(eventName, eventList)) continue;
			handledByRandomConfig = true;

			bool useArea = rule ? rule->randomDodgeUseArea : Settings::NPCRandomDodgeUseArea;
			int depth = VisionTracker::GetObserverDepth(useArea ? false : true, watcherID, attacker->GetFormID());
			if (depth != 1) continue;

			bool requireLoS = rule ? rule->randomDodgeRequireLoS : Settings::NPCRandomDodgeRequireLoS;
			if (requireLoS) {
				bool arg2 = false;
				if (!targetNpc->HasLineOfSight(attacker, arg2)) continue;
			}

			bool allowStagger = rule ? rule->allowStaggerDodge : Settings::NPCAllowStaggerDodge;
			if (!IsValidToDodge(targetNpc, allowStagger)) continue;

			float chance = rule ? rule->randomDodgeChance : Settings::NPCRandomDodgeChance;
			if (EvaluateFlatChance(chance)) {
				AttemptDodge(targetNpc, attacker, rule);
			}
		}

		return handledByRandomConfig;
	}
}

// --- Lógica Isolada do Player ---
const Settings::DistanceSettings& GetPlayerDistanceConfig(bool isRoll, bool isDash, bool isSheathed) {
	if (Settings::PlayerUseSeparateSheathedDistance && isSheathed) {
		if (isDash) return Settings::PlayerDashSheathedDistance;
		if (isRoll) return Settings::PlayerRollSheathedDistance;
		return Settings::PlayerStepSheathedDistance;
	}

	if (isDash) return Settings::PlayerDashDistance;
	if (isRoll) return Settings::PlayerRollDistance;
	return Settings::PlayerStepDistance;
}

void AttemptPlayerDodge(bool isRoll, bool isStep, bool isDash = false) {
	if (!isRoll && !isStep && !isDash) return;
	if ((isRoll && !Settings::DodgeRollEnabled) || (isStep && !Settings::DodgeStepEnabled) || (isDash && !Settings::DashEnabled)) return;
	auto player = RE::PlayerCharacter::GetSingleton();
	if (!player || !player->Is3DLoaded() || IsAnyMenuOpen()) return;

	const auto playerState = player->AsActorState();
	auto weaponState = playerState->GetWeaponState();
	bool isSheathed = (weaponState == RE::WEAPON_STATE::kSheathed || weaponState == RE::WEAPON_STATE::kSheathing);
	if (isSheathed) {
		bool allowSheathed = Settings::CanDodgeSheathed;
		if (!allowSheathed && Settings::PerkCanDodgeSheathed != 0) {
			auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(Settings::PerkCanDodgeSheathed);
			if (perk && player->HasPerk(perk)) allowSheathed = true;
		}
		if (!allowSheathed) return;
	}

	if (!(!player->IsInKillMove() && playerState->GetSitSleepState() == RE::SIT_SLEEP_STATE::kNormal && playerState->GetFlyState() == RE::FLY_STATE::kNone)) return;

	if (isRoll && Settings::PerkRollDodge != 0) {
		auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(Settings::PerkRollDodge);
		if (!perk || !player->HasPerk(perk)) return;
	}
	if (isStep && Settings::PerkStepDodge != 0) {
		auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(Settings::PerkStepDodge);
		if (!perk || !player->HasPerk(perk)) return;
	}

	const auto& dodgeSettings = isDash ? Settings::PlayerDashSettings : (isRoll ? Settings::PlayerRollSettings : Settings::PlayerStepSettings);
	if (playerState->GetAttackState() != RE::ATTACK_STATE_ENUM::kNone) {
		bool canCancelAttack = dodgeSettings.cancelAttackWithDodge;
		if (canCancelAttack && Settings::PerkCancelAttackWithDodge != 0) {
			auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(Settings::PerkCancelAttackWithDodge);
			if (!perk || !player->HasPerk(perk)) canCancelAttack = false;
		}
		if (!canCancelAttack) return;
	}

	bool isDodging = false;
	player->GetGraphVariableBool("isDodgingCMF", isDodging);
	if (isDodging) {
		bool hasActiveTimer = false;
		{
			std::shared_lock lock(DodgeTimerManager::g_dodgeMutex);
			if (DodgeTimerManager::g_dodgeWindows.find(player->GetFormID()) != DodgeTimerManager::g_dodgeWindows.end()) hasActiveTimer = true;
		}
		if (!hasActiveTimer) {
			player->SetGraphVariableBool("isDodgingCMF", false);
			player->SetGraphVariableBool("isPerfDodgingCMF", false);
			isDodging = false;
		}
	}

	bool canSpam = dodgeSettings.canSpamDodge;
	if (canSpam && Settings::PerkCanSpamDodge != 0) {
		auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(Settings::PerkCanSpamDodge);
		if (!perk || !player->HasPerk(perk)) canSpam = false;
	}
	if (!canSpam && isDodging) return;

	int movement = 0;
	player->GetGraphVariableInt("DirecionalCycleMoveset", movement);
	const auto& distanceConfig = GetPlayerDistanceConfig(isRoll, isDash, isSheathed);

	if (isRoll) {
		if (ConsumeResource(player, Settings::PlayerRollSettings.costType, ResolveDodgeValue(player, Settings::PlayerRollSettings.cost, Settings::PlayerRollSettings.costSource, Settings::PlayerRollSettings.costActorValue, Settings::PlayerRollSettings.costGlobal))) {
			if (canSpam && isDodging) player->NotifyAnimationGraph("BF_DodgeStop");
			StopCastingForDodge(player);

			bool rollUpwards = Settings::DodgeRollUpwards;
			if (rollUpwards && Settings::PerkRollUpwards != 0) {
				auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(Settings::PerkRollUpwards);
				if (!perk || !player->HasPerk(perk)) rollUpwards = false;
			}

			const bool animOk = DodgeFallback::SendAnimation(player, DodgeFallback::Kind::Roll, "BF_RollDodgeStart");
			DodgeTimerManager::StartDodge(player->GetFormID(), true);
			const bool impulseOk = ProcessAndExecuteDodgeImpulse(player, movement, distanceConfig, rollUpwards, Settings::DodgeRollAerialBoost);
			DodgeFallback::Supervise(player, DodgeFallback::Kind::Roll, animOk, impulseOk);
		}
	}
	else if (isStep) {
		if (ConsumeResource(player, Settings::PlayerStepSettings.costType, ResolveDodgeValue(player, Settings::PlayerStepSettings.cost, Settings::PlayerStepSettings.costSource, Settings::PlayerStepSettings.costActorValue, Settings::PlayerStepSettings.costGlobal))) {
			if (canSpam && isDodging) player->NotifyAnimationGraph("BF_DodgeStop");
			StopCastingForDodge(player);

			bool stepUpwards = Settings::DodgeStepUpwards;
			if (stepUpwards && Settings::PerkStepUpwards != 0) {
				auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(Settings::PerkStepUpwards);
				if (!perk || !player->HasPerk(perk)) stepUpwards = false;
			}

			const bool animOk = DodgeFallback::SendAnimation(player, DodgeFallback::Kind::Step, "BF_StepDodgeStart");
			DodgeTimerManager::StartDodge(player->GetFormID(), false);
			const bool impulseOk = ProcessAndExecuteDodgeImpulse(player, movement, distanceConfig, stepUpwards, Settings::DodgeStepAerialBoost);
			DodgeFallback::Supervise(player, DodgeFallback::Kind::Step, animOk, impulseOk);
		}
	}
	else if (isDash) {
		if (ConsumeResource(player, Settings::PlayerDashSettings.costType, ResolveDodgeValue(player, Settings::PlayerDashSettings.cost, Settings::PlayerDashSettings.costSource, Settings::PlayerDashSettings.costActorValue, Settings::PlayerDashSettings.costGlobal))) {
			if (canSpam && isDodging) player->NotifyAnimationGraph("BF_DodgeStop");
			StopCastingForDodge(player);

			const bool animOk = DodgeFallback::SendAnimation(player, DodgeFallback::Kind::Dash, "BF_DashStart");
			DodgeTimerManager::StartDash(player->GetFormID());
			const bool impulseOk = ProcessAndExecuteDodgeImpulse(player, movement, distanceConfig);
			DodgeFallback::Supervise(player, DodgeFallback::Kind::Dash, animOk, impulseOk);
		}
	}
}

namespace ProjectileMemory {
	std::unordered_map<RE::FormID, std::unordered_map<RE::FormID, std::chrono::steady_clock::time_point>> memory;
	std::shared_mutex mtx;

	bool HasProcessedAndRegister(RE::FormID npcID, RE::FormID projID) {
		std::unique_lock lock(mtx);
		auto now = std::chrono::steady_clock::now();

		auto& npcMem = memory[npcID];

		// Limpeza rápida do cache para evitar vazamento de memória.
		// (Projéteis não duram mais que alguns segundos no ar)
		for (auto it = npcMem.begin(); it != npcMem.end(); ) {
			if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count() > 3) {
				it = npcMem.erase(it);
			}
			else {
				++it;
			}
		}

		// Verifica se já existe na memória
		if (npcMem.find(projID) != npcMem.end()) {
			return true; // Já processou este projétil
		}

		// Registra o novo projétil
		npcMem[projID] = now;
		return false;
	}
}

RE::BSEventNotifyControl DodgeListener::ProcessEvent(const SKSE::ModCallbackEvent* a_event, RE::BSTEventSource<SKSE::ModCallbackEvent>*)
{
	if (!a_event) return RE::BSEventNotifyControl::kContinue;

	std::string_view eventName = a_event->eventName.c_str();
	int actionID = static_cast<int>(a_event->numArg);

	if (eventName == "TweenPause_ControlUpdated") {
		rapidjson::Document doc;
		doc.Parse(a_event->strArg.c_str());

		if (!doc.HasParseError() && doc.IsObject()) {
			std::string actionId = doc["actionId"].GetString();

			if (actionId == "DodgeRoll" || actionId == "DodgeStep" || actionId == "Dash") {
				DodgeModMenu::UnregisterInputCategory(actionId);

				std::vector<int> newActions;
				std::vector<int> newMotions;

				if (doc.HasMember("mappedIds") && doc["mappedIds"].IsArray()) {
					for (auto& bind : doc["mappedIds"].GetArray()) {
						if (bind.HasMember("actionID") && bind["actionID"].IsInt()) newActions.push_back(bind["actionID"].GetInt());
						if (bind.HasMember("motionID") && bind["motionID"].IsInt()) newMotions.push_back(bind["motionID"].GetInt());
					}
				}

				if (actionId == "DodgeRoll") {
					Settings::DodgeRollActionIDs = newActions;
					Settings::DodgeRollMotionIDs = newMotions;
				}
				else if (actionId == "DodgeStep") {
					Settings::DodgeStepActionIDs = newActions;
					Settings::DodgeStepMotionIDs = newMotions;
				}
				else if (actionId == "Dash") {
					Settings::DashActionIDs = newActions;
					Settings::DashMotionIDs = newMotions;
				}

				DodgeModMenu::RegisterAllInputs();
				DodgeModMenu::SaveSettings();
			}
		}
	}

	if (eventName == "DynamicFormsGeneratorLoaded") {
		Manager::GetSingleton()->PopulateAllLists();
		return RE::BSEventNotifyControl::kContinue;
	}

	if (eventName == "DynamicFormsGeneratorUpdated") {
		Manager::GetSingleton()->RefreshLists(a_event->strArg.c_str());
		return RE::BSEventNotifyControl::kContinue;
	}

	auto HasInput = [](const std::vector<int>& list, int id) {
		return std::find(list.begin(), list.end(), id) != list.end();
		};

	// 1. LÓGICA DE INPUT (PLAYER)
	if (eventName == "InputManager_ActionTriggered") {
		bool isRoll = HasInput(Settings::DodgeRollActionIDs, actionID);
		bool isStep = HasInput(Settings::DodgeStepActionIDs, actionID);
		bool isDash = HasInput(Settings::DashActionIDs, actionID);
		if (isRoll || isStep || isDash) {
			if (const auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([isRoll, isStep, isDash]() { AttemptPlayerDodge(isRoll, isStep, isDash); });
			}
		}
		return RE::BSEventNotifyControl::kContinue;
	}
	else if (eventName == "InputManager_MotionTriggered") {
		bool isRoll = HasInput(Settings::DodgeRollMotionIDs, actionID);
		bool isStep = HasInput(Settings::DodgeStepMotionIDs, actionID);
		bool isDash = HasInput(Settings::DashMotionIDs, actionID);
		if (isRoll || isStep || isDash) {
			if (const auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([isRoll, isStep, isDash]() { AttemptPlayerDodge(isRoll, isStep, isDash); });
			}
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	// 2. LÓGICA DE SENTIDOS (NPC)
	if (eventName.starts_with("NPCSenses_")) {
		// Diferencia explicitamente entre Visão e Área com base na string do Senses Mod
		bool isVision = eventName.find("Vision") != std::string_view::npos;
		bool isArea = eventName.find("Area") != std::string_view::npos;
		bool isEnter = eventName.ends_with("Enter");

		// Se não for nenhum dos dois reconhecidos, sai
		if (!isVision && !isArea) return RE::BSEventNotifyControl::kContinue;

		RE::FormID casterID = 0;
		try {
			std::size_t parsed = 0;
			std::string casterIDStr = a_event->strArg.c_str();
			casterID = static_cast<RE::FormID>(std::stoul(casterIDStr, &parsed, 16));
			if (parsed != casterIDStr.size()) {
				SKSE::log::warn("[NPCSenses] FormID invalido recebido '{}'.", casterIDStr);
				return RE::BSEventNotifyControl::kContinue;
			}
		}
		catch (const std::exception& e) {
			SKSE::log::warn("[NPCSenses] Falha ao converter FormID '{}': {}", a_event->strArg.c_str(), e.what());
			return RE::BSEventNotifyControl::kContinue;
		}
		int depthLevel = static_cast<int>(a_event->numArg);
		RE::Actor* ownerActor = a_event->sender ? a_event->sender->As<RE::Actor>() : nullptr;

		if (ownerActor) {
			if (isEnter) {
				if (!ownerActor->IsPlayerRef() && !NPCDodgeManager::IsNPCDodgeGloballyAllowed(ownerActor)) {
					NPCDodgeManager::ClearNPCDodgeState(ownerActor);
					return RE::BSEventNotifyControl::kContinue;
				}

				VisionTracker::AddTracker(isVision, ownerActor->GetFormID(), casterID, depthLevel);

				if (!ownerActor->IsDead() && !ownerActor->IsPlayerRef()) {
					auto rule = NPCDodgeManager::GetRuleForNPC(ownerActor);

					// Projéteis SEMPRE usam o sentido de Área e a Profundidade 1
					bool validSense = isArea;
					bool validDepth = (depthLevel == 1);

					if (validSense && validDepth) {
						auto casterForm = RE::TESForm::LookupByID(casterID);
						auto proj = casterForm ? casterForm->As<RE::Projectile>() : nullptr;

						// Se o cast funcionou, é uma instância de projétil válida e física no mundo
						if (proj && !proj->IsDisabled() && !proj->IsDeleted()) {

							// Verifica o Line of Sight diretamente contra a instância do projétil
							bool requireLoS = rule ? rule->projRequireLoS : Settings::NPCProjRequireLoS;
							bool hasLoS = true;

							if (requireLoS) {
								bool arg2 = false;
								// proj é aceito aqui porque RE::Projectile herda de RE::TESObjectREFR
								hasLoS = ownerActor->HasLineOfSight(proj, arg2);
							}

							if (hasLoS) {
								const auto& projData = proj->GetProjectileRuntimeData();
								RE::NiPoint3 currentVel;
								proj->GetLinearVelocity(currentVel);

								if (currentVel.SqrLength() > 1.0f) {
									auto shooterHandle = projData.shooter;
									RE::Actor* attacker = shooterHandle.get().get() ? shooterHandle.get().get()->As<RE::Actor>() : nullptr;

									if (attacker != ownerActor) {
										bool canDodgeProj = false;
										auto weaponSource = projData.weaponSource;

										// Busca a configuração da regra em vez de forçar a Global
										int projDodgeConfig = rule ? rule->dodgeProjectiles : Settings::NPCDodgeProjectiles;

										if (projDodgeConfig == 3) {
											canDodgeProj = false; // Desabilitado
										}
										else if (projDodgeConfig == 0) {
											canDodgeProj = true; // Ambos
										}
										else if (weaponSource) {
											bool isArrow = weaponSource->IsWeapon() && (weaponSource->As<RE::TESObjectWEAP>()->IsBow() || weaponSource->As<RE::TESObjectWEAP>()->IsCrossbow());

											if (projDodgeConfig == 2 && isArrow) canDodgeProj = true;
											else if (projDodgeConfig == 1 && !isArrow) canDodgeProj = true;
										}

										if (canDodgeProj) {
											auto [shouldDodge, dodgeType] = NPCDodgeManager::ShouldNPCDodge(ownerActor);
											if (shouldDodge) {
												NPCDodgeManager::AttemptDodge(ownerActor, attacker ? attacker : ownerActor, dodgeType);
											}
										}
									}
								}
							}
						}
					}

					bool dashGapCloseEnabled = rule ? rule->dashGapCloseEnabled : Settings::NPCDashGapCloseEnabled;
					if (dashGapCloseEnabled && depthLevel > 1 && (isArea || isVision)) {
						auto targetForm = RE::TESForm::LookupByID(casterID);
						auto targetActor = targetForm ? targetForm->As<RE::Actor>() : nullptr;
						auto combatTarget = ownerActor->GetActorRuntimeData().currentCombatTarget.get().get();

						if (targetActor && combatTarget == targetActor) {
							NPCDodgeManager::AttemptDashGapClose(ownerActor, targetActor, rule);
						}
					}
				}
			}
			else {
				VisionTracker::RemoveTracker(isVision, ownerActor->GetFormID(), casterID);
			}
		}
	}

	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl NpcCycleSink::ProcessEvent(const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>*)
{
	if (!a_event || !a_event->holder) return RE::BSEventNotifyControl::kContinue;

	auto* actor = a_event->holder->As<RE::Actor>();
	if (!actor || actor->IsDead()) return RE::BSEventNotifyControl::kContinue;
	const RE::FormID formID = actor->GetFormID();
	const std::string_view eventName = a_event->tag;
	auto npc = const_cast<RE::Actor*>(actor);
	bool isPlayer = actor->IsPlayerRef();
	if(!isPlayer) {
		if(eventName == "BF_RollDodgeStart" || eventName == "BF_StepDodgeStart") {
			if (!NPCDodgeManager::IsNPCDodgeGloballyAllowed(npc)) {
				NPCDodgeManager::ClearNPCDodgeState(npc);
				return RE::BSEventNotifyControl::kContinue;
			}
			DodgeTimerManager::StartDodge(actor->GetFormID(), eventName == "BF_RollDodgeStart");
		}
		else if (eventName == "BF_DashStart") {
			if (!NPCDodgeManager::IsNPCDodgeGloballyAllowed(npc)) {
				NPCDodgeManager::ClearNPCDodgeState(npc);
				return RE::BSEventNotifyControl::kContinue;
			}
			DodgeTimerManager::StartDash(actor->GetFormID());
		}
	}
	if (eventName == "BF_DodgeStop") {
		if (isPlayer) {
			DodgeTimerManager::FinishPlayerEvasion(actor);
		}
		else {
			int method = DodgeTimerManager::GetActiveInvulnerabilityMethod(actor);

			// Zera apenas se o método selecionado for Animation Time (1)
			if (method == 1) {
				actor->SetGraphVariableBool("isDodgingCMF", false);
			}
		}
	}
	else if (eventName == "MCO_EndAnimation") {
		bool isPowerAttack = actor->IsPowerAttacking();
		bool cancelEnabled = DodgeTimerManager::CanCancelActiveDodgeWithAttack(actor, isPowerAttack);

		if (cancelEnabled) {
			bool isDodging = false;
			if (actor->GetGraphVariableBool("isDodgingCMF", isDodging) && isDodging) {
				actor->NotifyAnimationGraph("BF_DodgeStop");
			}
		}
	}
	else if (NPCDodgeManager::TryRandomDodgeFromEvent(npc, eventName)) {
		return RE::BSEventNotifyControl::kContinue;
	}
	else if (eventName == "preHitFrame") {
		std::unordered_set<RE::FormID> observers;

		// Coleta apenas os observadores no nível 1 (já que melee agora é forçado a Profundidade 1)
		for (auto id : VisionTracker::GetValidObservers(false, actor->GetFormID(), 1)) observers.insert(id);
		for (auto id : VisionTracker::GetValidObservers(true, actor->GetFormID(), 1)) observers.insert(id);

		for (RE::FormID watcherID : observers) {
			auto* watcherForm = RE::TESForm::LookupByID(watcherID);
			if (!watcherForm) continue;

			auto* targetNpc = watcherForm->As<RE::Actor>();

			if (targetNpc && targetNpc != actor && !targetNpc->IsDead() && !targetNpc->IsPlayerRef()) {
				if (!NPCDodgeManager::IsNPCDodgeGloballyAllowed(targetNpc)) {
					NPCDodgeManager::ClearNPCDodgeState(targetNpc);
					continue;
				}

				auto rule = NPCDodgeManager::GetRuleForNPC(targetNpc);
				bool meleeEnabled = rule ? rule->meleeDodgeEnabled : Settings::NPCMeleeDodgeEnabled;
				if (!meleeEnabled) continue;

				bool useArea = rule ? rule->meleeUseArea : Settings::NPCMeleeUseArea;
				bool valid = false;
				int depth = VisionTracker::GetObserverDepth(useArea ? false : true, watcherID, actor->GetFormID());
				if (depth == 1) valid = true;

				if (valid && useArea) {
					bool requireLoS = rule ? rule->meleeRequireLoS : Settings::NPCMeleeRequireLoS;
					if (requireLoS) {
						bool arg2 = false;
						valid = targetNpc->HasLineOfSight(actor, arg2);
					}
				}

				if (!valid) continue;

				auto [shouldDodge, dodgeType] = NPCDodgeManager::ShouldNPCDodge(targetNpc);
				if (shouldDodge) {
					NPCDodgeManager::AttemptDodge(targetNpc, actor, rule);
				}
			}
		}
	}

	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl NpcCombatTracker::ProcessEvent(const RE::TESCombatEvent* a_event, RE::BSTEventSource<RE::TESCombatEvent>*)
{
	if (!a_event || !a_event->actor) {
		return RE::BSEventNotifyControl::kContinue;
	}

	auto actor = a_event->actor.get();
	auto* npc = actor->As<RE::Actor>();
	if (npc && npc != RE::PlayerCharacter::GetSingleton()) {
		switch (a_event->newState.get()) {
		case RE::ACTOR_COMBAT_STATE::kCombat:
			NpcCombatTracker::RegisterSink(npc);
			break;
		case RE::ACTOR_COMBAT_STATE::kNone:
			NpcCombatTracker::UnregisterSink(npc);
			break;
		}
	}
	return RE::BSEventNotifyControl::kContinue;
}

void NpcCombatTracker::RegisterSink(RE::Actor* a_actor)
{
	std::unique_lock lock(g_mutex);
	if (g_trackedNPCs.find(a_actor->GetFormID()) == g_trackedNPCs.end()) {
		a_actor->AddAnimationGraphEventSink(&g_npcSink);
		g_trackedNPCs.insert(a_actor->GetFormID());
	}
}

void NpcCombatTracker::UnregisterSink(RE::Actor* a_actor)
{
	if (!a_actor || a_actor->IsPlayerRef()) return;

	std::unique_lock lock(g_mutex);
	if (g_trackedNPCs.find(a_actor->GetFormID()) != g_trackedNPCs.end()) {
		a_actor->RemoveAnimationGraphEventSink(&g_npcSink);
		g_trackedNPCs.erase(a_actor->GetFormID());
	}
}

void NpcCombatTracker::RegisterSinksForExistingCombatants()
{
	auto* processLists = RE::ProcessLists::GetSingleton();
	if (!processLists) {
		SKSE::log::warn("[NpcCombatTracker] Não foi possível obter ProcessLists.");
		return;
	}

	// Itera sobre todos os atores que estão "ativos" no jogo
	for (auto& actorHandle : processLists->highActorHandles) {
		if (auto actor = actorHandle.get().get()) {
			// A função IsInCombat() nos diz se o ator já está em um estado de combate
			if (!actor->IsPlayerRef()) {
				if (actor->IsInCombat()) {
					SKSE::log::info("[NpcCombatTracker] Ator '{}' ({:08X}) já está em combate. Registrando sink...",
						actor->GetName(), actor->GetFormID());
					// Usamos a mesma função de registro que já existe!
					RegisterSink(actor);
				}
			}

		}
	}

	SKSE::log::info("[NpcCombatTracker] Verificação concluída.");
}

void ScheduleSinkRegistration(RE::Actor* actor, int attempts)
{
	if (attempts > 20) {
		SKSE::log::critical("[Actor3DLoadEventHandler] Desistindo após {} tentativas para o ator {:08X}.", attempts, actor->GetFormID());
		return;
	}

	auto actorHandle = actor->CreateRefHandle();

	Utils::DelayedDispatcher::Get().PostDelayed(std::chrono::milliseconds(100), [actorHandle, attempts]() {
		SKSE::GetTaskInterface()->AddTask([actorHandle, attempts]() {
			if (!actorHandle) return;
			if (!actorHandle.get()) return;

			auto actor = actorHandle.get();

			RE::BSTSmartPointer<RE::BSAnimationGraphManager> graphManager;
			actor->GetAnimationGraphManager(graphManager);

			if (graphManager) {


				if (actor->IsPlayerRef()) {
					actor->RemoveAnimationGraphEventSink(NpcCycleSink::GetSingleton());
					if (actor->AddAnimationGraphEventSink(NpcCycleSink::GetSingleton())) {
						SKSE::log::info("[Actor3DLoadEventHandler] Sink do PLAYER reconectada com sucesso.");
					}

				}
				else {
					NpcCombatTracker::UnregisterSink(actor.get());
					NpcCombatTracker::RegisterSink(actor.get());
					SKSE::log::info("[Actor3DLoadEventHandler] Sink de NPC reconectada (via CombatTracker).");
				}
			}
			else {
				// Graph ainda nulo, tenta de novo
				ScheduleSinkRegistration(actor.get(), attempts + 1);
			}
			});
		});
}

RE::BSEventNotifyControl PC3DLoadEventHandler::ProcessEvent(const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*)
{
	if (!a_event || !a_event->loaded) {
		return RE::BSEventNotifyControl::kContinue;
	}

	// Em vez de pegar o Player Singleton, buscamos o formulário pelo ID do evento
	auto* form = RE::TESForm::LookupByID(a_event->formID);
	if (!form) return RE::BSEventNotifyControl::kContinue;

	// Tentamos converter para Ator. Se não for ator (ex: uma parede), ignoramos.
	auto* actor = form->As<RE::Actor>();

	if (actor) {
		ScheduleSinkRegistration(actor, 0);
	}

	return RE::BSEventNotifyControl::kContinue;
}

