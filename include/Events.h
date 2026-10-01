#pragma once
#include <shared_mutex>
#include <unordered_map>
#include <chrono>
#include <unordered_set>
#include <random>
#include <vector>

namespace DodgeTimerManager {
	enum class DodgeState {
		None,
		Perfect,
		Invulnerable
	};

	void StartDodge(RE::FormID a_formID, bool a_isRoll = true);
	void StartDash(RE::FormID a_formID);
	DodgeState GetDodgeState(RE::FormID a_formID);
	void RemoveWindow(RE::FormID a_formID);
	void CleanupExpiredWindows();
	bool TryTriggerPerfectDodge(RE::Actor* a_actor); // Avalia e dispara o TimeStop se for o Player
	void UpdatePlayerMovementIntent(bool a_hasMovement);
	void FinishPlayerEvasion(RE::Actor* a_actor);
}

namespace NPCDodgeManager {
	bool IsNPCDodgeGloballyAllowed(RE::Actor* a_actor);
	void ClearNPCDodgeState(RE::Actor* a_actor);
}

namespace Idles {
	inline RE::TESIdleForm* DodgeRoll = nullptr;
	inline RE::TESIdleForm* DodgeStop = nullptr;
	inline RE::TESIdleForm* GetIdleByFormID(RE::FormID formID, const std::string& pluginName);
	void InitIdles();
	inline void PlayIdleAnimation(RE::Actor* actor, RE::TESIdleForm* idle);
}

class DodgeListener : public RE::BSTEventSink<SKSE::ModCallbackEvent> {
public:
	static DodgeListener* GetSingleton() {
		static DodgeListener singleton;
		return &singleton;
	}

	void Register() {
		auto dispatcher = SKSE::GetModCallbackEventSource();
		if (dispatcher) dispatcher->AddEventSink(this);
	}

	RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event, RE::BSTEventSource<SKSE::ModCallbackEvent>*) override;
};

class PlayerMovementInputListener : public RE::BSTEventSink<RE::InputEvent*> {
public:
	static PlayerMovementInputListener* GetSingleton() {
		static PlayerMovementInputListener singleton;
		return &singleton;
	}

	RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event,
		RE::BSTEventSource<RE::InputEvent*>*) override;
	void Reset();

private:
	void DispatchMovementIntent();

	bool forwardPressed = false;
	bool backPressed = false;
	bool leftPressed = false;
	bool rightPressed = false;
	bool leftStickMoving = false;
	bool lastMovementIntent = false;
};

class NpcCycleSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent> {
public:
	static NpcCycleSink* GetSingleton() {
		static NpcCycleSink singleton;
		return &singleton;
	}

	RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event,
		RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override;
};

class NpcCombatTracker : public RE::BSTEventSink<RE::TESCombatEvent> {
public:
	static NpcCombatTracker* GetSingleton() {
		static NpcCombatTracker singleton;
		return &singleton;
	}

	// Função chamada quando um evento de combate ocorre
	RE::BSEventNotifyControl ProcessEvent(const RE::TESCombatEvent* a_event,
		RE::BSTEventSource<RE::TESCombatEvent>*) override;

	static void RegisterSink(RE::Actor* a_actor);
	static void UnregisterSink(RE::Actor* a_actor);

	static void RegisterSinksForExistingCombatants();

private:
	// Instância compartilhada do nosso processador de lógica
	inline static NpcCycleSink g_npcSink;

	// Guarda os FormIDs dos NPCs que já estamos ouvindo
	inline static std::set<RE::FormID> g_trackedNPCs;
	inline static std::shared_mutex g_mutex;
};

class PC3DLoadEventHandler : public RE::BSTEventSink<RE::TESObjectLoadedEvent> {
public:
	static PC3DLoadEventHandler* GetSingleton() {
		static PC3DLoadEventHandler singleton;
		return &singleton;
	}

	RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override;
};

