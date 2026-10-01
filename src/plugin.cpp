#include "logger.h"
#include "Settings.h"
#include "Hooks.h"
#include "Manager.h"
#include "Serialization.h"
#include "Utils.h"
#include "FFC_API.h"
#include "DodgeFallback.h"

namespace TimeStop { void ResetSession(); }
namespace DodgeTimerManager { void ResetAll(); }

namespace Hook_Precision { void Initialize(); }
namespace {
    bool hasDFG = false;
    bool dataLoaded = false;
    bool inputManagerInitialized = false;

    void InitializeInputManagerIntegration() {
        if (!dataLoaded || inputManagerInitialized || !InputManagerAPI::_API) return;

        DodgeModMenu::EnsureDefaultInputs();
        DodgeModMenu::RegisterAllInputs();
        DodgeModMenu::TweenPauseRegister();
        inputManagerInitialized = true;
    }
}

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == InputManagerAPI::kMessage_ProvideAPI) {
        InputManagerAPI::ReceiveAPI(message);
        logger::info("API do Input Manager recebida com sucesso!");
        InitializeInputManagerIntegration();
    }
    if (message->type == FFC_API::kMessage_ProvideAPI) {
        FFC_API::ReceiveAPI(message);
        logger::info("API do ApplyImpulse (FFC) recebida com sucesso via mensagem!");
    }
    if (message->type == SKSE::MessagingInterface::kPostLoad) {
        hasDFG = GetModuleHandleA("DynamicFormsGenerator.dll") != nullptr;
        if (hasDFG) {
            logger::info("DynamicFormsGenerator.dll found");
        }
    }
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        DodgeModMenu::Register();
        dataLoaded = true;
        InputManagerAPI::RequestAPIDirect();
        FFC_API::RequestAPIDirect();
        RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink(PC3DLoadEventHandler::GetSingleton());
        if (auto* inputDeviceManager = RE::BSInputDeviceManager::GetSingleton()) {
            inputDeviceManager->AddEventSink(PlayerMovementInputListener::GetSingleton());
        }
        Hook_OnMeleeHit::install();
        Hook_OnProjectileCollision::install();
        Hook_Precision::Initialize();
        InitializeInputManagerIntegration();
        DodgeFallback::Init();
        if (FFC_API::_API) {
            logger::info("ApplyCustomVelocityImpulse linkado e pronto para uso.");
        }
        if (!hasDFG) {
            Manager::GetSingleton()->PopulateAllLists();
        }
       // hooks::Install();
    }
    if (message->type == SKSE::MessagingInterface::kNewGame || message->type == SKSE::MessagingInterface::kPostLoadGame) {
        TimeStop::ResetSession();
        DodgeTimerManager::ResetAll();
        PlayerMovementInputListener::GetSingleton()->Reset();
        RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink(NpcCombatTracker::GetSingleton());
        auto player = RE::PlayerCharacter::GetSingleton();
        player->SetGraphVariableBool("isDodgingCMF", false);
        player->SetGraphVariableBool("isPerfDodgingCMF", false);

        NpcCombatTracker::RegisterSinksForExistingCombatants();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {

    SetupLog();
    logger::info("Plugin loaded");
    SKSE::Init(skse);
    auto serialization = SKSE::GetSerializationInterface();
    serialization->SetUniqueID(Tracking::Serialization::kSerializationID);
    serialization->SetSaveCallback(Tracking::Serialization::SaveCallback);
    serialization->SetLoadCallback(Tracking::Serialization::LoadCallback);
    serialization->SetRevertCallback(Tracking::Serialization::RevertCallback);
    DodgeListener::GetSingleton()->Register();
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    return true;
}
