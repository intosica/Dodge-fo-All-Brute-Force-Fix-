#include "InputManagerAPI.h"
#include "SKSEMCP/SKSEMenuFramework.hpp"
#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/writer.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <map>
#include <string>
// Miniz (Zip)
#include <miniz.h>
namespace Settings {
    enum class DodgeValueSource : std::uint8_t {
        kFixed = 0,
        kActorValue = 1,
        kGlobal = 2
    };

    struct DodgeTypeSettings {
        bool invulnerabilityEnabled = true;
        int invulnerabilityMethod = 0;
        float invincibilityDuration = 0.5f;
        RE::FormID invincibilityDurationGlobal = 0;
        bool enablePerfectDodge = true;
        float perfectDodgeWindow = 0.2f;
        bool stopTimeOnPerfectDodge = false;
        float timeStopDuration = 0.5f;
        RE::FormID timeStopDurationGlobal = 0;
        bool cancelDodgeWithAttacks = true;
        bool cancelDodgeWithPowerAttacks = true;
        bool cancelAttackWithDodge = false;
        bool canSpamDodge = false;
        float cost = 15.0f;
        int costType = 1;
        RE::FormID costGlobal = 0;
        DodgeValueSource invincibilityDurationSource = DodgeValueSource::kFixed;
        std::string invincibilityDurationActorValue = "Stamina";
        RE::FormID perfectDodgeWindowGlobal = 0;
        DodgeValueSource perfectDodgeWindowSource = DodgeValueSource::kFixed;
        std::string perfectDodgeWindowActorValue = "Stamina";
        DodgeValueSource timeStopDurationSource = DodgeValueSource::kFixed;
        std::string timeStopDurationActorValue = "Stamina";
        DodgeValueSource costSource = DodgeValueSource::kFixed;
        std::string costActorValue = "Stamina";
        bool cancelDodgeWithMovement = true;
        int movementLockMethod = 0;
        float movementLockDuration = 0.3f;
        RE::FormID movementLockDurationGlobal = 0;
        DodgeValueSource movementLockDurationSource = DodgeValueSource::kFixed;
        std::string movementLockDurationActorValue = "Stamina";
    };

    // --- Novos inputs baseados em Vetores ---
    inline std::vector<int> DodgeRollActionIDs;
    inline std::vector<int> DodgeRollMotionIDs;
    inline std::vector<int> DodgeStepActionIDs;
    inline std::vector<int> DodgeStepMotionIDs;
    inline std::vector<int> DashActionIDs;
    inline std::vector<int> DashMotionIDs;
    inline bool DefaultInputsInitialized = false;

    inline float PerfectDodgeWindow = 0.2f;
    inline float DodgeInvincibilityDuration = 0.5f;
    inline int DodgeInvulnerabilityMethod = 0;
    inline float DodgeRollCost = 20.0f;
    inline int DodgeRollTypeCost = 1;
    inline float DodgeStepCost = 15.0f;
    inline int DodgeStepTypeCost = 1;
    inline bool DodgeRollEnabled = true;
    inline bool DodgeStepEnabled = true;
    inline bool DashEnabled = true;
    inline bool CanSpamDodge = false;
    inline bool EnablePerfectDodge = true;
    inline bool StopTimeOnPerfectDodge = true;
    inline float TimeStopDuration = 0.5f;
    inline bool CancelDodgeWithAttacks = true;
    inline bool CancelAttackWithDodge = false;
    inline bool DodgeRollUpwards = false;
    inline float DodgeRollAerialBoost = 25.0f;
    inline bool DodgeStepUpwards = false;
    inline float DodgeStepAerialBoost = 25.0f;
    inline bool RollOnlyLightArmor = false;
    inline bool CanDodgeSheathed = true;
    inline bool CancelDodgeWithPowerAttacks = true;
    inline bool PlayerUseSeparateSheathedDistance = false;

    inline float DashCost = 15.0f;
    inline int DashTypeCost = 1;
    inline bool DashInvulnerabilityEnabled = true;
    inline int DashInvulnerabilityMethod = 0;
    inline float DashInvincibilityDuration = 0.35f;

    inline DodgeTypeSettings PlayerRollSettings{
        true, 0, 0.5f, 0, true, 0.2f, true, 0.5f, 0, true, true, false, false, 20.0f, 1, 0
    };
    inline DodgeTypeSettings PlayerStepSettings{
        true, 0, 0.5f, 0, true, 0.2f, true, 0.5f, 0, true, true, false, false, 15.0f, 1, 0
    };
    inline DodgeTypeSettings PlayerDashSettings{
        true, 0, 0.35f, 0, false, 0.2f, false, 0.5f, 0, true, true, false, false, 15.0f, 1, 0
    };

    inline RE::FormID PerkCancelDodgeWithPowerAttacks = 0;
    inline RE::FormID PerkCanDodgeSheathed = 0;
    inline RE::FormID PerkStepDodge = 0;
    inline RE::FormID PerkRollDodge = 0;
    inline RE::FormID PerkRollUpwards = 0;
    inline RE::FormID PerkStepUpwards = 0;
    inline RE::FormID PerkCanSpamDodge = 0;
    inline RE::FormID PerkEnablePerfectDodge = 0;
    inline RE::FormID PerkCancelDodgeWithAttacks = 0;
    inline RE::FormID PerkCancelAttackWithDodge = 0;
    inline RE::FormID PerkStopTimeOnPerfectDodge = 0;

    struct ChanceSettings {
        float baseChance = 20.0f;
        float healthMult = 0.5f;
        float aggressionMult = 1.0f;
        float skillMult = 1.0f;
        float maxStaminaMult = 0.1f;
        float weightMult = -0.05f;    
        float equippedWeightMult = -0.05f;
        float encumbranceMult = -0.1f;  
        float globalDifficulty = 100.0f;
    };

    struct DistanceSettings {
        int mode = 0; // 0 = Animation, 1 = Simple, 2 = Complex
        float duration = 0.2f;

        // Modo Simples
        float simpleForwardBack = 150.0f;
        float simpleLeftRight = 150.0f;

        // Modo Complexo (0 = Neutral, 1 até 8 = Direções do Relógio)
        float complexX[9] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        float complexY[9] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        float complexZ[9] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    };

    inline DistanceSettings PlayerRollDistance;
    inline DistanceSettings PlayerStepDistance;
    inline DistanceSettings PlayerRollSheathedDistance;
    inline DistanceSettings PlayerStepSheathedDistance;
    inline DistanceSettings PlayerDashDistance;
    inline DistanceSettings PlayerDashSheathedDistance;

    struct NPCDodgeRule {
        std::string ruleName = "New Rule";
        RE::FormID perkID = 0;
        ChanceSettings chance;
        ChanceSettings dashChance;
        DistanceSettings distance; // Legacy fallback for old rule files.
        DistanceSettings rollDistance;
        DistanceSettings stepDistance;
        DistanceSettings dashDistance;
        
        int dodgeProjectiles = 0; 
        bool projRequireLoS = true;

        bool meleeDodgeEnabled = true; 
        bool meleeUseArea = false;
        bool meleeRequireLoS = false;
        bool dashGapCloseEnabled = false;

        int dodgeType = 0;
        int invulnerabilityMethod = 0;
        float invincibilityDuration = 0.5f;
        bool cancelDodgeWithAttacks = true;
        bool cancelAttackWithDodge = false;
        bool canSpamDodge = false;
        bool allowStaggerDodge = false;
        bool rollOnlyLightArmor = false;
        float rollCost = 15.0f;
        int rollTypeCost = 1;
        float stepCost = 10.0f;
        int stepTypeCost = 1;
        bool smartDodge = true;
        bool cancelDodgeWithPowerAttacks = true;
        bool dashInvulnerabilityEnabled = true;
        int dashInvulnerabilityMethod = 0;
        float dashInvincibilityDuration = 0.35f;
        float dashCost = 10.0f;
        int dashTypeCost = 1;
        DodgeTypeSettings rollSettings{ true, 0, 0.5f, 0, true, 0.2f, false, 0.5f, 0, true, true, false, false, 15.0f, 1, 0 };
        DodgeTypeSettings stepSettings{ true, 0, 0.5f, 0, true, 0.2f, false, 0.5f, 0, true, true, false, false, 10.0f, 1, 0 };
        DodgeTypeSettings dashSettings{ true, 0, 0.35f, 0, false, 0.2f, false, 0.5f, 0, true, true, false, false, 10.0f, 1, 0 };

        bool randomDodgeEnabled = false;
        float randomDodgeChance = 10.0f;
        bool randomDodgeUseArea = false;
        bool randomDodgeRequireLoS = false;
        std::vector<std::string> randomDodgeEvents{ "preHitFrame" };
    };

    // --- Configurações de NPC ---
    inline bool NPCDodgeEnabled = true;
    inline bool NPCRollOnlyLightArmor = false;
    inline bool NPCSmartDodge = true;
    inline ChanceSettings NPCGlobalChance;
    inline ChanceSettings NPCGlobalDashChance;
    inline DistanceSettings NPCGlobalDistance; // Legacy fallback for old settings.
    inline DistanceSettings NPCGlobalRollDistance;
    inline DistanceSettings NPCGlobalStepDistance;
    inline DistanceSettings NPCGlobalDashDistance;

    inline int NPCDodgeProjectiles = 0;
    inline bool NPCProjRequireLoS = true;

    inline bool NPCMeleeDodgeEnabled = true; 
    inline bool NPCMeleeRequireLoS = false;
    inline bool NPCMeleeUseArea = false;
    inline bool NPCDashGapCloseEnabled = false;

    inline bool NPCRandomDodgeEnabled = false;
    inline float NPCRandomDodgeChance = 10.0f;
    inline bool NPCRandomDodgeUseArea = false;
    inline bool NPCRandomDodgeRequireLoS = false;
    inline std::vector<std::string> NPCRandomDodgeEvents{ "preHitFrame" };

    inline int NPCDodgeType = 0;

    inline int NPCInvulnerabilityMethod = 0;
    inline float NPCInvincibilityDuration = 0.5f;
    inline bool NPCCancelDodgeWithAttacks = true;
    inline bool NPCCancelAttackWithDodge = false;
    inline bool NPCCanSpamDodge = false;
    inline bool NPCAllowStaggerDodge = false;
    inline bool NPCCancelDodgeWithPowerAttacks = true;
    inline bool NPCDashInvulnerabilityEnabled = true;
    inline int NPCDashInvulnerabilityMethod = 0;
    inline float NPCDashInvincibilityDuration = 0.35f;
    inline float NPCRollCost = 15.0f;
    inline int NPCRollTypeCost = 1;
    inline float NPCStepCost = 10.0f;
    inline int NPCStepTypeCost = 1;
    inline float NPCDashCost = 10.0f;
    inline int NPCDashTypeCost = 1;
    inline DodgeTypeSettings NPCRollSettings{
        true, 0, 0.5f, 0, true, 0.2f, false, 0.5f, 0, true, true, false, false, 15.0f, 1, 0
    };
    inline DodgeTypeSettings NPCStepSettings{
        true, 0, 0.5f, 0, true, 0.2f, false, 0.5f, 0, true, true, false, false, 10.0f, 1, 0
    };
    inline DodgeTypeSettings NPCDashSettings{
        true, 0, 0.35f, 0, false, 0.2f, false, 0.5f, 0, true, true, false, false, 10.0f, 1, 0
    };
    inline RE::FormID NPCDisableDodgePerk = 0;

    inline std::vector<NPCDodgeRule> NPCRules;
}
namespace DodgeModMenu {
    inline const char* actionStateNames[] = { "Ignore", "Tap", "Hold", "Gesture", "Press" };
    constexpr uint32_t MOUSE_OFFSET = 256;
    constexpr uint32_t GAMEPAD_OFFSET = 266;

    inline const char* pcKeyNames[] = {
      "None",
      // Mouse
      "Mouse 1 (Left)", "Mouse 2 (Right)", "Mouse 3 (Middle)", "Mouse 4", "Mouse 5", "Mouse 6", "Mouse 7", "Mouse 8",
      "Mouse Wheel Up", "Mouse Wheel Down",
      // Alfabeto
      "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
      // Números
      "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
      // Pontuação e Símbolos
      "Minus ( - )", "Equals ( = )", "Bracket Left ( [ )", "Bracket Right ( ] )", "Semicolon ( ; )", "Apostrophe ( ' )", "Tilde ( ~ )", "Backslash ( \\ )", "Comma ( , )", "Period ( . )", "Slash ( / )",
      // F-Keys
      "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
      // Especiais e Modificadores
      "Esc", "Tab", "Caps Lock", "Shift (Left)", "Shift (Right)", "Ctrl (Left)", "Ctrl (Right)", "Alt (Left)", "Alt (Right)",
      "Space", "Enter", "Backspace", "Print Screen", "Scroll Lock", "Pause", "Num Lock",
      // Navegação
      "Up Arrow", "Down Arrow", "Left Arrow", "Right Arrow", "Insert", "Delete", "Home", "End", "Page Up", "Page Down",
      // Numpad
      "Num 0", "Num 1", "Num 2", "Num 3", "Num 4", "Num 5", "Num 6", "Num 7", "Num 8", "Num 9",
      "Num +", "Num -", "Num *", "Num /", "Num Enter", "Num Dot"
    };

    inline const int pcKeyIDs[] = {
        0,
        // Mouse (Base + 256)
        RE::BSWin32MouseDevice::Keys::kLeftButton + MOUSE_OFFSET, RE::BSWin32MouseDevice::Keys::kRightButton + MOUSE_OFFSET,
        RE::BSWin32MouseDevice::Keys::kMiddleButton + MOUSE_OFFSET, RE::BSWin32MouseDevice::Keys::kButton3 + MOUSE_OFFSET,
        RE::BSWin32MouseDevice::Keys::kButton4 + MOUSE_OFFSET, RE::BSWin32MouseDevice::Keys::kButton5 + MOUSE_OFFSET,
        RE::BSWin32MouseDevice::Keys::kButton6 + MOUSE_OFFSET, RE::BSWin32MouseDevice::Keys::kButton7 + MOUSE_OFFSET,
        RE::BSWin32MouseDevice::Keys::kWheelUp + MOUSE_OFFSET, RE::BSWin32MouseDevice::Keys::kWheelDown + MOUSE_OFFSET,
        // Alfabeto
        RE::BSKeyboardDevice::Keys::kA, RE::BSKeyboardDevice::Keys::kB, RE::BSKeyboardDevice::Keys::kC, RE::BSKeyboardDevice::Keys::kD,
        RE::BSKeyboardDevice::Keys::kE, RE::BSKeyboardDevice::Keys::kF, RE::BSKeyboardDevice::Keys::kG, RE::BSKeyboardDevice::Keys::kH,
        RE::BSKeyboardDevice::Keys::kI, RE::BSKeyboardDevice::Keys::kJ, RE::BSKeyboardDevice::Keys::kK, RE::BSKeyboardDevice::Keys::kL,
        RE::BSKeyboardDevice::Keys::kM, RE::BSKeyboardDevice::Keys::kN, RE::BSKeyboardDevice::Keys::kO, RE::BSKeyboardDevice::Keys::kP,
        RE::BSKeyboardDevice::Keys::kQ, RE::BSKeyboardDevice::Keys::kR, RE::BSKeyboardDevice::Keys::kS, RE::BSKeyboardDevice::Keys::kT,
        RE::BSKeyboardDevice::Keys::kU, RE::BSKeyboardDevice::Keys::kV, RE::BSKeyboardDevice::Keys::kW, RE::BSKeyboardDevice::Keys::kX,
        RE::BSKeyboardDevice::Keys::kY, RE::BSKeyboardDevice::Keys::kZ,
        // Números
        RE::BSKeyboardDevice::Keys::kNum1, RE::BSKeyboardDevice::Keys::kNum2, RE::BSKeyboardDevice::Keys::kNum3, RE::BSKeyboardDevice::Keys::kNum4,
        RE::BSKeyboardDevice::Keys::kNum5, RE::BSKeyboardDevice::Keys::kNum6, RE::BSKeyboardDevice::Keys::kNum7, RE::BSKeyboardDevice::Keys::kNum8,
        RE::BSKeyboardDevice::Keys::kNum9, RE::BSKeyboardDevice::Keys::kNum0,
        // Pontuação e Símbolos
        RE::BSKeyboardDevice::Keys::kMinus, RE::BSKeyboardDevice::Keys::kEquals, RE::BSKeyboardDevice::Keys::kBracketLeft,
        RE::BSKeyboardDevice::Keys::kBracketRight, RE::BSKeyboardDevice::Keys::kSemicolon, RE::BSKeyboardDevice::Keys::kApostrophe,
        RE::BSKeyboardDevice::Keys::kTilde, RE::BSKeyboardDevice::Keys::kBackslash, RE::BSKeyboardDevice::Keys::kComma,
        RE::BSKeyboardDevice::Keys::kPeriod, RE::BSKeyboardDevice::Keys::kSlash,
        // F-Keys
        RE::BSKeyboardDevice::Keys::kF1, RE::BSKeyboardDevice::Keys::kF2, RE::BSKeyboardDevice::Keys::kF3, RE::BSKeyboardDevice::Keys::kF4,
        RE::BSKeyboardDevice::Keys::kF5, RE::BSKeyboardDevice::Keys::kF6, RE::BSKeyboardDevice::Keys::kF7, RE::BSKeyboardDevice::Keys::kF8,
        RE::BSKeyboardDevice::Keys::kF9, RE::BSKeyboardDevice::Keys::kF10, RE::BSKeyboardDevice::Keys::kF11, RE::BSKeyboardDevice::Keys::kF12,
        // Especiais e Modificadores
        RE::BSKeyboardDevice::Keys::kEscape, RE::BSKeyboardDevice::Keys::kTab, RE::BSKeyboardDevice::Keys::kCapsLock,
        RE::BSKeyboardDevice::Keys::kLeftShift, RE::BSKeyboardDevice::Keys::kRightShift,
        RE::BSKeyboardDevice::Keys::kLeftControl, RE::BSKeyboardDevice::Keys::kRightControl,
        RE::BSKeyboardDevice::Keys::kLeftAlt, RE::BSKeyboardDevice::Keys::kRightAlt,
        RE::BSKeyboardDevice::Keys::kSpacebar, RE::BSKeyboardDevice::Keys::kEnter, RE::BSKeyboardDevice::Keys::kBackspace,
        RE::BSKeyboardDevice::Keys::kPrintScreen, RE::BSKeyboardDevice::Keys::kScrollLock, RE::BSKeyboardDevice::Keys::kPause,
        RE::BSKeyboardDevice::Keys::kNumLock,
        // Navegação
        RE::BSKeyboardDevice::Keys::kUp, RE::BSKeyboardDevice::Keys::kDown, RE::BSKeyboardDevice::Keys::kLeft, RE::BSKeyboardDevice::Keys::kRight,
        RE::BSKeyboardDevice::Keys::kInsert, RE::BSKeyboardDevice::Keys::kDelete, RE::BSKeyboardDevice::Keys::kHome, RE::BSKeyboardDevice::Keys::kEnd,
        RE::BSKeyboardDevice::Keys::kPageUp, RE::BSKeyboardDevice::Keys::kPageDown,
        // Numpad
        RE::BSKeyboardDevice::Keys::kKP_0, RE::BSKeyboardDevice::Keys::kKP_1, RE::BSKeyboardDevice::Keys::kKP_2, RE::BSKeyboardDevice::Keys::kKP_3,
        RE::BSKeyboardDevice::Keys::kKP_4, RE::BSKeyboardDevice::Keys::kKP_5, RE::BSKeyboardDevice::Keys::kKP_6, RE::BSKeyboardDevice::Keys::kKP_7,
        RE::BSKeyboardDevice::Keys::kKP_8, RE::BSKeyboardDevice::Keys::kKP_9,
        RE::BSKeyboardDevice::Keys::kKP_Plus, RE::BSKeyboardDevice::Keys::kKP_Subtract, RE::BSKeyboardDevice::Keys::kKP_Multiply,
        RE::BSKeyboardDevice::Keys::kKP_Divide, RE::BSKeyboardDevice::Keys::kKP_Enter, RE::BSKeyboardDevice::Keys::kKP_Decimal
    };

    inline const char* gamepadKeyNames[] = {
        "None",
        "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right",
        "Start / Options", "Back / Share / Select", "LS / L3 (Left Stick)", "RS / R3 (Right Stick)",
        "LB / L1 (Left Bumper)", "RB / R1 (Right Bumper)",
        "LT / L2 (Left Trigger)", "RT / R2 (Right Trigger)",
        "A / Cross", "B / Circle", "X / Square", "Y / Triangle"
    };

    inline const int gamepadKeyIDs[] = {
        0,
        RE::BSWin32GamepadDevice::Keys::kUp + GAMEPAD_OFFSET,
        RE::BSWin32GamepadDevice::Keys::kDown + GAMEPAD_OFFSET,
        RE::BSWin32GamepadDevice::Keys::kLeft + GAMEPAD_OFFSET,
        RE::BSWin32GamepadDevice::Keys::kRight + GAMEPAD_OFFSET,
        RE::BSWin32GamepadDevice::Keys::kStart + GAMEPAD_OFFSET,
        RE::BSWin32GamepadDevice::Keys::kBack + GAMEPAD_OFFSET,
        RE::BSWin32GamepadDevice::Keys::kLeftThumb + GAMEPAD_OFFSET,
        RE::BSWin32GamepadDevice::Keys::kRightThumb + GAMEPAD_OFFSET,
        RE::BSWin32GamepadDevice::Keys::kLeftShoulder + GAMEPAD_OFFSET,
        RE::BSWin32GamepadDevice::Keys::kRightShoulder + GAMEPAD_OFFSET,
        RE::BSWin32GamepadDevice::Keys::kLeftTrigger + GAMEPAD_OFFSET,
        RE::BSWin32GamepadDevice::Keys::kRightTrigger + GAMEPAD_OFFSET,
        RE::BSWin32GamepadDevice::Keys::kA + GAMEPAD_OFFSET,
        RE::BSWin32GamepadDevice::Keys::kB + GAMEPAD_OFFSET,
        RE::BSWin32GamepadDevice::Keys::kX + GAMEPAD_OFFSET,
        RE::BSWin32GamepadDevice::Keys::kY + GAMEPAD_OFFSET
    };


    void Register();
    void PlayerRender();
    void NPCRender();
    void RulesRender();
    void LoadSettings();
    void SaveSettings();
    void TweenPauseRegister();
    void UnregisterInputCategory(const std::string& actionId);
    void EnsureDefaultInputs();
    void RegisterAllInputs();
    void DMCOConverterRender();
    // Utilitários de JSON
    void ReadJSON(const rapidjson::Value& doc, const char* nome, std::vector<int>& lista);
    void WriteJSON(rapidjson::Value& doc, rapidjson::Document::AllocatorType& alloc, const char* nome, const std::vector<int>& lista);
}
