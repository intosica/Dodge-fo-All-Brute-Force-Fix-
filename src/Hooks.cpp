#include "Hooks.h"
#include "Settings.h"
#include "Serialization.h"
#include "PrecisionAPI.h"

struct NPCDodgeCapability {
	bool meleeEnabled = true;
	int projectileMode = 0; // 0 = Ambos, 1 = Só Magia, 2 = Só Flechas, 3 = Desativado
};

static NPCDodgeCapability GetNPCDodgeCapability(RE::Actor* a_actor)
{
	NPCDodgeCapability cap;

	if (!NPCDodgeManager::IsNPCDodgeGloballyAllowed(a_actor)) {
		NPCDodgeManager::ClearNPCDodgeState(a_actor);
		cap.meleeEnabled = false;
		cap.projectileMode = 3; // Desativado
		return cap;
	}

	// Verificação por Regra Específica: Procura se o NPC possui algum Perk registrado
	for (const auto& rule : Settings::NPCRules) {
		if (rule.perkID != 0) {
			auto perkForm = RE::TESForm::LookupByID(rule.perkID);
			if (perkForm && perkForm->GetFormType() == RE::FormType::Perk) {
				auto perk = perkForm->As<RE::BGSPerk>();
				if (perk && a_actor->HasPerk(perk)) {
					cap.meleeEnabled = rule.meleeDodgeEnabled;
					cap.projectileMode = rule.dodgeProjectiles;
					return cap;
				}
			}
		}
	}

	// Fallback: Se nenhuma regra por Perk foi encontrada, usa as configurações globais
	cap.meleeEnabled = Settings::NPCMeleeDodgeEnabled;
	cap.projectileMode = Settings::NPCDodgeProjectiles;
	return cap;
}

void Hook_OnMeleeHit::processHit(RE::Actor * victim, RE::HitData & hitData)
{
	if (!victim) {
		_ProcessHit(victim, hitData);
		return;
	}

	auto aggressor = hitData.aggressor.get().get();
	bool isUndodgeable = false;
	if (aggressor) {
		aggressor->GetGraphVariableBool("isUndodgeableHit", isUndodgeable);
	}

	if (!aggressor || isUndodgeable) {
		_ProcessHit(victim, hitData);
		return;
	}

	auto dodgeState = DodgeTimerManager::GetDodgeState(victim->GetFormID());

	if (dodgeState != DodgeTimerManager::DodgeState::None) {

		if (!victim->IsPlayerRef()) {
			auto cap = GetNPCDodgeCapability(victim);
			if (!cap.meleeEnabled) {
				_ProcessHit(victim, hitData);
				return;
			}
		}

		const char* dodgedEvent = (dodgeState == DodgeTimerManager::DodgeState::Perfect) ? "PerfDodgedCMF" : "DodgedCMF";
		const char* gotDodgedEvent = (dodgeState == DodgeTimerManager::DodgeState::Perfect) ? "GotPerfDodgeCMF" : "GotDodgedCMF";

		// Envia as notificações para as árvores de animação do defensor e do atacante
		victim->NotifyAnimationGraph(dodgedEvent);
		if (aggressor) {
			aggressor->NotifyAnimationGraph(gotDodgedEvent);
		}

		// Se for Perfect Dodge, aciona o Time Stop (apenas para o Player, tratado internamente)
		if (victim->IsPlayerRef()) {
			if (dodgeState == DodgeTimerManager::DodgeState::Perfect) {
				DodgeTimerManager::TryTriggerPerfectDodge(victim);
			}
			else {
				Tracking::RecordNormalDodge(victim);
			}
		}

		// O alvo está invulnerável (I-Frame ou Perfect Dodge).
		// Zeramos o dano, o stagger e retornamos imediatamente.
		// O _ProcessHit NÃO é chamado, anulando o ataque mesmo se for "Undodgeable".
		hitData.totalDamage = 0.0f;
		hitData.physicalDamage = 0.0f;
		hitData.stagger = 0;
		return;
	}

	_ProcessHit(victim, hitData);
}

namespace Hook_Precision
{
	static PRECISION_API::PreHitCallbackReturn PreHitCallback(const PRECISION_API::PrecisionHitData& a_hitData)
	{
		PRECISION_API::PreHitCallbackReturn ret;

		// Garante que o alvo atingido existe e é um Actor
		if (!a_hitData.target || a_hitData.target->formType != RE::FormType::ActorCharacter) {
			return ret;
		}

		auto victim = const_cast<RE::TESObjectREFR*>(a_hitData.target)->As<RE::Actor>();
		if (!victim) {
			return ret;
		}

		auto aggressor = a_hitData.attacker;
		bool isUndodgeable = false;
		if (aggressor) {
			aggressor->GetGraphVariableBool("isUndodgeableHit", isUndodgeable);
		}

		// Se o golpe for indefensável, não aplica a lógica de esquiva e deixa o Precision processar o hit
		if (isUndodgeable) {
			return ret;
		}

		// Verifica o estado da esquiva da vítima
		auto dodgeState = DodgeTimerManager::GetDodgeState(victim->GetFormID());

		if (dodgeState != DodgeTimerManager::DodgeState::None) {
			if (!victim->IsPlayerRef()) {
				auto cap = GetNPCDodgeCapability(victim);
				if (!cap.meleeEnabled) {
					return ret;
				}
			}

			const char* dodgedEvent = (dodgeState == DodgeTimerManager::DodgeState::Perfect) ? "PerfDodgedCMF" : "DodgedCMF";
			const char* gotDodgedEvent = (dodgeState == DodgeTimerManager::DodgeState::Perfect) ? "GotPerfDodgeCMF" : "GotDodgedCMF";

			// Envia as notificações para as árvores de animação do defensor e do atacante
			victim->NotifyAnimationGraph(dodgedEvent);
			if (aggressor) {
				aggressor->NotifyAnimationGraph(gotDodgedEvent);
			}

			if (victim->IsPlayerRef()) {
				if (dodgeState == DodgeTimerManager::DodgeState::Perfect) {
					DodgeTimerManager::TryTriggerPerfectDodge(victim);
				}
				else {
					Tracking::RecordNormalDodge(victim);
				}
			}

			// Informa ao Precision para ignorar completamente este impacto (anula o dano e a colisão física)
			ret.bIgnoreHit = true;
		}

		return ret;
	}

	void Initialize()
	{
		// Solicita a interface do Precision
		auto precisionAPI = static_cast<PRECISION_API::IVPrecision4*>(PRECISION_API::RequestPluginAPI(PRECISION_API::InterfaceVersion::V4));
		if (precisionAPI) {
			// Registra o callback usando o PluginHandle do seu mod obtido pelo SKSE
			precisionAPI->AddPreHitCallback(SKSE::GetPluginHandle(), PreHitCallback);
			SKSE::log::info("Precision API: PreHitCallback registrado com sucesso!");
		}
		else {
			SKSE::log::info("Precision API nao encontrada ou versao incompativel. Usando apenas hooks nativos.");
		}
	}
}

bool processProjectileBlock(RE::Actor* a_blocker, RE::Projectile* a_projectile, RE::hkpCollidable* a_projectile_collidable)
{
	auto shooterHandle = a_projectile->GetProjectileRuntimeData().shooter;
	auto shooter = shooterHandle.get().get() ? shooterHandle.get().get()->As<RE::Actor>() : nullptr;
	bool isUndodgeable = false;
	bool isArrow = !a_projectile->GetProjectileRuntimeData().spell;
	if (shooter) {
		shooter->GetGraphVariableBool("isUndodgeableHit", isUndodgeable);
		if (isUndodgeable) {
			return false;
		}
	}
	auto dodgeState = DodgeTimerManager::GetDodgeState(a_blocker->GetFormID());
	if (dodgeState == DodgeTimerManager::DodgeState::None) {
		return false;
	}
	else {
		if (!a_blocker->IsPlayerRef()) {
			auto cap = GetNPCDodgeCapability(a_blocker);

			if (cap.projectileMode == 3) { // 3 = Disabled
				return false;
			}
			if (isArrow && cap.projectileMode == 1) { // 1 = Only Magic (Bloqueia se for flecha)
				return false;
			}
			if (!isArrow && cap.projectileMode == 2) { // 2 = Only Arrows (Bloqueia se for magia)
				return false;
			}
		}

		const char* dodgedEvent = (dodgeState == DodgeTimerManager::DodgeState::Perfect) ? "PerfDodgedCMF" : "DodgedCMF";
		const char* gotDodgedEvent = (dodgeState == DodgeTimerManager::DodgeState::Perfect) ? "GotPerfDodgeCMF" : "GotDodgedCMF";

		// Aplica a reação e o feedback visual de desvio ao Blocker e ao Shooter original
		a_blocker->NotifyAnimationGraph(dodgedEvent);
		if (shooter) {
			shooter->NotifyAnimationGraph(gotDodgedEvent);
		}

		if (a_blocker->IsPlayerRef()) {
			if (dodgeState == DodgeTimerManager::DodgeState::Perfect) {
				DodgeTimerManager::TryTriggerPerfectDodge(a_blocker);
			}
			else {
				Tracking::RecordNormalDodge(a_blocker);
			}
		}
	}

	RE::Offset::destroyProjectile(a_projectile);
	return true;
}

inline bool shouldIgnoreHit(RE::Projectile* a_projectile, RE::hkpAllCdPointCollector* a_AllCdPointCollector)
{
	if (a_AllCdPointCollector) {
		for (auto& hit : a_AllCdPointCollector->hits) {
			auto refrA = RE::TESHavokUtilities::FindCollidableRef(*hit.rootCollidableA);
			auto refrB = RE::TESHavokUtilities::FindCollidableRef(*hit.rootCollidableB);
			RE::Actor* target = nullptr;
			RE::hkpCollidable* projectileCollidable = nullptr;

			if (refrA && refrA->formType == RE::FormType::ActorCharacter) {
				target = refrA->As<RE::Actor>();
				projectileCollidable = const_cast<RE::hkpCollidable*>(hit.rootCollidableB);
			}
			else if (refrB && refrB->formType == RE::FormType::ActorCharacter) {
				target = refrB->As<RE::Actor>();
				projectileCollidable = const_cast<RE::hkpCollidable*>(hit.rootCollidableA);
			}

			if (target) {
				if (processProjectileBlock(target, a_projectile, projectileCollidable)) {
					return true;
				}
			}
			else {
			}
		}
	}
	return false;
}

void Hook_OnProjectileCollision::OnArrowCollision(RE::Projectile* a_this, RE::hkpAllCdPointCollector* a_AllCdPointCollector)
{
	if (shouldIgnoreHit(a_this, a_AllCdPointCollector)) {
		return;
	}
	_arrowCollission(a_this, a_AllCdPointCollector);
}

void Hook_OnProjectileCollision::OnMissileCollision(RE::Projectile* a_this, RE::hkpAllCdPointCollector* a_AllCdPointCollector)
{

	if (a_this && (a_this->GetProjectileRuntimeData().spell || a_this->GetProjectileBase())) {
		if (shouldIgnoreHit(a_this, a_AllCdPointCollector)) {
			return;
		}
	}

	_missileCollission(a_this, a_AllCdPointCollector);
}
