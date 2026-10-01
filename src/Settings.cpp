#include "Settings.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <map>
#include <filesystem>
#include <system_error>
#include "Manager.h"

namespace ImGui = ImGuiMCP;

namespace DodgeModMenu {
    constexpr const char* MOD_DIR = "Data/Viny Mods/Dodge for all";
    const std::string RULES_DIR = "Data/Viny Mods/Dodge for all/Rules/";
    const std::string OLD_RULES_DIR = "Data/SKSE/Plugins/DodgeAll/Rules/";
    const char* OLD_SETTINGS_PATH = "Data/SKSE/Plugins/DodgeAll/DodgeMod_Settings.json";
    const char* SETTINGS_PATH = "Data/SKSE/Plugins/DodgeAll/Settings.json";
    const char* OLD_PLAYER_SETTINGS_PATH = "Data/SKSE/Plugins/DodgeAll/PlayerSettings.json";
    const char* OLD_NPC_SETTINGS_PATH = "Data/SKSE/Plugins/DodgeAll/NPCSettings.json";
    const char* OLD_LANG_PATH = "Data/SKSE/Plugins/DodgeAll/Language.json";
    const char* PLAYER_SETTINGS_PATH = "Data/Viny Mods/Dodge for all/PlayerSettings.json";
    const char* NPC_SETTINGS_PATH = "Data/Viny Mods/Dodge for all/NPCSettings.json";
    const char* LANG_PATH = "Data/Viny Mods/Dodge for all/Language.json";
    static std::unordered_map<std::string, std::string> LangMap;

    void LoadLanguage() {
        LangMap.clear();
        std::ifstream file(LANG_PATH, std::ios::binary);
        if (!file.is_open()) {
            file.open(OLD_LANG_PATH, std::ios::binary);
        }
        if (!file.is_open()) {
            SKSE::log::warn("Não foi possível carregar DodgeMod_Language.json. Usando textos padrões.");
            return;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string jsonStr = buffer.str();
        file.close();

        if (jsonStr.size() >= 3 && (unsigned char)jsonStr[0] == 0xEF && (unsigned char)jsonStr[1] == 0xBB && (unsigned char)jsonStr[2] == 0xBF) {
            jsonStr.erase(0, 3);
        }

        rapidjson::Document doc;
        doc.Parse(jsonStr.c_str());

        if (doc.HasParseError()) return;

        if (doc.IsObject()) {
            for (auto itr = doc.MemberBegin(); itr != doc.MemberEnd(); ++itr) {
                if (itr->value.IsObject()) {
                    std::string category = itr->name.GetString();
                    for (auto jtr = itr->value.MemberBegin(); jtr != itr->value.MemberEnd(); ++jtr) {
                        if (jtr->value.IsString()) {
                            LangMap[category + "." + jtr->name.GetString()] = jtr->value.GetString();
                        }
                    }
                }
                else if (itr->value.IsString()) {
                    LangMap[itr->name.GetString()] = itr->value.GetString();
                }
            }
        }
    }

    const char* GetLoc(const std::string& key, const char* defaultVal) {
        auto it = LangMap.find(key);
        if (it != LangMap.end()) return it->second.c_str();
        return defaultVal;
    }

    inline std::string ToLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    inline int GetIndexFromID(int id, const int* idArray, int arraySize) {
        for (int i = 0; i < arraySize; i++) {
            if (idArray[i] == id) return i;
        }
        return 0;
    }

    inline bool SearchableCombo(const char* label, int* current_item, const char* const items[], int items_count) {
        bool changed = false;
        const char* preview_value = (*current_item >= 0 && *current_item < items_count) ? items[*current_item] : GetLoc("common.none", "None");

        if (ImGui::BeginCombo(label, preview_value)) {
            static char searchBuf[128] = "";
            if (ImGui::IsWindowAppearing()) {
                searchBuf[0] = '\0';
                ImGui::SetKeyboardFocusHere();
            }
            std::string searchLabel = std::string(GetLoc("common.search_placeholder", "Filter...")) + "##Search";
            ImGui::InputText(searchLabel.c_str(), searchBuf, sizeof(searchBuf));
            ImGui::Separator();

            std::string searchLower = ToLower(searchBuf);

            for (int i = 0; i < items_count; i++) {
                if (searchLower.empty() || ToLower(items[i]).find(searchLower) != std::string::npos) {
                    bool is_selected = (*current_item == i);
                    if (ImGui::Selectable(items[i], is_selected)) {
                        *current_item = i;
                        changed = true;
                    }
                    if (is_selected && ImGui::IsWindowAppearing()) {
                        ImGui::SetScrollHereY();
                    }
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    std::string SanitizeRuleFileName(const std::string& ruleName) {
        std::string safeName;
        safeName.reserve(ruleName.size());

        for (char ch : ruleName) {
            unsigned char c = static_cast<unsigned char>(ch);
            if (std::isalnum(c) || ch == ' ' || ch == '_' || ch == '-') {
                safeName.push_back(ch);
            }
            else {
                safeName.push_back('_');
            }
        }

        while (!safeName.empty() && (safeName.back() == ' ' || safeName.back() == '.')) {
            safeName.pop_back();
        }

        if (safeName.empty() || safeName == "." || safeName == "..") {
            safeName = "New Rule";
        }

        return safeName;
    }

    // ==========================================
    // UI DROPDOWN (Adaptado para RE::FormID)
    // ==========================================
    bool DrawDropdown(const char* label, const std::string& category, RE::FormID& current_form_id, float customWidth = -1.0f) {
        bool changed = false;
        const auto& fullList = Manager::GetSingleton()->GetList(category);
        if (fullList.empty()) return false;

        std::vector<const char*> comboItems;
        std::vector<int> mapToFull;

        comboItems.push_back(GetLoc("text.none", "None"));
        mapToFull.push_back(-1);

        int localSelection = 0;
        for (size_t i = 0; i < fullList.size(); ++i) {
            comboItems.push_back(fullList[i].cachedDisplayName.c_str());
            mapToFull.push_back(static_cast<int>(i));
            if (fullList[i].formID == current_form_id) {
                localSelection = static_cast<int>(i) + 1; // +1 porque o 0 é o "None"
            }
        }

        ImGui::PushID(label);
        std::string displayLabel = label;
        size_t hashPos = displayLabel.find("##");
        if (hashPos != std::string::npos) displayLabel = displayLabel.substr(0, hashPos);

        ImGui::Text("%s:", displayLabel.c_str());
        ImGui::SameLine();

        if (customWidth > 0.0f) ImGui::SetNextItemWidth(customWidth);
        const char* previewValue = comboItems[localSelection];

        if (ImGui::BeginCombo("##drop", previewValue)) {
            static std::map<std::string, std::string> searchBuffers;
            char searchBuf[256] = "";
            if (searchBuffers.contains(label)) strcpy_s(searchBuf, searchBuffers[label].c_str());

            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##busca", searchBuf, sizeof(searchBuf))) {
                searchBuffers[label] = searchBuf;
            }
            ImGui::Separator();

            std::string searchStr = searchBuf;
            std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), [](unsigned char c) { return std::tolower(c); });

            ImGui::BeginChild("##scroll", ImGui::ImVec2(0, 200), false);
            for (int i = 0; i < comboItems.size(); i++) {
                std::string itemStr = comboItems[i];
                std::string itemLower = itemStr;
                std::transform(itemLower.begin(), itemLower.end(), itemLower.begin(), [](unsigned char c) { return std::tolower(c); });

                if (searchStr.empty() || itemLower.find(searchStr) != std::string::npos) {
                    bool isSelected = (localSelection == i);
                    if (ImGui::Selectable(comboItems[i], isSelected)) {
                        localSelection = i;
                        int originalIndex = mapToFull[localSelection];

                        if (originalIndex == -1) current_form_id = 0;
                        else current_form_id = fullList[originalIndex].formID;

                        searchBuffers[label] = "";
                        changed = true;
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndChild();
            ImGui::EndCombo();
        }
        ImGui::PopID();
        return changed;
    }

    bool RenderSliderWithInput(const char* label, float* v, float v_min, float v_max, const char* format = "%.2f") {
        bool changed = false;
        ImGui::PushID(label);

        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::SliderFloat("##slider", v, v_min, v_max, format)) changed = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputFloat(label, v, 0.0f, 0.0f, format)) changed = true;
        if (*v < v_min) {
            *v = v_min;
            changed = true;
        }

        ImGui::PopID();
        return changed;
    }

    const char* GetDodgeValueSourceLabel(Settings::DodgeValueSource source) {
        switch (source) {
        case Settings::DodgeValueSource::kFixed:
            return GetLoc("menu.value_source_fixed", "Fixed");
        case Settings::DodgeValueSource::kActorValue:
            return GetLoc("menu.value_source_actor_value", "Actor Value");
        case Settings::DodgeValueSource::kGlobal:
            return GetLoc("menu.value_source_global", "Global");
        default:
            return GetLoc("common.unnamed", "Unnamed");
        }
    }

    bool DrawDodgeValueSourceDropdown(const char* label, Settings::DodgeValueSource& source) {
        bool changed = false;
        const Settings::DodgeValueSource sources[] = {
            Settings::DodgeValueSource::kFixed,
            Settings::DodgeValueSource::kActorValue,
            Settings::DodgeValueSource::kGlobal
        };

        if (ImGui::BeginCombo(label, GetDodgeValueSourceLabel(source))) {
            for (auto option : sources) {
                bool selected = source == option;
                if (ImGui::Selectable(GetDodgeValueSourceLabel(option), selected)) {
                    source = option;
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    bool RenderStringInput(const char* label, std::string& value) {
        char buffer[128]{};
        strcpy_s(buffer, value.c_str());
        if (ImGui::InputText(label, buffer, sizeof(buffer))) {
            value = buffer;
            return true;
        }
        return false;
    }

    bool RenderDodgeValueSetting(const char* sectionId, const char* label, float& fixedValue, Settings::DodgeValueSource& source, std::string& actorValue, RE::FormID& global, float minValue, float maxValue, const char* format, bool allowActorValue = true) {
        bool changed = false;
        ImGui::PushID(sectionId);

        std::string sourceLabel = std::string(label) + " " + GetLoc("menu.value_source", "Source");
        if (DrawDodgeValueSourceDropdown(sourceLabel.c_str(), source)) changed = true;

        switch (source) {
        case Settings::DodgeValueSource::kFixed:
            if (RenderSliderWithInput(label, &fixedValue, minValue, maxValue, format)) changed = true;
            break;
        case Settings::DodgeValueSource::kActorValue:
            if (allowActorValue) {
                if (RenderStringInput(GetLoc("menu.actor_value_name", "Actor Value"), actorValue)) changed = true;
            } else {
                source = Settings::DodgeValueSource::kFixed;
                changed = true;
            }
            break;
        case Settings::DodgeValueSource::kGlobal:
            if (DrawDropdown(GetLoc("menu.value_global", "Global"), "Global", global, 300.0f)) changed = true;
            break;
        default:
            source = Settings::DodgeValueSource::kFixed;
            changed = true;
            break;
        }

        ImGui::PopID();
        return changed;
    }

    void DrawDodgeTypeRuntimeUI(const char* sectionId, Settings::DodgeTypeSettings& settings, bool& changed, const char* const costTypes[], const char* const invulnMethods[], bool showPlayerMovementLock = false) {
        ImGui::PushID(sectionId);

        ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "%s", GetLoc("menu.costs_header", "Costs"));
        if (ImGui::Combo(GetLoc("menu.dodge_cost_type", "Dodge Cost Type"), &settings.costType, costTypes, 3)) changed = true;
        if (RenderDodgeValueSetting("cost", GetLoc("menu.dodge_cost", "Dodge Cost"), settings.cost, settings.costSource, settings.costActorValue, settings.costGlobal, 0.0f, 1000.0f, "%.2f")) changed = true;

        ImGui::Spacing();
        ImGui::TextColored({ 1.0f, 0.8f, 0.4f, 1.0f }, "%s", GetLoc("menu.invulnerability_header", "Invulnerability (I-Frames)"));
        if (ImGui::Checkbox(GetLoc("menu.enable_invulnerability", "Enable Invulnerability"), &settings.invulnerabilityEnabled)) changed = true;
        if (settings.invulnerabilityEnabled) {
            if (ImGui::Combo(GetLoc("menu.invuln_method", "Invulnerability Method"), &settings.invulnerabilityMethod, invulnMethods, 2)) changed = true;
            if (settings.invulnerabilityMethod == 0) {
                if (RenderDodgeValueSetting("invuln_duration", GetLoc("menu.invuln_duration", "Invulnerability Duration"), settings.invincibilityDuration, settings.invincibilityDurationSource, settings.invincibilityDurationActorValue, settings.invincibilityDurationGlobal, 0.05f, 30.0f, "%.2f")) changed = true;
            }
        }

        ImGui::Spacing();
        ImGui::TextColored({ 1.0f, 0.8f, 0.4f, 1.0f }, "%s", GetLoc("menu.perfect_dodge_header", "Perfect Dodge"));
        if (ImGui::Checkbox(GetLoc("menu.enable_perfect_dodge", "Enable Perfect Dodge"), &settings.enablePerfectDodge)) changed = true;
        if (settings.enablePerfectDodge) {
            if (RenderDodgeValueSetting("perfect_window", GetLoc("menu.perfect_window", "Perfect Dodge Window"), settings.perfectDodgeWindow, settings.perfectDodgeWindowSource, settings.perfectDodgeWindowActorValue, settings.perfectDodgeWindowGlobal, 0.01f, 30.0f, "%.2f")) changed = true;
            if (ImGui::Checkbox(GetLoc("menu.stop_time", "Stop Time on Perfect Dodge"), &settings.stopTimeOnPerfectDodge)) changed = true;
            if (settings.stopTimeOnPerfectDodge) {
                if (RenderDodgeValueSetting("time_stop_duration", GetLoc("menu.time_stop_duration", "Time Stop Duration"), settings.timeStopDuration, settings.timeStopDurationSource, settings.timeStopDurationActorValue, settings.timeStopDurationGlobal, 0.05f, 30.0f, "%.2f")) changed = true;
            }
        }

        ImGui::Spacing();
        ImGui::TextColored({ 1.0f, 0.8f, 0.4f, 1.0f }, "%s", GetLoc("menu.cancel_rules_header", "Cancel Rules"));
        if (ImGui::Checkbox(GetLoc("menu.cancel_dodge_attacks", "Cancel Dodge With Attacks"), &settings.cancelDodgeWithAttacks)) changed = true;
        if (ImGui::Checkbox(GetLoc("menu.cancel_dodge_power_attacks", "Cancel Dodge With Power Attacks"), &settings.cancelDodgeWithPowerAttacks)) changed = true;
        if (ImGui::Checkbox(GetLoc("menu.cancel_attack_dodge", "Cancel Attack With Dodge"), &settings.cancelAttackWithDodge)) changed = true;
        if (ImGui::Checkbox(GetLoc("menu.can_spam", "Can Spam Dodge"), &settings.canSpamDodge)) changed = true;

        if (showPlayerMovementLock) {
            ImGui::Spacing();
            ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "%s", GetLoc("menu.movement_lock_header", "Movement Lock"));
            if (ImGui::Checkbox(GetLoc("menu.cancel_dodge_movement", "Cancel Dodge With Movement"), &settings.cancelDodgeWithMovement)) changed = true;
            if (settings.cancelDodgeWithMovement) {
                const char* movementLockMethods[] = {
                    GetLoc("menu.movement_lock_custom", "Custom Time"),
                    GetLoc("menu.movement_lock_anim", "Animation Time")
                };
                if (ImGui::Combo(GetLoc("menu.movement_lock_method", "Movement Lock Method"), &settings.movementLockMethod, movementLockMethods, 2)) changed = true;
                if (settings.movementLockMethod == 0) {
                    if (RenderDodgeValueSetting("movement_lock_duration", GetLoc("menu.movement_lock_duration", "Movement Lock Duration"), settings.movementLockDuration, settings.movementLockDurationSource, settings.movementLockDurationActorValue, settings.movementLockDurationGlobal, 0.0f, 30.0f, "%.2f")) changed = true;
                }
                else {
                    ImGui::TextDisabled("%s", GetLoc("menu.movement_lock_anim_hint", "Movement remains locked until the normal BF_DodgeStop event."));
                }
            }
        }

        ImGui::PopID();
    }

    void DrawEventStringListUI(const char* sectionId, std::vector<std::string>& events, bool& changed) {
        ImGui::PushID(sectionId);

        static std::map<std::string, std::string> addBuffers;
        auto& pending = addBuffers[sectionId];
        char addBuf[128] = "";
        strcpy_s(addBuf, pending.c_str());

        for (size_t idx = 0; idx < events.size(); ) {
            ImGui::PushID(static_cast<int>(idx));
            ImGui::Text("%s", events[idx].c_str());
            ImGui::SameLine();
            if (ImGui::Button(GetLoc("common.remove", "Remove"))) {
                events.erase(events.begin() + idx);
                changed = true;
                ImGui::PopID();
                continue;
            }
            ImGui::PopID();
            idx++;
        }

        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::InputText(GetLoc("menu.random_dodge_event_name", "Animation Event"), addBuf, sizeof(addBuf))) {
            pending = addBuf;
        }
        ImGui::SameLine();
        if (ImGui::Button(GetLoc("menu.add_event", "Add Event"))) {
            std::string eventName = pending;
            if (!eventName.empty() && std::find(events.begin(), events.end(), eventName) == events.end()) {
                events.push_back(eventName);
                pending.clear();
                changed = true;
            }
        }

        ImGui::PopID();
    }

    void DrawRandomDodgeUI(const char* sectionId, bool& enabled, float& chance, bool& useArea, bool& requireLoS, std::vector<std::string>& events, bool& changed) {
        ImGui::PushID(sectionId);
        if (ImGui::Checkbox(GetLoc("menu.random_dodge_enabled", "Enable Random Dodge Events"), &enabled)) changed = true;
        if (enabled) {
            if (RenderSliderWithInput(GetLoc("menu.random_dodge_chance", "Random Dodge Chance (%)"), &chance, 0.0f, 100.0f, "%.1f")) changed = true;
            if (ImGui::Checkbox(GetLoc("menu.random_dodge_use_area", "Use Area Sensor Instead of Vision"), &useArea)) changed = true;
            if (ImGui::Checkbox(GetLoc("menu.random_dodge_require_los", "Require Line of Sight"), &requireLoS)) changed = true;
            DrawEventStringListUI("events", events, changed);
        }
        ImGui::PopID();
    }

    // --- Sub-Interface Unificada ImGui para Modos de Distância ---
    void DrawDistanceSettingsUI(const char* sectionId, Settings::DistanceSettings& d, bool& changed, bool showUpwardOptions, bool& upwardVar, RE::FormID& perkVar, const char* title = nullptr) {
        ImGui::PushID(sectionId);
        ImGui::TextColored({ 0.4f, 0.8f, 1.0f, 1.0f }, "%s", title ? title : GetLoc("menu.distance_settings_header", "--- Dodge Distance Configurations ---"));

        const char* modes[] = {
            GetLoc("menu.dist_mode_anim", "Animation (Root Motion)"),
            GetLoc("menu.dist_mode_simple", "Simple Vector"),
            GetLoc("menu.dist_mode_complex", "Complex Vector (9-Axis Custom)")
        };
        if (ImGui::Combo(GetLoc("menu.distance_mode", "Distance Mode"), &d.mode, modes, 3)) changed = true;

        if (d.mode != 0) {
            // Alterado o nome de "Force Duration" para "Impulse Duration" conforme solicitado
            if (RenderSliderWithInput(GetLoc("menu.distance_duration", "Impulse Duration (s)"), &d.duration, 0.05f, 2.0f)) changed = true;
        }

        if (d.mode == 1) { // Simple Mode
            if (RenderSliderWithInput(GetLoc("menu.distance_forward_back", "Forward / Backward Distance"), &d.simpleForwardBack, 0.0f, 500.0f)) changed = true;
            if (RenderSliderWithInput(GetLoc("menu.distance_left_right", "Left / Right Distance"), &d.simpleLeftRight, 0.0f, 500.0f)) changed = true;

            if (showUpwardOptions) {
                if (ImGui::Checkbox(GetLoc("menu.dodge_upwards", "Enable Upward Boost (Z-Axis)"), &upwardVar)) changed = true;
                if (upwardVar) {
                    if (DrawDropdown(GetLoc("menu.perk_upwards_lock", "Lock Upward Behind Perk"), "Perk", perkVar, 300.0f)) changed = true;
                    if (RenderSliderWithInput(GetLoc("menu.distance_upward", "Upward Boost Force"), &d.complexZ[0], -500.0f, 500.0f)) changed = true;
                }
            }
        }
        else if (d.mode == 2) { // Complex Mode
            if (showUpwardOptions) {
                if (ImGui::Checkbox(GetLoc("menu.dodge_upwards", "Enable Upward Boost (Z-Axis)"), &upwardVar)) changed = true;
                if (upwardVar) {
                    if (DrawDropdown(GetLoc("menu.perk_upwards_lock", "Lock Upward Behind Perk"), "Perk", perkVar, 300.0f)) changed = true;
                }
            }

            // Nomes alterados para obedecer estritamente à tabela numérica de direções solicitada
            const char* axisNames[] = {
                GetLoc("direction.no_movement", "0 = No Movement"),
                GetLoc("direction.forward", "1 = Forward"),
                GetLoc("direction.forward_right", "2 = Forward-Right"),
                GetLoc("direction.right", "3 = Right"),
                GetLoc("direction.backward_right", "4 = Backward-Right"),
                GetLoc("direction.backward", "5 = Backward"),
                GetLoc("direction.backward_left", "6 = Backward-Left"),
                GetLoc("direction.left", "7 = Left"),
                GetLoc("direction.forward_left", "8 = Forward-Left")
            };

            for (int i = 0; i < 9; i++) {
                if (ImGui::TreeNode(axisNames[i])) {
                    std::string labelX = std::string(GetLoc("direction.axis_x", "X (Positive = Right, Negative = Left)")) + "##" + std::to_string(i);

                    // Texto descritivo do eixo Y atualizado: Positivo para Foward, Negativo para Back conforme o comportamento nativo mapeado
                    std::string labelY = std::string(GetLoc("direction.axis_y", "Y (Positive = Forward, Negative = Backward)")) + "##" + std::to_string(i);

                    if (RenderSliderWithInput(labelX.c_str(), &d.complexX[i], -500.0f, 500.0f)) changed = true;
                    if (RenderSliderWithInput(labelY.c_str(), &d.complexY[i], -500.0f, 500.0f)) changed = true;

                    if (!showUpwardOptions || upwardVar) {
                        std::string labelZ = std::string(GetLoc("direction.axis_z", "Z (Upward Boost)")) + "##" + std::to_string(i);
                        if (RenderSliderWithInput(labelZ.c_str(), &d.complexZ[i], -500.0f, 500.0f)) changed = true;
                    }
                    ImGui::TreePop();
                }
            }
        }
        ImGui::PopID();
    }

    // --- Serialização JSON da Estrutura de Distâncias ---
    void SerializeDistance(rapidjson::Value& parent, rapidjson::Document::AllocatorType& alloc, const Settings::DistanceSettings& d) {
        parent.AddMember("mode", d.mode, alloc);
        parent.AddMember("duration", d.duration, alloc);
        parent.AddMember("simpleForwardBack", d.simpleForwardBack, alloc);
        parent.AddMember("simpleLeftRight", d.simpleLeftRight, alloc);

        rapidjson::Value arrX(rapidjson::kArrayType);
        rapidjson::Value arrY(rapidjson::kArrayType);
        rapidjson::Value arrZ(rapidjson::kArrayType);
        for (int i = 0; i < 9; i++) {
            arrX.PushBack(d.complexX[i], alloc);
            arrY.PushBack(d.complexY[i], alloc);
            arrZ.PushBack(d.complexZ[i], alloc);
        }
        parent.AddMember("complexX", arrX, alloc);
        parent.AddMember("complexY", arrY, alloc);
        parent.AddMember("complexZ", arrZ, alloc);
    }

    void DeserializeDistance(const rapidjson::Value& parent, Settings::DistanceSettings& d) {
        if (parent.HasMember("mode")) d.mode = parent["mode"].GetInt();
        if (parent.HasMember("duration")) d.duration = parent["duration"].GetFloat();
        if (parent.HasMember("simpleForwardBack")) d.simpleForwardBack = parent["simpleForwardBack"].GetFloat();
        if (parent.HasMember("simpleLeftRight")) d.simpleLeftRight = parent["simpleLeftRight"].GetFloat();

        auto ReadFloatArray = [](const rapidjson::Value& container, const char* key, float* targetArray) {
            if (container.HasMember(key) && container[key].IsArray()) {
                int idx = 0;
                for (auto& v : container[key].GetArray()) {
                    if (idx < 9 && v.IsNumber()) targetArray[idx++] = v.GetFloat();
                }
            }
            };
        ReadFloatArray(parent, "complexX", d.complexX);
        ReadFloatArray(parent, "complexY", d.complexY);
        ReadFloatArray(parent, "complexZ", d.complexZ);
    }

    void ReadJSON(const rapidjson::Value& doc, const char* nome, std::vector<int>& lista) {
        if (doc.HasMember(nome) && doc[nome].IsArray()) {
            lista.clear();
            for (auto& v : doc[nome].GetArray()) {
                if (v.IsInt()) lista.push_back(v.GetInt());
            }
        }
    }

    void WriteJSON(rapidjson::Value& doc, rapidjson::Document::AllocatorType& alloc, const char* nome, const std::vector<int>& lista) {
        rapidjson::Value array(rapidjson::kArrayType);
        for (int id : lista) {
            array.PushBack(id, alloc);
        }
        rapidjson::Value chave; chave.SetString(nome, alloc);
        doc.AddMember(chave, array, alloc);
    }

    void ReadStringJSON(const rapidjson::Value& doc, const char* nome, std::vector<std::string>& lista) {
        if (doc.HasMember(nome) && doc[nome].IsArray()) {
            lista.clear();
            for (auto& v : doc[nome].GetArray()) {
                if (v.IsString() && v.GetStringLength() > 0) lista.emplace_back(v.GetString());
            }
        }
    }

    void WriteStringJSON(rapidjson::Value& doc, rapidjson::Document::AllocatorType& alloc, const char* nome, const std::vector<std::string>& lista) {
        rapidjson::Value array(rapidjson::kArrayType);
        for (const auto& item : lista) {
            rapidjson::Value val;
            val.SetString(item.c_str(), alloc);
            array.PushBack(val, alloc);
        }
        rapidjson::Value chave; chave.SetString(nome, alloc);
        doc.AddMember(chave, array, alloc);
    }

    // --- SISTEMA DE INPUT LIST ---
    void EditInputVectors(const char* label, const std::string& actionIdStr, std::vector<int>& actionsRef, std::vector<int>& motionsRef) {
        ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "%s", label);

        if (!InputManagerAPI::_API) {
            ImGui::TextDisabled("%s", GetLoc("menu.input_manager_missing", "[Input Manager not found in memory]"));
            return;
        }

        bool changed = false;
        static int editingActionId = -1;
        static InputManagerAPI::ActionInfo editStagingInfo{};
        static bool showEditError = false;

        bool openEditPopup = false;
        std::string editPopupId = "EditActionPopup_" + actionIdStr;

        auto DrawSelectedList = [&](std::vector<int>& list, int type, const char* typeName) {
            const char* localizedType = (type == 0) ? GetLoc("common.action", "Action") : GetLoc("common.motion", "Motion");
            for (size_t i = 0; i < list.size(); i++) {
                ImGui::PushID((std::string(typeName) + "_" + std::to_string(i) + actionIdStr).c_str());

                const char* name = InputManagerAPI::_API->GetInputName(type, list[i]);
                std::string displayName = std::string("[") + localizedType + "] [" + std::to_string(list[i]) + "] " + (name ? name : GetLoc("common.unnamed", "Unnamed"));

                ImGui::Text("%s", displayName.c_str());

                if (type == 0) {
                    ImGui::SameLine();
                    if (ImGui::Button(GetLoc("common.edit", "Edit"))) {
                        editingActionId = list[i];
                        editStagingInfo = InputManagerAPI::_API->GetActionInfo(editingActionId);
                        showEditError = false;
                        openEditPopup = true;
                    }
                }

                ImGui::SameLine();
                if (ImGui::Button("X")) {
                    const char* purpose = actionIdStr == "DodgeRoll" ? "Dodge Roll" :
                        (actionIdStr == "DodgeStep" ? "Dodge Step" : "Dash");
                    InputManagerAPI::_API->UpdateListener(type, list[i], "Dodge for all", purpose, false, nullptr, 0, nullptr, 0);
                    list.erase(list.begin() + i);
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            };

        DrawSelectedList(actionsRef, 0, "Action");
        DrawSelectedList(motionsRef, 1, "Motion");

        if (openEditPopup) ImGui::OpenPopup(editPopupId.c_str());

        // POPUP EDIÇÃO DE AÇÃO
        if (ImGui::BeginPopup(editPopupId.c_str())) {
            if (editingActionId != -1 && editStagingInfo.isValid) {
                ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "%s: %s", GetLoc("menu.editing_action", "Editing Action"), editStagingInfo.name ? editStagingInfo.name : GetLoc("common.unnamed", "Unnamed"));
                ImGui::Separator();

                int pcKeySize = sizeof(pcKeyIDs) / sizeof(pcKeyIDs[0]);
                int padKeySize = sizeof(gamepadKeyIDs) / sizeof(gamepadKeyIDs[0]);

                auto DrawMainActionCombo = [](const char* label, int& current_action) {
                    if (ImGui::BeginCombo(label, actionStateNames[current_action])) {
                        for (int n = 0; n < 5; n++) {
                            if (n == 3) continue;
                            bool is_selected = (current_action == n);
                            if (ImGui::Selectable(actionStateNames[n], is_selected)) current_action = n;
                            if (is_selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    };

                auto DrawModActionCombo = [](const char* label, int& current_action, int main_action) {
                    if (ImGui::BeginCombo(label, actionStateNames[current_action])) {
                        for (int n = 0; n < 5; n++) {
                            if (n == 3 && main_action != 2 && main_action != 4) continue;
                            bool is_selected = (current_action == n);
                            if (ImGui::Selectable(actionStateNames[n], is_selected)) current_action = n;
                            if (is_selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    };

                auto DrawGestureCombo = [](const char* label, uint32_t& current_gesture) {
                    size_t gestCount = InputManagerAPI::_API->GetInputCount(2);
                    int current = static_cast<int>(current_gesture);
                    const char* preview = (current >= 0 && current < gestCount) ? InputManagerAPI::_API->GetInputName(2, current) : GetLoc("common.none", "None");

                    if (ImGui::BeginCombo(label, preview)) {
                        for (int i = 0; i < gestCount; ++i) {
                            bool is_selected = (current == i);
                            if (ImGui::Selectable(InputManagerAPI::_API->GetInputName(2, i), is_selected)) {
                                current_gesture = static_cast<uint32_t>(i);
                            }
                            if (is_selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    };

                auto DrawStickCombo = [](const char* label, int& current_stick) {
                    const char* sticks[] = { GetLoc("menu.left_stick", "Left Stick"), GetLoc("menu.right_stick", "Right Stick") };
                    const char* preview = (current_stick >= 0 && current_stick < 2) ? sticks[current_stick] : GetLoc("common.unnamed", "Unknown");
                    if (ImGui::BeginCombo(label, preview)) {
                        for (int i = 0; i < 2; i++) {
                            bool is_selected = (current_stick == i);
                            if (ImGui::Selectable(sticks[i], is_selected)) current_stick = i;
                            if (is_selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    };

                // PC
                ImGui::TextColored({ 0.7f, 0.7f, 1.0f, 1.0f }, "%s", GetLoc("menu.pc_settings_header", "--- PC Settings ---"));
                int pcMainIdx = GetIndexFromID(editStagingInfo.pcMainKey, pcKeyIDs, pcKeySize);
                if (SearchableCombo(GetLoc("menu.pc_main_key", "PC Main Key"), &pcMainIdx, pcKeyNames, pcKeySize)) editStagingInfo.pcMainKey = pcKeyIDs[pcMainIdx];
                DrawMainActionCombo(GetLoc("menu.pc_main_action", "PC Main Action"), editStagingInfo.pcMainAction);
                if (editStagingInfo.pcMainAction == 1) {
                    ImGui::SetNextItemWidth(120.0f);
                    ImGui::InputInt(GetLoc("menu.pc_main_taps", "PC Main Taps"), &editStagingInfo.pcMainTapCount);
                }

                if (editStagingInfo.pcModAction == 3 && editStagingInfo.pcMainAction != 2 && editStagingInfo.pcMainAction != 4) editStagingInfo.pcModAction = 0;

                DrawModActionCombo(GetLoc("menu.pc_mod_action", "PC Mod Action"), editStagingInfo.pcModAction, editStagingInfo.pcMainAction);
                if (editStagingInfo.pcModAction == 3) {
                    DrawGestureCombo(GetLoc("menu.pc_gesture", "PC Gesture"), editStagingInfo.pcModifierKey);
                }
                else {
                    int pcModIdx = GetIndexFromID(editStagingInfo.pcModifierKey, pcKeyIDs, pcKeySize);
                    if (SearchableCombo(GetLoc("menu.pc_mod_key", "PC Mod Key"), &pcModIdx, pcKeyNames, pcKeySize)) editStagingInfo.pcModifierKey = pcKeyIDs[pcModIdx];
                    if (editStagingInfo.pcModAction == 1) {
                        ImGui::SetNextItemWidth(120.0f);
                        ImGui::InputInt(GetLoc("menu.pc_mod_taps", "PC Mod Taps"), &editStagingInfo.pcModTapCount);
                    }
                }

                // GAMEPAD
                ImGui::Spacing();
                ImGui::TextColored({ 0.7f, 1.0f, 0.7f, 1.0f }, "%s", GetLoc("menu.pad_settings_header", "--- Gamepad Settings ---"));
                int padMainIdx = GetIndexFromID(editStagingInfo.gamepadMainKey, gamepadKeyIDs, padKeySize);
                if (SearchableCombo(GetLoc("menu.pad_main_key", "Pad Main Key"), &padMainIdx, gamepadKeyNames, padKeySize)) editStagingInfo.gamepadMainKey = gamepadKeyIDs[padMainIdx];
                DrawMainActionCombo(GetLoc("menu.pad_main_action", "Pad Main Action"), editStagingInfo.gamepadMainAction);
                if (editStagingInfo.gamepadMainAction == 1) {
                    ImGui::SetNextItemWidth(120.0f);
                    ImGui::InputInt(GetLoc("menu.pad_main_taps", "Pad Main Taps"), &editStagingInfo.gamepadMainTapCount);
                }

                if (editStagingInfo.gamepadModAction == 3 && editStagingInfo.gamepadMainAction != 2 && editStagingInfo.gamepadMainAction != 4) editStagingInfo.gamepadModAction = 0;

                DrawModActionCombo(GetLoc("menu.pad_mod_action", "Pad Mod Action"), editStagingInfo.gamepadModAction, editStagingInfo.gamepadMainAction);
                if (editStagingInfo.gamepadModAction == 3) {
                    DrawGestureCombo(GetLoc("menu.pad_gesture", "Pad Gesture"), editStagingInfo.gamepadModifierKey);
                    DrawStickCombo(GetLoc("menu.pad_gesture_stick", "Gesture Stick"), editStagingInfo.gamepadGestureStick);
                }
                else {
                    int padModIdx = GetIndexFromID(editStagingInfo.gamepadModifierKey, gamepadKeyIDs, padKeySize);
                    if (SearchableCombo(GetLoc("menu.pad_mod_key", "Pad Mod Key"), &padModIdx, gamepadKeyNames, padKeySize)) editStagingInfo.gamepadModifierKey = gamepadKeyIDs[padModIdx];
                    if (editStagingInfo.gamepadModAction == 1) {
                        ImGui::SetNextItemWidth(120.0f);
                        ImGui::InputInt(GetLoc("menu.pad_mod_taps", "Pad Mod Taps"), &editStagingInfo.gamepadModTapCount);
                    }
                }

                ImGui::Separator();
                if (showEditError) {
                    ImGui::TextColored({ 1.0f, 0.2f, 0.2f, 1.0f }, "%s", GetLoc("menu.save_error", "Error: Conflict detected or invalid input!"));
                }

                if (ImGui::Button(GetLoc("common.save", "Save"), { 120, 0 })) {
                    bool success = InputManagerAPI::_API->UpdateActionMapping(editingActionId, editStagingInfo);
                    if (success) {
                        ImGui::CloseCurrentPopup();
                        editingActionId = -1;
                    }
                    else {
                        showEditError = true;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(GetLoc("common.cancel", "Cancel"), { 120, 0 })) {
                    ImGui::CloseCurrentPopup();
                    editingActionId = -1;
                }
            }
            ImGui::EndPopup();
        }

        std::string popupId = "AddInputPopup_" + actionIdStr;
        std::string addInputLabel = std::string(GetLoc("menu.add_input", "+ Add Input")) + "##" + actionIdStr;
        if (ImGui::Button(addInputLabel.c_str())) {
            ImGui::OpenPopup(popupId.c_str());
        }

        // POPUP ADD INPUT
        if (ImGui::BeginPopup(popupId.c_str())) {
            static char searchBuf[128] = "";
            if (ImGui::IsWindowAppearing()) {
                searchBuf[0] = '\0';
                ImGui::SetKeyboardFocusHere();
            }

            if (ImGui::BeginTabBar(("InputTabs_" + actionIdStr).c_str())) {
                int selectedType = -1;
                if (ImGui::BeginTabItem(GetLoc("common.actions", "Actions"))) {
                    selectedType = 0;
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(GetLoc("common.motions", "Motions"))) {
                    selectedType = 1;
                    ImGui::EndTabItem();
                }

                if (selectedType != -1) {
                    std::string searchLabel = std::string(GetLoc("common.search_placeholder", "Filter...")) + "##SearchInput";
                    ImGui::InputText(searchLabel.c_str(), searchBuf, sizeof(searchBuf));
                    ImGui::Separator();

                    std::string searchLower = ToLower(searchBuf);

                    ImGui::BeginChild(("ChildList_" + actionIdStr).c_str(), { 300, 200 }, true);
                    size_t count = InputManagerAPI::_API->GetInputCount(selectedType);

                    for (int i = 0; i < count; i++) {
                        const char* name = InputManagerAPI::_API->GetInputName(selectedType, i);
                        std::string itemLabel = "[" + std::to_string(i) + "] " + (name ? name : GetLoc("common.unnamed", "Unnamed"));

                        bool matches = searchLower.empty();
                        if (!matches) matches = (ToLower(itemLabel).find(searchLower) != std::string::npos);

                        if (matches) {
                            if (ImGui::Selectable(itemLabel.c_str(), false)) {
                                if (selectedType == 0) {
                                    if (std::find(actionsRef.begin(), actionsRef.end(), i) == actionsRef.end()) {
                                        actionsRef.push_back(i);
                                        changed = true;
                                    }
                                }
                                else {
                                    if (std::find(motionsRef.begin(), motionsRef.end(), i) == motionsRef.end()) {
                                        motionsRef.push_back(i);
                                        changed = true;
                                    }
                                }
                                ImGui::CloseCurrentPopup();
                            }

                            if (ImGui::IsItemHovered()) {
                                ImGui::BeginTooltip();
                                ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "%s", GetLoc("menu.input_details", "Input Details"));
                                ImGui::Separator();

                                const char* idLoc = GetLoc("common.id", "ID");
                                const char* nameLoc = GetLoc("common.name", "Name");
                                const char* unnamedLoc = GetLoc("common.unnamed", "Unnamed");
                                const char* actionLoc = GetLoc("common.action", "Action");
                                const char* noInfoLoc = GetLoc("menu.no_info", "No information available.");

                                auto getActionName = [](int actionId) -> const char* {
                                    if (actionId >= 0 && actionId < 5) return actionStateNames[actionId];
                                    return "Unknown";
                                    };

                                auto formatAction = [&](int actionId, int tapCount) -> std::string {
                                    std::string n = getActionName(actionId);
                                    if (actionId == 1) { // 1 = Tap
                                        n += " x" + std::to_string(tapCount);
                                    }
                                    return n;
                                    };

                                auto getDirectionalName = [](int keyId) -> const char* {
                                    switch (keyId) {
                                    case InputManagerAPI::VKEY_DIR_UP:        return GetLoc("direction.up", "Up");
                                    case InputManagerAPI::VKEY_DIR_DOWN:      return GetLoc("direction.down", "Down");
                                    case InputManagerAPI::VKEY_DIR_LEFT:      return GetLoc("direction.left", "Left");
                                    case InputManagerAPI::VKEY_DIR_RIGHT:     return GetLoc("direction.right", "Right");
                                    case InputManagerAPI::VKEY_DIR_UPRIGHT:   return GetLoc("direction.up_right", "Up-Right");
                                    case InputManagerAPI::VKEY_DIR_UPLEFT:    return GetLoc("direction.up_left", "Up-Left");
                                    case InputManagerAPI::VKEY_DIR_DOWNRIGHT: return GetLoc("direction.down_right", "Down-Right");
                                    case InputManagerAPI::VKEY_DIR_DOWNLEFT:  return GetLoc("direction.down_left", "Down-Left");
                                    default: return nullptr;
                                    }
                                    };

                                auto getPcKeyName = [&](int keyId) -> const char* {
                                    if (const char* dirName = getDirectionalName(keyId)) return dirName;
                                    int size = sizeof(pcKeyIDs) / sizeof(pcKeyIDs[0]);
                                    int idx = GetIndexFromID(keyId, pcKeyIDs, size);
                                    return pcKeyNames[idx];
                                    };

                                auto getPadKeyName = [&](int keyId) -> const char* {
                                    if (const char* dirName = getDirectionalName(keyId)) return dirName;
                                    int size = sizeof(gamepadKeyIDs) / sizeof(gamepadKeyIDs[0]);
                                    int idx = GetIndexFromID(keyId, gamepadKeyIDs, size);
                                    return gamepadKeyNames[idx];
                                    };

                                if (selectedType == 0) { // Action
                                    auto info = InputManagerAPI::_API->GetActionInfo(i);

                                    if (info.isValid) {
                                        ImGui::Text("%s: %d | %s: %s", idLoc, info.id, nameLoc, info.name ? info.name : unnamedLoc);

                                        ImGui::Text("%s: %s (%s: %s)", GetLoc("menu.pc_main_key", "PC Main Key"),
                                            getPcKeyName(info.pcMainKey), actionLoc, formatAction(info.pcMainAction, info.pcMainTapCount).c_str());

                                        if (info.pcModifierKey != 0) {
                                            ImGui::Text("%s: %s (%s: %s)", GetLoc("menu.pc_mod_key", "PC Mod Key"),
                                                (info.pcModAction == 3) ? InputManagerAPI::_API->GetInputName(2, info.pcModifierKey) : getPcKeyName(info.pcModifierKey),
                                                actionLoc, formatAction(info.pcModAction, info.pcModTapCount).c_str());
                                        }

                                        ImGui::Text("%s: %s (%s: %s)", GetLoc("menu.pad_main_key", "Gamepad Main Key"),
                                            getPadKeyName(info.gamepadMainKey), actionLoc, formatAction(info.gamepadMainAction, info.gamepadMainTapCount).c_str());

                                        if (info.gamepadModifierKey != 0) {
                                            ImGui::Text("%s: %s (%s: %s)", GetLoc("menu.pad_mod_key", "Gamepad Mod Key"),
                                                (info.gamepadModAction == 3) ? InputManagerAPI::_API->GetInputName(2, info.gamepadModifierKey) : getPadKeyName(info.gamepadModifierKey),
                                                actionLoc, formatAction(info.gamepadModAction, info.gamepadModTapCount).c_str());
                                        }

                                        if (info.useCustomTimings) {
                                            ImGui::TextColored({ 0.8f, 0.8f, 0.4f, 1.0f }, "%s - %s: %.2fs | %s: %.2fs",
                                                GetLoc("menu.custom_timings", "Custom Timings"),
                                                GetLoc("menu.tap_window", "Tap Window"), info.tapWindow,
                                                GetLoc("menu.hold", "Hold"), info.holdDuration);
                                        }
                                    }
                                    else {
                                        ImGui::TextDisabled("%s", noInfoLoc);
                                    }
                                }
                                else { // Motion
                                    auto info = InputManagerAPI::_API->GetMotionInfo(i);

                                    if (info.isValid) {
                                        ImGui::Text("%s: %d | %s: %s", idLoc, info.id, nameLoc, info.name ? info.name : unnamedLoc);
                                        ImGui::Text("%s: %.2fs", GetLoc("menu.time_window", "Time Window"), info.timeWindow);

                                        std::string pcSeq = "";
                                        for (int k = 0; k < info.pcSequenceLength; k++) {
                                            if (k > 0) pcSeq += ", ";
                                            pcSeq += getPcKeyName(info.pcSequence[k]);
                                        }
                                        ImGui::Text("%s: %d [%s]", GetLoc("menu.pc_seq_size", "PC Sequence"), info.pcSequenceLength, pcSeq.c_str());

                                        std::string padSeq = "";
                                        for (int k = 0; k < info.padSequenceLength; k++) {
                                            if (k > 0) padSeq += ", ";
                                            padSeq += getPadKeyName(info.padSequence[k]);
                                        }
                                        ImGui::Text("%s: %d [%s]", GetLoc("menu.pad_seq_size", "Gamepad Sequence"), info.padSequenceLength, padSeq.c_str());
                                    }
                                    else {
                                        ImGui::TextDisabled("%s", noInfoLoc);
                                    }
                                }
                                ImGui::EndTooltip();
                            }
                        }
                    }
                    ImGui::EndChild();
                }
                ImGui::EndTabBar();
            }
            ImGui::EndPopup();
        }

        if (changed) {
            UnregisterInputCategory(actionIdStr);
            RegisterAllInputs();
            SaveSettings();
            TweenPauseRegister();
        }
    }

    void PlayerRender() {
        bool settings_changed = false;

        ImGui::Text("%s", GetLoc("menu.title", "Dodge Mod Settings"));
        ImGui::Separator(); ImGui::Spacing();

        // Estado da Arma (Sheathed / Unsheathed)
        if (ImGui::Checkbox(GetLoc("menu.can_dodge_sheathed", "Can Dodge With Weapon Sheathed"), &Settings::CanDodgeSheathed)) settings_changed = true;
        if (DrawDropdown(GetLoc("menu.perk_can_dodge_sheathed", "Lock Behind Perk (Weapon Sheathed)"), "Perk", Settings::PerkCanDodgeSheathed, 300.0f)) settings_changed = true;
        if (ImGui::Checkbox(GetLoc("menu.player_separate_sheathed_distance", "Use Separate Distance With Weapon Sheathed"), &Settings::PlayerUseSeparateSheathedDistance)) settings_changed = true;
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        const char* costTypes[] = {
            GetLoc("menu.cost_health", "Health"),
            GetLoc("menu.cost_stamina", "Stamina"),
            GetLoc("menu.cost_magicka", "Magicka")
        };
        const char* invulnMethods[] = {
            GetLoc("menu.invuln_custom", "Custom Time"),
            GetLoc("menu.invuln_anim", "Animation Time")
        };

        ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "%s", GetLoc("menu.player_actions_header", "Player Dodge / Dash Actions"));

        if (ImGui::Checkbox(GetLoc("menu.enable_roll_dodge", "Enable Roll Dodge"), &Settings::DodgeRollEnabled)) settings_changed = true;
        if (Settings::DodgeRollEnabled && ImGui::CollapsingHeader(GetLoc("menu.roll_header", "Roll Dodge"))) {
            ImGui::Indent();
            EditInputVectors(GetLoc("menu.roll_config", "Roll Dodge Config"), "DodgeRoll", Settings::DodgeRollActionIDs, Settings::DodgeRollMotionIDs);
            DrawDistanceSettingsUI("player_roll_dist", Settings::PlayerRollDistance, settings_changed, true, Settings::DodgeRollUpwards, Settings::PerkRollUpwards,
                Settings::PlayerUseSeparateSheathedDistance ? GetLoc("menu.roll_distance_drawn", "Roll Distance - Weapon Drawn") : GetLoc("menu.roll_distance_shared", "Roll Distance - Shared For Weapon Drawn / Sheathed"));
            if (Settings::PlayerUseSeparateSheathedDistance) {
                DrawDistanceSettingsUI("player_roll_sheathed_dist", Settings::PlayerRollSheathedDistance, settings_changed, true, Settings::DodgeRollUpwards, Settings::PerkRollUpwards, GetLoc("menu.roll_distance_sheathed", "Roll Distance - Weapon Sheathed"));
            }
            if (DrawDropdown(GetLoc("menu.perk_roll_dodge", "Lock Behind Perk (Roll Dodge)"), "Perk", Settings::PerkRollDodge, 300.0f)) settings_changed = true;
            DrawDodgeTypeRuntimeUI("player_roll_runtime", Settings::PlayerRollSettings, settings_changed, costTypes, invulnMethods, true);
            ImGui::Unindent();
        }

        if (ImGui::Checkbox(GetLoc("menu.enable_step_dodge", "Enable Step Dodge"), &Settings::DodgeStepEnabled)) settings_changed = true;
        if (Settings::DodgeStepEnabled && ImGui::CollapsingHeader(GetLoc("menu.step_header", "Step Dodge"))) {
            ImGui::Indent();
            EditInputVectors(GetLoc("menu.step_config", "Step Dodge Config"), "DodgeStep", Settings::DodgeStepActionIDs, Settings::DodgeStepMotionIDs);
            DrawDistanceSettingsUI("player_step_dist", Settings::PlayerStepDistance, settings_changed, true, Settings::DodgeStepUpwards, Settings::PerkStepUpwards,
                Settings::PlayerUseSeparateSheathedDistance ? GetLoc("menu.step_distance_drawn", "Step Distance - Weapon Drawn") : GetLoc("menu.step_distance_shared", "Step Distance - Shared For Weapon Drawn / Sheathed"));
            if (Settings::PlayerUseSeparateSheathedDistance) {
                DrawDistanceSettingsUI("player_step_sheathed_dist", Settings::PlayerStepSheathedDistance, settings_changed, true, Settings::DodgeStepUpwards, Settings::PerkStepUpwards, GetLoc("menu.step_distance_sheathed", "Step Distance - Weapon Sheathed"));
            }
            if (DrawDropdown(GetLoc("menu.perk_step_dodge", "Lock Behind Perk (Step Dodge)"), "Perk", Settings::PerkStepDodge, 300.0f)) settings_changed = true;
            DrawDodgeTypeRuntimeUI("player_step_runtime", Settings::PlayerStepSettings, settings_changed, costTypes, invulnMethods, true);
            ImGui::Unindent();
        }

        if (ImGui::Checkbox(GetLoc("menu.enable_dash", "Enable Dash"), &Settings::DashEnabled)) settings_changed = true;
        if (Settings::DashEnabled && ImGui::CollapsingHeader(GetLoc("menu.dash_header", "Dash"))) {
            ImGui::Indent();
            EditInputVectors(GetLoc("menu.dash_config", "Dash Config"), "Dash", Settings::DashActionIDs, Settings::DashMotionIDs);
            DrawDistanceSettingsUI("player_dash_dist", Settings::PlayerDashDistance, settings_changed, false, Settings::DodgeStepUpwards, Settings::PerkStepUpwards,
                Settings::PlayerUseSeparateSheathedDistance ? GetLoc("menu.dash_distance_drawn", "Dash Distance - Weapon Drawn") : GetLoc("menu.dash_distance_shared", "Dash Distance - Shared For Weapon Drawn / Sheathed"));
            if (Settings::PlayerUseSeparateSheathedDistance) {
                DrawDistanceSettingsUI("player_dash_sheathed_dist", Settings::PlayerDashSheathedDistance, settings_changed, false, Settings::DodgeStepUpwards, Settings::PerkStepUpwards, GetLoc("menu.dash_distance_sheathed", "Dash Distance - Weapon Sheathed"));
            }
            DrawDodgeTypeRuntimeUI("player_dash_runtime", Settings::PlayerDashSettings, settings_changed, costTypes, invulnMethods, true);
            ImGui::Unindent();
        }

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        ImGui::TextColored({ 1.0f, 0.8f, 0.4f, 1.0f }, "%s", GetLoc("menu.global_player_locks_header", "Global Player Perk Locks / Effects"));
        if (DrawDropdown(GetLoc("menu.perk_can_spam", "Lock Behind Perk (Can Spam)"), "Perk", Settings::PerkCanSpamDodge, 300.0f)) settings_changed = true;
        if (DrawDropdown(GetLoc("menu.perk_perfect_dodge", "Lock Behind Perk (Perfect Dodge)"), "Perk", Settings::PerkEnablePerfectDodge, 300.0f)) settings_changed = true;
        if (DrawDropdown(GetLoc("menu.perk_cancel_dodge", "Lock Behind Perk (Cancel Dodge)"), "Perk", Settings::PerkCancelDodgeWithAttacks, 300.0f)) settings_changed = true;
        if (DrawDropdown(GetLoc("menu.perk_cancel_dodge_power", "Lock Behind Perk (Cancel Dodge with Power Attack)"), "Perk", Settings::PerkCancelDodgeWithPowerAttacks, 300.0f)) settings_changed = true;
        if (DrawDropdown(GetLoc("menu.perk_cancel_attack", "Lock Behind Perk (Cancel Attack)"), "Perk", Settings::PerkCancelAttackWithDodge, 300.0f)) settings_changed = true;

        if (DrawDropdown(GetLoc("menu.perk_stop_time", "Lock Behind Perk (Stop Time)"), "Perk", Settings::PerkStopTimeOnPerfectDodge, 300.0f)) settings_changed = true;

        if (settings_changed) SaveSettings();
    }

    void DrawChanceUI(const char* label, Settings::ChanceSettings& s, bool& changed) {
        ImGuiMCP::TextColored({ 1.0f, 0.8f, 0.4f, 1.0f }, "%s", label);

        ImGuiMCP::Indent();

        // Expressão lambda para gerar o Slider + InputBox com facilidade
        auto DrawField = [&](const char* locKey, const char* fallback, float& val, float minV, float maxV, float limitV, const char* fmt) {
            std::string textLabel = std::string(GetLoc(locKey, fallback)) + "##" + label;

            // Desenha o Slider
            ImGuiMCP::SetNextItemWidth(250.0f);
            if (ImGuiMCP::SliderFloat(textLabel.c_str(), &val, minV, maxV, fmt)) changed = true;

            // Desenha o InputBox na mesma linha
            ImGuiMCP::SameLine();
            ImGuiMCP::SetNextItemWidth(90.0f);
            std::string inputLabel = "##Precise" + std::string(locKey) + label;
            if (ImGuiMCP::InputFloat(inputLabel.c_str(), &val, 0.0f, 0.0f, fmt)) {
                val = std::clamp(val, minV, limitV); // Clampa com um limite superior amigável para inputs manuais
                changed = true;
            }
            };

        // Renderização dos campos usando a nova lambda
        DrawField("menu.chance_base_weight", "Base Weight", s.baseChance, 0.0f, 100.0f, 1000.0f, "%.1f");
        DrawField("menu.chance_health_mult", "Missing Health Mult", s.healthMult, 0.0f, 100.0f, 1000.0f, "%.2f");
        DrawField("menu.chance_aggro_mult", "Aggression Mult", s.aggressionMult, 0.0f, 50.0f, 500.0f, "%.1f");
        DrawField("menu.chance_skill_mult", "Skill Mult (Sneak)", s.skillMult, 0.0f, 5.0f, 50.0f, "%.2f");
        DrawField("menu.chance_stamina_mult", "Max Stamina Mult", s.maxStaminaMult, 0.0f, 5.0f, 50.0f, "%.2f");
        DrawField("menu.chance_weight_mult", "Body Weight Mult", s.weightMult, -2.0f, 2.0f, 20.0f, "%.2f");
        DrawField("menu.chance_eq_weight_mult", "Equipped Weight Mult", s.equippedWeightMult, -2.0f, 2.0f, 20.0f, "%.2f");
        DrawField("menu.chance_encumb_mult", "Encumbrance Mult", s.encumbranceMult, -2.0f, 2.0f, 20.0f, "%.2f");
        DrawField("menu.chance_global_diff", "Global Difficulty", s.globalDifficulty, 1.0f, 1000.0f, 5000.0f, "%.1f");

        ImGuiMCP::Unindent();
        ImGuiMCP::Spacing();

        // --- Seção de Simulação e Fórmula ---
        ImGuiMCP::TextColored({ 1.0f, 0.8f, 0.0f, 1.0f }, "%s", GetLoc("menu.probability_logic", "Probability Logic:"));
        ImGuiMCP::Spacing();

        auto calcChance = [&](float missingHpPct, float aggro, float sneak, float stam, float weight, float eqpWeight, float encumbPct) {
            float power = s.baseChance + (missingHpPct * 100.0f * s.healthMult) + (aggro * s.aggressionMult) +
                (sneak * s.skillMult) + (stam * s.maxStaminaMult) + (weight * s.weightMult) + (eqpWeight * s.equippedWeightMult) + (encumbPct * 100.0f * s.encumbranceMult);
            if (power < 0.0f) power = 0.0f;
            return (power / (power + s.globalDifficulty)) * 100.0f;
            };

        float c1 = calcChance(0.0f, 0.8f, 15.0f, 100.0f, 50.0f, 15.0f, 0.3f);
        float c2 = calcChance(0.75f, 1.0f, 15.0f, 100.0f, 50.0f, 15.0f, 0.3f);
        float c3 = calcChance(0.0f, 2.0f, 40.0f, 200.0f, 70.0f, 20.0f, 0.5f);
        float c4 = calcChance(0.5f, 3.0f, 100.0f, 300.0f, 80.0f, 35.0f, 0.6f);
        float c5 = calcChance(0.0f, 1.0f, 80.0f, 150.0f, 30.0f, 5.0f, 0.15f);
        float c6 = calcChance(0.0f, 2.0f, 10.0f, 250.0f, 100.0f, 60.0f, 0.9f);
        float c7 = calcChance(0.9f, 0.5f, 20.0f, 100.0f, 40.0f, 5.0f, 0.2f);
        float c8 = calcChance(0.25f, 2.5f, 90.0f, 350.0f, 50.0f, 25.0f, 0.4f);

        ImGuiMCP::BulletText(GetLoc("menu.sim_bandit_normal", "Normal Bandit (HP:100%%, Aggr:0.8, Sneak:15, Stam:100, Wgt:50, EqpWgt:15, Encumb:30%%) -> Chance: %.2f%%"), c1);
        ImGuiMCP::BulletText(GetLoc("menu.sim_bandit_injured", "Injured Bandit (HP:25%%, Aggr:1.0, Sneak:15, Stam:100, Wgt:50, EqpWgt:15, Encumb:30%%) -> Chance: %.2f%%"), c2);
        ImGuiMCP::BulletText(GetLoc("menu.sim_npc_skilled", "Skilled NPC (HP:100%%, Aggr:2.0, Sneak:40, Stam:200, Wgt:70, EqpWgt:20, Encumb:50%%) -> Chance: %.2f%%"), c3);
        ImGuiMCP::BulletText(GetLoc("menu.sim_boss", "Boss (HP:50%%, Aggr:3.0, Sneak:100, Stam:300, Wgt:80, EqpWgt:35, Encumb:60%%) -> Chance: %.2f%%"), c4);
        ImGuiMCP::BulletText(GetLoc("menu.sim_light_assassin", "Light Assassin (HP:100%%, Aggr:1.0, Sneak:80, Stam:150, Wgt:30, EqpWgt:5, Encumb:15%%) -> Chance: %.2f%%"), c5);
        ImGuiMCP::BulletText(GetLoc("menu.sim_heavy_tank", "Heavy Tank (HP:100%%, Aggr:2.0, Sneak:10, Stam:250, Wgt:100, EqpWgt:60, Encumb:90%%) -> Chance: %.2f%%"), c6);
        ImGuiMCP::BulletText(GetLoc("menu.sim_desperate_mage", "Desperate Mage (HP:10%%, Aggr:0.5, Sneak:20, Stam:100, Wgt:40, EqpWgt:5, Encumb:20%%) -> Chance: %.2f%%"), c7);
        ImGuiMCP::BulletText(GetLoc("menu.sim_vampire_boss", "Vampire Boss (HP:75%%, Aggr:2.5, Sneak:90, Stam:350, Wgt:50, EqpWgt:25, Encumb:40%%) -> Chance: %.2f%%"), c8);
        ImGuiMCP::Spacing();
    }

    void WriteDodgeTypeSettings(rapidjson::Value& doc, rapidjson::Document::AllocatorType& alloc, const char* nome, const Settings::DodgeTypeSettings& settings);
    void ReadDodgeTypeSettings(const rapidjson::Value& doc, const char* nome, Settings::DodgeTypeSettings& settings);

    bool SaveRule(const Settings::NPCDodgeRule& rule) {
        std::error_code ec;
        const std::filesystem::path rulesDir(RULES_DIR);
        if (!std::filesystem::exists(rulesDir, ec)) {
            if (!std::filesystem::create_directories(rulesDir, ec) || ec) {
                SKSE::log::error("[Settings] Falha ao criar pasta de regras '{}': {}", rulesDir.string(), ec.message());
                return false;
            }
        }

        std::filesystem::path filepath = rulesDir / (SanitizeRuleFileName(rule.ruleName) + ".json");
        rapidjson::Document doc;
        doc.SetObject();
        auto& alloc = doc.GetAllocator();

        rapidjson::Value rName; rName.SetString(rule.ruleName.c_str(), alloc);
        doc.AddMember("ruleName", rName, alloc);

        auto perkForm = RE::TESForm::LookupByID(rule.perkID);
        std::string perkStr = FormUtil::NormalizeFormID(perkForm);
        rapidjson::Value pStr; pStr.SetString(perkStr.c_str(), alloc);
        doc.AddMember("perk", pStr, alloc);

        rapidjson::Value chanceObj(rapidjson::kObjectType);
        chanceObj.AddMember("baseChance", rule.chance.baseChance, alloc);
        chanceObj.AddMember("healthMult", rule.chance.healthMult, alloc);
        chanceObj.AddMember("aggressionMult", rule.chance.aggressionMult, alloc);
        chanceObj.AddMember("skillMult", rule.chance.skillMult, alloc);
        chanceObj.AddMember("maxStaminaMult", rule.chance.maxStaminaMult, alloc);
        chanceObj.AddMember("weightMult", rule.chance.weightMult, alloc);
        chanceObj.AddMember("encumbranceMult", rule.chance.encumbranceMult, alloc);
        chanceObj.AddMember("globalDifficulty", rule.chance.globalDifficulty, alloc);
        chanceObj.AddMember("equippedWeightMult", rule.chance.equippedWeightMult, alloc);
        doc.AddMember("chance", chanceObj, alloc);

        rapidjson::Value dashChanceObj(rapidjson::kObjectType);
        dashChanceObj.AddMember("baseChance", rule.dashChance.baseChance, alloc);
        dashChanceObj.AddMember("healthMult", rule.dashChance.healthMult, alloc);
        dashChanceObj.AddMember("aggressionMult", rule.dashChance.aggressionMult, alloc);
        dashChanceObj.AddMember("skillMult", rule.dashChance.skillMult, alloc);
        dashChanceObj.AddMember("maxStaminaMult", rule.dashChance.maxStaminaMult, alloc);
        dashChanceObj.AddMember("weightMult", rule.dashChance.weightMult, alloc);
        dashChanceObj.AddMember("encumbranceMult", rule.dashChance.encumbranceMult, alloc);
        dashChanceObj.AddMember("globalDifficulty", rule.dashChance.globalDifficulty, alloc);
        dashChanceObj.AddMember("equippedWeightMult", rule.dashChance.equippedWeightMult, alloc);
        doc.AddMember("dashChance", dashChanceObj, alloc);

        rapidjson::Value distObj(rapidjson::kObjectType);
        SerializeDistance(distObj, alloc, rule.distance);
        doc.AddMember("distance", distObj, alloc);

        rapidjson::Value rollDistObj(rapidjson::kObjectType);
        SerializeDistance(rollDistObj, alloc, rule.rollDistance);
        doc.AddMember("rollDistance", rollDistObj, alloc);

        rapidjson::Value stepDistObj(rapidjson::kObjectType);
        SerializeDistance(stepDistObj, alloc, rule.stepDistance);
        doc.AddMember("stepDistance", stepDistObj, alloc);

        rapidjson::Value dashDistObj(rapidjson::kObjectType);
        SerializeDistance(dashDistObj, alloc, rule.dashDistance);
        doc.AddMember("dashDistance", dashDistObj, alloc);

        doc.AddMember("dodgeProjectiles", rule.dodgeProjectiles, alloc);
        doc.AddMember("projRequireLoS", rule.projRequireLoS, alloc);
        doc.AddMember("meleeUseArea", rule.meleeUseArea, alloc);
        doc.AddMember("dashGapCloseEnabled", rule.dashGapCloseEnabled, alloc);
        doc.AddMember("dodgeType", rule.dodgeType, alloc);
        doc.AddMember("invulnerabilityMethod", rule.rollSettings.invulnerabilityMethod, alloc);
        doc.AddMember("invincibilityDuration", rule.rollSettings.invincibilityDuration, alloc);
        doc.AddMember("cancelDodgeWithAttacks", rule.rollSettings.cancelDodgeWithAttacks, alloc);
        doc.AddMember("cancelAttackWithDodge", rule.rollSettings.cancelAttackWithDodge, alloc);
        doc.AddMember("canSpamDodge", rule.rollSettings.canSpamDodge || rule.stepSettings.canSpamDodge || rule.dashSettings.canSpamDodge, alloc);
        doc.AddMember("allowStaggerDodge", rule.allowStaggerDodge, alloc);
        doc.AddMember("rollCost", rule.rollSettings.cost, alloc);
        doc.AddMember("rollTypeCost", rule.rollSettings.costType, alloc);
        doc.AddMember("stepCost", rule.stepSettings.cost, alloc);
        doc.AddMember("stepTypeCost", rule.stepSettings.costType, alloc);
        doc.AddMember("rollOnlyLightArmor", rule.rollOnlyLightArmor, alloc);
        doc.AddMember("meleeRequireLoS", rule.meleeRequireLoS, alloc);
        doc.AddMember("meleeDodgeEnabled", rule.meleeDodgeEnabled, alloc);
        doc.AddMember("smartDodge", rule.smartDodge, alloc);
        doc.AddMember("cancelDodgeWithPowerAttacks", rule.rollSettings.cancelDodgeWithPowerAttacks, alloc);
        doc.AddMember("dashInvulnerabilityEnabled", rule.dashSettings.invulnerabilityEnabled, alloc);
        doc.AddMember("dashInvulnerabilityMethod", rule.dashSettings.invulnerabilityMethod, alloc);
        doc.AddMember("dashInvincibilityDuration", rule.dashSettings.invincibilityDuration, alloc);
        doc.AddMember("dashCost", rule.dashSettings.cost, alloc);
        doc.AddMember("dashTypeCost", rule.dashSettings.costType, alloc);
        doc.AddMember("randomDodgeEnabled", rule.randomDodgeEnabled, alloc);
        doc.AddMember("randomDodgeChance", rule.randomDodgeChance, alloc);
        doc.AddMember("randomDodgeUseArea", rule.randomDodgeUseArea, alloc);
        doc.AddMember("randomDodgeRequireLoS", rule.randomDodgeRequireLoS, alloc);
        WriteStringJSON(doc, alloc, "randomDodgeEvents", rule.randomDodgeEvents);
        WriteDodgeTypeSettings(doc, alloc, "rollSettings", rule.rollSettings);
        WriteDodgeTypeSettings(doc, alloc, "stepSettings", rule.stepSettings);
        WriteDodgeTypeSettings(doc, alloc, "dashSettings", rule.dashSettings);

        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            SKSE::log::error("[Settings] Falha ao abrir regra para salvar '{}'.", filepath.string());
            return false;
        }

        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        if (!doc.Accept(writer)) {
            SKSE::log::error("[Settings] Falha ao serializar regra '{}'.", rule.ruleName);
            return false;
        }

        file << buffer.GetString();
        if (!file.good()) {
            SKSE::log::error("[Settings] Falha ao escrever regra '{}'.", filepath.string());
            return false;
        }

        return true;
    }

    void LoadRulesFromDirectory(const std::string& rulesDir) {
        Settings::NPCRules.clear();
        std::error_code ec;
        if (!std::filesystem::exists(rulesDir, ec)) {
            if (ec) {
                SKSE::log::warn("[Settings] Falha ao verificar pasta de regras '{}': {}", rulesDir, ec.message());
            }
            return;
        }

        for (const auto& entry : std::filesystem::directory_iterator(rulesDir, ec)) {
            if (ec) {
                SKSE::log::warn("[Settings] Falha ao iterar pasta de regras '{}': {}", rulesDir, ec.message());
                break;
            }
            if (entry.path().extension() == ".json") {
                std::ifstream file(entry.path());
                if (!file.is_open()) {
                    SKSE::log::warn("[Settings] Falha ao abrir regra '{}'.", entry.path().string());
                    continue;
                }

                std::stringstream buffer;
                buffer << file.rdbuf();

                rapidjson::Document doc;
                doc.Parse(buffer.str().c_str());
                if (doc.HasParseError() || !doc.IsObject()) {
                    SKSE::log::warn("[Settings] JSON de regra invalido '{}'.", entry.path().string());
                    continue;
                }

                Settings::NPCDodgeRule rule;
                if (doc.HasMember("ruleName")) rule.ruleName = doc["ruleName"].GetString();

                if (doc.HasMember("perk") && doc["perk"].IsString()) {
                    std::string perkStr = doc["perk"].GetString();
                    rule.perkID = FormUtil::FormIDFromString(perkStr);
                }

                if (doc.HasMember("chance") && doc["chance"].IsObject()) {
                    auto& c = doc["chance"];
                    if (c.HasMember("baseChance")) rule.chance.baseChance = c["baseChance"].GetFloat();
                    if (c.HasMember("healthMult")) rule.chance.healthMult = c["healthMult"].GetFloat();
                    if (c.HasMember("aggressionMult")) rule.chance.aggressionMult = c["aggressionMult"].GetFloat();
                    if (c.HasMember("skillMult")) rule.chance.skillMult = c["skillMult"].GetFloat();
                    if (c.HasMember("maxStaminaMult")) rule.chance.maxStaminaMult = c["maxStaminaMult"].GetFloat();
                    if (c.HasMember("weightMult")) rule.chance.weightMult = c["weightMult"].GetFloat();
                    if (c.HasMember("encumbranceMult")) rule.chance.encumbranceMult = c["encumbranceMult"].GetFloat();
                    if (c.HasMember("globalDifficulty")) rule.chance.globalDifficulty = c["globalDifficulty"].GetFloat();
                    if (c.HasMember("equippedWeightMult")) rule.chance.equippedWeightMult = c["equippedWeightMult"].GetFloat();
                }
                rule.dashChance = rule.chance;
                if (doc.HasMember("dashChance") && doc["dashChance"].IsObject()) {
                    auto& c = doc["dashChance"];
                    if (c.HasMember("baseChance")) rule.dashChance.baseChance = c["baseChance"].GetFloat();
                    if (c.HasMember("healthMult")) rule.dashChance.healthMult = c["healthMult"].GetFloat();
                    if (c.HasMember("aggressionMult")) rule.dashChance.aggressionMult = c["aggressionMult"].GetFloat();
                    if (c.HasMember("skillMult")) rule.dashChance.skillMult = c["skillMult"].GetFloat();
                    if (c.HasMember("maxStaminaMult")) rule.dashChance.maxStaminaMult = c["maxStaminaMult"].GetFloat();
                    if (c.HasMember("weightMult")) rule.dashChance.weightMult = c["weightMult"].GetFloat();
                    if (c.HasMember("encumbranceMult")) rule.dashChance.encumbranceMult = c["encumbranceMult"].GetFloat();
                    if (c.HasMember("globalDifficulty")) rule.dashChance.globalDifficulty = c["globalDifficulty"].GetFloat();
                    if (c.HasMember("equippedWeightMult")) rule.dashChance.equippedWeightMult = c["equippedWeightMult"].GetFloat();
                }

                // Recupera o bloco de distâncias customizado desta Regra de NPC
                if (doc.HasMember("distance") && doc["distance"].IsObject()) {
                    DeserializeDistance(doc["distance"], rule.distance);
                    rule.rollDistance = rule.distance;
                    rule.stepDistance = rule.distance;
                }
                if (doc.HasMember("rollDistance") && doc["rollDistance"].IsObject()) {
                    DeserializeDistance(doc["rollDistance"], rule.rollDistance);
                }
                if (doc.HasMember("stepDistance") && doc["stepDistance"].IsObject()) {
                    DeserializeDistance(doc["stepDistance"], rule.stepDistance);
                }
                if (doc.HasMember("dashDistance") && doc["dashDistance"].IsObject()) {
                    DeserializeDistance(doc["dashDistance"], rule.dashDistance);
                }

                if (doc.HasMember("dodgeProjectiles")) rule.dodgeProjectiles = doc["dodgeProjectiles"].GetInt();
                if (doc.HasMember("projRequireLoS")) rule.projRequireLoS = doc["projRequireLoS"].GetBool();
                if (doc.HasMember("meleeUseArea")) rule.meleeUseArea = doc["meleeUseArea"].GetBool();
                if (doc.HasMember("dashGapCloseEnabled")) rule.dashGapCloseEnabled = doc["dashGapCloseEnabled"].GetBool();
                if (doc.HasMember("dodgeType")) rule.dodgeType = doc["dodgeType"].GetInt();
                if (doc.HasMember("invulnerabilityMethod")) rule.invulnerabilityMethod = doc["invulnerabilityMethod"].GetInt();
                if (doc.HasMember("invincibilityDuration")) rule.invincibilityDuration = doc["invincibilityDuration"].GetFloat();
                if (doc.HasMember("cancelDodgeWithAttacks")) rule.cancelDodgeWithAttacks = doc["cancelDodgeWithAttacks"].GetBool();
                if (doc.HasMember("cancelAttackWithDodge")) rule.cancelAttackWithDodge = doc["cancelAttackWithDodge"].GetBool();
                if (doc.HasMember("canSpamDodge")) rule.canSpamDodge = doc["canSpamDodge"].GetBool();
                if (doc.HasMember("allowStaggerDodge")) rule.allowStaggerDodge = doc["allowStaggerDodge"].GetBool();
                if (doc.HasMember("rollCost")) rule.rollCost = doc["rollCost"].GetFloat();
                if (doc.HasMember("rollTypeCost")) rule.rollTypeCost = doc["rollTypeCost"].GetInt();
                if (doc.HasMember("stepCost")) rule.stepCost = doc["stepCost"].GetFloat();
                if (doc.HasMember("stepTypeCost")) rule.stepTypeCost = doc["stepTypeCost"].GetInt();
                if (doc.HasMember("rollOnlyLightArmor")) rule.rollOnlyLightArmor = doc["rollOnlyLightArmor"].GetBool();
                if (doc.HasMember("meleeRequireLoS")) rule.meleeRequireLoS = doc["meleeRequireLoS"].GetBool();
                if (doc.HasMember("meleeDodgeEnabled")) rule.meleeDodgeEnabled = doc["meleeDodgeEnabled"].GetBool();
                if (doc.HasMember("smartDodge")) rule.smartDodge = doc["smartDodge"].GetBool();
                if (doc.HasMember("cancelDodgeWithPowerAttacks")) rule.cancelDodgeWithPowerAttacks = doc["cancelDodgeWithPowerAttacks"].GetBool();
                if (doc.HasMember("dashInvulnerabilityEnabled")) rule.dashInvulnerabilityEnabled = doc["dashInvulnerabilityEnabled"].GetBool();
                if (doc.HasMember("dashInvulnerabilityMethod")) rule.dashInvulnerabilityMethod = doc["dashInvulnerabilityMethod"].GetInt();
                if (doc.HasMember("dashInvincibilityDuration")) rule.dashInvincibilityDuration = doc["dashInvincibilityDuration"].GetFloat();
                if (doc.HasMember("dashCost")) rule.dashCost = doc["dashCost"].GetFloat();
                if (doc.HasMember("dashTypeCost")) rule.dashTypeCost = doc["dashTypeCost"].GetInt();
                if (doc.HasMember("randomDodgeEnabled")) rule.randomDodgeEnabled = doc["randomDodgeEnabled"].GetBool();
                if (doc.HasMember("randomDodgeChance")) rule.randomDodgeChance = doc["randomDodgeChance"].GetFloat();
                if (doc.HasMember("randomDodgeUseArea")) rule.randomDodgeUseArea = doc["randomDodgeUseArea"].GetBool();
                if (doc.HasMember("randomDodgeRequireLoS")) rule.randomDodgeRequireLoS = doc["randomDodgeRequireLoS"].GetBool();
                ReadStringJSON(doc, "randomDodgeEvents", rule.randomDodgeEvents);

                rule.rollSettings.invulnerabilityMethod = rule.invulnerabilityMethod;
                rule.rollSettings.invincibilityDuration = rule.invincibilityDuration;
                rule.rollSettings.cancelDodgeWithAttacks = rule.cancelDodgeWithAttacks;
                rule.rollSettings.cancelDodgeWithPowerAttacks = rule.cancelDodgeWithPowerAttacks;
                rule.rollSettings.cancelAttackWithDodge = rule.cancelAttackWithDodge;
                rule.rollSettings.canSpamDodge = rule.canSpamDodge;
                rule.rollSettings.cost = rule.rollCost;
                rule.rollSettings.costType = rule.rollTypeCost;

                rule.stepSettings.invulnerabilityMethod = rule.invulnerabilityMethod;
                rule.stepSettings.invincibilityDuration = rule.invincibilityDuration;
                rule.stepSettings.cancelDodgeWithAttacks = rule.cancelDodgeWithAttacks;
                rule.stepSettings.cancelDodgeWithPowerAttacks = rule.cancelDodgeWithPowerAttacks;
                rule.stepSettings.cancelAttackWithDodge = rule.cancelAttackWithDodge;
                rule.stepSettings.canSpamDodge = rule.canSpamDodge;
                rule.stepSettings.cost = rule.stepCost;
                rule.stepSettings.costType = rule.stepTypeCost;

                rule.dashSettings.invulnerabilityEnabled = rule.dashInvulnerabilityEnabled;
                rule.dashSettings.invulnerabilityMethod = rule.dashInvulnerabilityMethod;
                rule.dashSettings.invincibilityDuration = rule.dashInvincibilityDuration;
                rule.dashSettings.cancelDodgeWithAttacks = rule.cancelDodgeWithAttacks;
                rule.dashSettings.cancelDodgeWithPowerAttacks = rule.cancelDodgeWithPowerAttacks;
                rule.dashSettings.cancelAttackWithDodge = rule.cancelAttackWithDodge;
                rule.dashSettings.canSpamDodge = rule.canSpamDodge;
                rule.dashSettings.cost = rule.dashCost;
                rule.dashSettings.costType = rule.dashTypeCost;

                ReadDodgeTypeSettings(doc, "rollSettings", rule.rollSettings);
                ReadDodgeTypeSettings(doc, "stepSettings", rule.stepSettings);
                ReadDodgeTypeSettings(doc, "dashSettings", rule.dashSettings);
                Settings::NPCRules.push_back(rule);
            }
        }
    }

    void LoadRules() {
        LoadRulesFromDirectory(RULES_DIR);
        if (Settings::NPCRules.empty()) {
            LoadRulesFromDirectory(OLD_RULES_DIR);
        }
    }

    bool LoadJSONFile(const char* path, rapidjson::Document& doc) {
        FILE* fp = nullptr;
        fopen_s(&fp, path, "rb");
        if (!fp) {
            SKSE::log::warn("[Settings] Arquivo JSON nao encontrado ou inacessivel '{}'.", path);
            return false;
        }

        char readBuffer[65536];
        rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));
        doc.ParseStream(is);
        fclose(fp);

        if (doc.HasParseError()) {
            SKSE::log::error("[Settings] Falha ao parsear JSON '{}'.", path);
            return false;
        }
        if (!doc.IsObject()) {
            SKSE::log::error("[Settings] JSON '{}' nao contem um objeto raiz.", path);
            return false;
        }

        return true;
    }

    bool SaveJSONFile(const char* path, rapidjson::Document& doc) {
        std::filesystem::path outPath(path);
        if (outPath.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(outPath.parent_path(), ec);
            if (ec) {
                SKSE::log::error("[Settings] Falha ao criar pasta '{}': {}", outPath.parent_path().string(), ec.message());
                return false;
            }
        }

        FILE* fp = nullptr;
        fopen_s(&fp, path, "wb");
        if (!fp) {
            SKSE::log::error("[Settings] Falha ao abrir JSON para salvar '{}'.", path);
            return false;
        }

        char writeBuffer[65536];
        rapidjson::FileWriteStream os(fp, writeBuffer, sizeof(writeBuffer));
        rapidjson::Writer<rapidjson::FileWriteStream> writer(os);
        bool serialized = doc.Accept(writer);
        os.Flush();
        int closeResult = fclose(fp);

        if (!serialized) {
            SKSE::log::error("[Settings] Falha ao serializar JSON '{}'.", path);
            return false;
        }
        if (closeResult != 0) {
            SKSE::log::error("[Settings] Falha ao finalizar escrita do JSON '{}'.", path);
            return false;
        }

        return true;
    }

    void WritePerk(rapidjson::Value& doc, rapidjson::Document::AllocatorType& alloc, const char* nome, RE::FormID formID) {
        auto form = RE::TESForm::LookupByID(formID);
        std::string str = FormUtil::NormalizeFormID(form);
        rapidjson::Value val; val.SetString(str.c_str(), alloc);
        rapidjson::Value chave; chave.SetString(nome, alloc);
        doc.AddMember(chave, val, alloc);
    }

    void ReadPerk(const rapidjson::Value& doc, const char* nome, RE::FormID& formIDRef) {
        if (doc.HasMember(nome)) {
            if (doc[nome].IsString()) formIDRef = FormUtil::FormIDFromString(doc[nome].GetString());
            else if (doc[nome].IsUint()) formIDRef = doc[nome].GetUint();
            else SKSE::log::warn("[Settings] Campo de perk '{}' possui tipo invalido.", nome);
        }
    }

    void WriteStringMember(rapidjson::Value& obj, rapidjson::Document::AllocatorType& alloc, const char* nome, const std::string& value) {
        rapidjson::Value str;
        str.SetString(value.c_str(), alloc);
        rapidjson::Value key;
        key.SetString(nome, alloc);
        obj.AddMember(key, str, alloc);
    }

    void WriteDodgeValueSource(rapidjson::Value& obj, rapidjson::Document::AllocatorType& alloc, const char* sourceName, const char* actorValueName, Settings::DodgeValueSource source, const std::string& actorValue) {
        rapidjson::Value key;
        key.SetString(sourceName, alloc);
        obj.AddMember(key, static_cast<int>(source), alloc);
        WriteStringMember(obj, alloc, actorValueName, actorValue);
    }

    void ReadDodgeValueSource(const rapidjson::Value& obj, const char* sourceName, const char* actorValueName, Settings::DodgeValueSource& source, std::string& actorValue) {
        if (obj.HasMember(sourceName) && obj[sourceName].IsInt()) {
            int value = obj[sourceName].GetInt();
            if (value >= static_cast<int>(Settings::DodgeValueSource::kFixed) && value <= static_cast<int>(Settings::DodgeValueSource::kGlobal)) {
                source = static_cast<Settings::DodgeValueSource>(value);
            }
        }
        if (obj.HasMember(actorValueName) && obj[actorValueName].IsString()) {
            actorValue = obj[actorValueName].GetString();
        }
    }

    void WriteDodgeTypeSettings(rapidjson::Value& doc, rapidjson::Document::AllocatorType& alloc, const char* nome, const Settings::DodgeTypeSettings& settings) {
        rapidjson::Value obj(rapidjson::kObjectType);
        obj.AddMember("invulnerabilityEnabled", settings.invulnerabilityEnabled, alloc);
        obj.AddMember("invulnerabilityMethod", settings.invulnerabilityMethod, alloc);
        obj.AddMember("invincibilityDuration", settings.invincibilityDuration, alloc);
        WritePerk(obj, alloc, "invincibilityDurationGlobal", settings.invincibilityDurationGlobal);
        WriteDodgeValueSource(obj, alloc, "invincibilityDurationSource", "invincibilityDurationActorValue", settings.invincibilityDurationSource, settings.invincibilityDurationActorValue);
        obj.AddMember("enablePerfectDodge", settings.enablePerfectDodge, alloc);
        obj.AddMember("perfectDodgeWindow", settings.perfectDodgeWindow, alloc);
        WritePerk(obj, alloc, "perfectDodgeWindowGlobal", settings.perfectDodgeWindowGlobal);
        WriteDodgeValueSource(obj, alloc, "perfectDodgeWindowSource", "perfectDodgeWindowActorValue", settings.perfectDodgeWindowSource, settings.perfectDodgeWindowActorValue);
        obj.AddMember("stopTimeOnPerfectDodge", settings.stopTimeOnPerfectDodge, alloc);
        obj.AddMember("timeStopDuration", settings.timeStopDuration, alloc);
        WritePerk(obj, alloc, "timeStopDurationGlobal", settings.timeStopDurationGlobal);
        WriteDodgeValueSource(obj, alloc, "timeStopDurationSource", "timeStopDurationActorValue", settings.timeStopDurationSource, settings.timeStopDurationActorValue);
        obj.AddMember("cancelDodgeWithAttacks", settings.cancelDodgeWithAttacks, alloc);
        obj.AddMember("cancelDodgeWithPowerAttacks", settings.cancelDodgeWithPowerAttacks, alloc);
        obj.AddMember("cancelAttackWithDodge", settings.cancelAttackWithDodge, alloc);
        obj.AddMember("canSpamDodge", settings.canSpamDodge, alloc);
        obj.AddMember("cost", settings.cost, alloc);
        obj.AddMember("costType", settings.costType, alloc);
        WritePerk(obj, alloc, "costGlobal", settings.costGlobal);
        WriteDodgeValueSource(obj, alloc, "costSource", "costActorValue", settings.costSource, settings.costActorValue);
        obj.AddMember("cancelDodgeWithMovement", settings.cancelDodgeWithMovement, alloc);
        obj.AddMember("movementLockMethod", settings.movementLockMethod, alloc);
        obj.AddMember("movementLockDuration", settings.movementLockDuration, alloc);
        WritePerk(obj, alloc, "movementLockDurationGlobal", settings.movementLockDurationGlobal);
        WriteDodgeValueSource(obj, alloc, "movementLockDurationSource", "movementLockDurationActorValue", settings.movementLockDurationSource, settings.movementLockDurationActorValue);

        rapidjson::Value key; key.SetString(nome, alloc);
        doc.AddMember(key, obj, alloc);
    }

    void ReadDodgeTypeSettings(const rapidjson::Value& doc, const char* nome, Settings::DodgeTypeSettings& settings) {
        if (!doc.HasMember(nome) || !doc[nome].IsObject()) return;

        const auto& obj = doc[nome];
        if (obj.HasMember("invulnerabilityEnabled")) settings.invulnerabilityEnabled = obj["invulnerabilityEnabled"].GetBool();
        if (obj.HasMember("invulnerabilityMethod")) settings.invulnerabilityMethod = obj["invulnerabilityMethod"].GetInt();
        if (obj.HasMember("invincibilityDuration")) settings.invincibilityDuration = obj["invincibilityDuration"].GetFloat();
        ReadPerk(obj, "invincibilityDurationGlobal", settings.invincibilityDurationGlobal);
        ReadDodgeValueSource(obj, "invincibilityDurationSource", "invincibilityDurationActorValue", settings.invincibilityDurationSource, settings.invincibilityDurationActorValue);
        if (!obj.HasMember("invincibilityDurationSource") && settings.invincibilityDurationGlobal != 0) settings.invincibilityDurationSource = Settings::DodgeValueSource::kGlobal;
        if (obj.HasMember("enablePerfectDodge")) settings.enablePerfectDodge = obj["enablePerfectDodge"].GetBool();
        if (obj.HasMember("perfectDodgeWindow")) settings.perfectDodgeWindow = obj["perfectDodgeWindow"].GetFloat();
        ReadPerk(obj, "perfectDodgeWindowGlobal", settings.perfectDodgeWindowGlobal);
        ReadDodgeValueSource(obj, "perfectDodgeWindowSource", "perfectDodgeWindowActorValue", settings.perfectDodgeWindowSource, settings.perfectDodgeWindowActorValue);
        if (!obj.HasMember("perfectDodgeWindowSource") && settings.perfectDodgeWindowGlobal != 0) settings.perfectDodgeWindowSource = Settings::DodgeValueSource::kGlobal;
        if (obj.HasMember("stopTimeOnPerfectDodge")) settings.stopTimeOnPerfectDodge = obj["stopTimeOnPerfectDodge"].GetBool();
        if (obj.HasMember("timeStopDuration")) settings.timeStopDuration = obj["timeStopDuration"].GetFloat();
        ReadPerk(obj, "timeStopDurationGlobal", settings.timeStopDurationGlobal);
        ReadDodgeValueSource(obj, "timeStopDurationSource", "timeStopDurationActorValue", settings.timeStopDurationSource, settings.timeStopDurationActorValue);
        if (!obj.HasMember("timeStopDurationSource") && settings.timeStopDurationGlobal != 0) settings.timeStopDurationSource = Settings::DodgeValueSource::kGlobal;
        if (obj.HasMember("cancelDodgeWithAttacks")) settings.cancelDodgeWithAttacks = obj["cancelDodgeWithAttacks"].GetBool();
        if (obj.HasMember("cancelDodgeWithPowerAttacks")) settings.cancelDodgeWithPowerAttacks = obj["cancelDodgeWithPowerAttacks"].GetBool();
        if (obj.HasMember("cancelAttackWithDodge")) settings.cancelAttackWithDodge = obj["cancelAttackWithDodge"].GetBool();
        if (obj.HasMember("canSpamDodge")) settings.canSpamDodge = obj["canSpamDodge"].GetBool();
        if (obj.HasMember("cost")) settings.cost = obj["cost"].GetFloat();
        if (obj.HasMember("costType")) settings.costType = obj["costType"].GetInt();
        ReadPerk(obj, "costGlobal", settings.costGlobal);
        ReadDodgeValueSource(obj, "costSource", "costActorValue", settings.costSource, settings.costActorValue);
        if (!obj.HasMember("costSource") && settings.costGlobal != 0) settings.costSource = Settings::DodgeValueSource::kGlobal;
        if (obj.HasMember("cancelDodgeWithMovement")) settings.cancelDodgeWithMovement = obj["cancelDodgeWithMovement"].GetBool();
        if (obj.HasMember("movementLockMethod")) settings.movementLockMethod = std::clamp(obj["movementLockMethod"].GetInt(), 0, 1);
        if (obj.HasMember("movementLockDuration")) settings.movementLockDuration = obj["movementLockDuration"].GetFloat();
        ReadPerk(obj, "movementLockDurationGlobal", settings.movementLockDurationGlobal);
        ReadDodgeValueSource(obj, "movementLockDurationSource", "movementLockDurationActorValue", settings.movementLockDurationSource, settings.movementLockDurationActorValue);
        if (!obj.HasMember("movementLockDurationSource") && settings.movementLockDurationGlobal != 0) settings.movementLockDurationSource = Settings::DodgeValueSource::kGlobal;
    }

    void WriteChance(rapidjson::Value& doc, rapidjson::Document::AllocatorType& alloc, const char* nome, const Settings::ChanceSettings& chance) {
        rapidjson::Value obj(rapidjson::kObjectType);
        obj.AddMember("baseChance", chance.baseChance, alloc);
        obj.AddMember("healthMult", chance.healthMult, alloc);
        obj.AddMember("aggressionMult", chance.aggressionMult, alloc);
        obj.AddMember("skillMult", chance.skillMult, alloc);
        obj.AddMember("maxStaminaMult", chance.maxStaminaMult, alloc);
        obj.AddMember("weightMult", chance.weightMult, alloc);
        obj.AddMember("encumbranceMult", chance.encumbranceMult, alloc);
        obj.AddMember("globalDifficulty", chance.globalDifficulty, alloc);
        obj.AddMember("equippedWeightMult", chance.equippedWeightMult, alloc);
        rapidjson::Value key; key.SetString(nome, alloc);
        doc.AddMember(key, obj, alloc);
    }

    void ReadChance(const rapidjson::Value& doc, const char* nome, Settings::ChanceSettings& chance) {
        if (!doc.HasMember(nome) || !doc[nome].IsObject()) return;

        auto& chanceObj = doc[nome];
        if (chanceObj.HasMember("baseChance")) chance.baseChance = chanceObj["baseChance"].GetFloat();
        if (chanceObj.HasMember("healthMult")) chance.healthMult = chanceObj["healthMult"].GetFloat();
        if (chanceObj.HasMember("aggressionMult")) chance.aggressionMult = chanceObj["aggressionMult"].GetFloat();
        if (chanceObj.HasMember("skillMult")) chance.skillMult = chanceObj["skillMult"].GetFloat();
        if (chanceObj.HasMember("maxStaminaMult")) chance.maxStaminaMult = chanceObj["maxStaminaMult"].GetFloat();
        if (chanceObj.HasMember("weightMult")) chance.weightMult = chanceObj["weightMult"].GetFloat();
        if (chanceObj.HasMember("encumbranceMult")) chance.encumbranceMult = chanceObj["encumbranceMult"].GetFloat();
        if (chanceObj.HasMember("globalDifficulty")) chance.globalDifficulty = chanceObj["globalDifficulty"].GetFloat();
        if (chanceObj.HasMember("equippedWeightMult")) chance.equippedWeightMult = chanceObj["equippedWeightMult"].GetFloat();
    }

    void WritePlayerSettings(rapidjson::Value& doc, rapidjson::Document::AllocatorType& alloc) {
        WriteJSON(doc, alloc, "DodgeRollActionIDs", Settings::DodgeRollActionIDs);
        WriteJSON(doc, alloc, "DodgeRollMotionIDs", Settings::DodgeRollMotionIDs);
        WriteJSON(doc, alloc, "DodgeStepActionIDs", Settings::DodgeStepActionIDs);
        WriteJSON(doc, alloc, "DodgeStepMotionIDs", Settings::DodgeStepMotionIDs);
        WriteJSON(doc, alloc, "DashActionIDs", Settings::DashActionIDs);
        WriteJSON(doc, alloc, "DashMotionIDs", Settings::DashMotionIDs);
        doc.AddMember("DefaultInputsInitialized", Settings::DefaultInputsInitialized, alloc);

        doc.AddMember("DodgeRollCost", Settings::PlayerRollSettings.cost, alloc);
        doc.AddMember("DodgeRollTypeCost", Settings::PlayerRollSettings.costType, alloc);
        doc.AddMember("DodgeStepCost", Settings::PlayerStepSettings.cost, alloc);
        doc.AddMember("DodgeStepTypeCost", Settings::PlayerStepSettings.costType, alloc);
        doc.AddMember("DashCost", Settings::PlayerDashSettings.cost, alloc);
        doc.AddMember("DashTypeCost", Settings::PlayerDashSettings.costType, alloc);
        doc.AddMember("DodgeRollEnabled", Settings::DodgeRollEnabled, alloc);
        doc.AddMember("DodgeStepEnabled", Settings::DodgeStepEnabled, alloc);
        doc.AddMember("DashEnabled", Settings::DashEnabled, alloc);
        doc.AddMember("CanSpamDodge", Settings::PlayerRollSettings.canSpamDodge || Settings::PlayerStepSettings.canSpamDodge || Settings::PlayerDashSettings.canSpamDodge, alloc);
        doc.AddMember("EnablePerfectDodge", Settings::PlayerRollSettings.enablePerfectDodge || Settings::PlayerStepSettings.enablePerfectDodge || Settings::PlayerDashSettings.enablePerfectDodge, alloc);
        doc.AddMember("StopTimeOnPerfectDodge", Settings::PlayerRollSettings.stopTimeOnPerfectDodge || Settings::PlayerStepSettings.stopTimeOnPerfectDodge || Settings::PlayerDashSettings.stopTimeOnPerfectDodge, alloc);
        doc.AddMember("PerfectDodgeWindow", Settings::PlayerRollSettings.perfectDodgeWindow, alloc);
        doc.AddMember("TimeStopDuration", Settings::PlayerRollSettings.timeStopDuration, alloc);
        doc.AddMember("DodgeInvulnerabilityMethod", Settings::PlayerRollSettings.invulnerabilityMethod, alloc);
        doc.AddMember("DodgeInvincibilityDuration", Settings::PlayerRollSettings.invincibilityDuration, alloc);
        doc.AddMember("DodgeRollUpwards", Settings::DodgeRollUpwards, alloc);
        doc.AddMember("DodgeRollAerialBoost", Settings::DodgeRollAerialBoost, alloc);
        doc.AddMember("DodgeStepUpwards", Settings::DodgeStepUpwards, alloc);
        doc.AddMember("DodgeStepAerialBoost", Settings::DodgeStepAerialBoost, alloc);
        doc.AddMember("CancelDodgeWithAttacks", Settings::PlayerRollSettings.cancelDodgeWithAttacks, alloc);
        doc.AddMember("CancelAttackWithDodge", Settings::PlayerRollSettings.cancelAttackWithDodge, alloc);
        doc.AddMember("RollOnlyLightArmor", Settings::RollOnlyLightArmor, alloc);
        doc.AddMember("CanDodgeSheathed", Settings::CanDodgeSheathed, alloc);
        doc.AddMember("CancelDodgeWithPowerAttacks", Settings::PlayerRollSettings.cancelDodgeWithPowerAttacks, alloc);
        doc.AddMember("PlayerUseSeparateSheathedDistance", Settings::PlayerUseSeparateSheathedDistance, alloc);
        doc.AddMember("DashInvulnerabilityEnabled", Settings::PlayerDashSettings.invulnerabilityEnabled, alloc);
        doc.AddMember("DashInvulnerabilityMethod", Settings::PlayerDashSettings.invulnerabilityMethod, alloc);
        doc.AddMember("DashInvincibilityDuration", Settings::PlayerDashSettings.invincibilityDuration, alloc);
        WriteDodgeTypeSettings(doc, alloc, "PlayerRollSettings", Settings::PlayerRollSettings);
        WriteDodgeTypeSettings(doc, alloc, "PlayerStepSettings", Settings::PlayerStepSettings);
        WriteDodgeTypeSettings(doc, alloc, "PlayerDashSettings", Settings::PlayerDashSettings);

        rapidjson::Value prDist(rapidjson::kObjectType); SerializeDistance(prDist, alloc, Settings::PlayerRollDistance); doc.AddMember("PlayerRollDistance", prDist, alloc);
        rapidjson::Value psDist(rapidjson::kObjectType); SerializeDistance(psDist, alloc, Settings::PlayerStepDistance); doc.AddMember("PlayerStepDistance", psDist, alloc);
        rapidjson::Value prsDist(rapidjson::kObjectType); SerializeDistance(prsDist, alloc, Settings::PlayerRollSheathedDistance); doc.AddMember("PlayerRollSheathedDistance", prsDist, alloc);
        rapidjson::Value pssDist(rapidjson::kObjectType); SerializeDistance(pssDist, alloc, Settings::PlayerStepSheathedDistance); doc.AddMember("PlayerStepSheathedDistance", pssDist, alloc);
        rapidjson::Value pdDist(rapidjson::kObjectType); SerializeDistance(pdDist, alloc, Settings::PlayerDashDistance); doc.AddMember("PlayerDashDistance", pdDist, alloc);
        rapidjson::Value pdsDist(rapidjson::kObjectType); SerializeDistance(pdsDist, alloc, Settings::PlayerDashSheathedDistance); doc.AddMember("PlayerDashSheathedDistance", pdsDist, alloc);

        WritePerk(doc, alloc, "PerkCanDodgeSheathed", Settings::PerkCanDodgeSheathed);
        WritePerk(doc, alloc, "PerkStepDodge", Settings::PerkStepDodge);
        WritePerk(doc, alloc, "PerkRollDodge", Settings::PerkRollDodge);
        WritePerk(doc, alloc, "PerkRollUpwards", Settings::PerkRollUpwards);
        WritePerk(doc, alloc, "PerkStepUpwards", Settings::PerkStepUpwards);
        WritePerk(doc, alloc, "PerkCanSpamDodge", Settings::PerkCanSpamDodge);
        WritePerk(doc, alloc, "PerkEnablePerfectDodge", Settings::PerkEnablePerfectDodge);
        WritePerk(doc, alloc, "PerkCancelDodgeWithAttacks", Settings::PerkCancelDodgeWithAttacks);
        WritePerk(doc, alloc, "PerkCancelAttackWithDodge", Settings::PerkCancelAttackWithDodge);
        WritePerk(doc, alloc, "PerkStopTimeOnPerfectDodge", Settings::PerkStopTimeOnPerfectDodge);
        WritePerk(doc, alloc, "PerkCancelDodgeWithPowerAttacks", Settings::PerkCancelDodgeWithPowerAttacks);
    }

    void WriteNPCSettings(rapidjson::Value& doc, rapidjson::Document::AllocatorType& alloc) {
        rapidjson::Value nDist(rapidjson::kObjectType); SerializeDistance(nDist, alloc, Settings::NPCGlobalDistance); doc.AddMember("NPCGlobalDistance", nDist, alloc);
        rapidjson::Value nRollDist(rapidjson::kObjectType); SerializeDistance(nRollDist, alloc, Settings::NPCGlobalRollDistance); doc.AddMember("NPCGlobalRollDistance", nRollDist, alloc);
        rapidjson::Value nStepDist(rapidjson::kObjectType); SerializeDistance(nStepDist, alloc, Settings::NPCGlobalStepDistance); doc.AddMember("NPCGlobalStepDistance", nStepDist, alloc);
        rapidjson::Value nDashDist(rapidjson::kObjectType); SerializeDistance(nDashDist, alloc, Settings::NPCGlobalDashDistance); doc.AddMember("NPCGlobalDashDistance", nDashDist, alloc);

        WritePerk(doc, alloc, "NPCDisableDodgePerk", Settings::NPCDisableDodgePerk);
        doc.AddMember("NPCDodgeEnabled", Settings::NPCDodgeEnabled, alloc);
        doc.AddMember("NPCDodgeProjectiles", Settings::NPCDodgeProjectiles, alloc);
        doc.AddMember("NPCProjRequireLoS", Settings::NPCProjRequireLoS, alloc);
        doc.AddMember("NPCMeleeUseArea", Settings::NPCMeleeUseArea, alloc);
        doc.AddMember("NPCDodgeType", Settings::NPCDodgeType, alloc);
        doc.AddMember("NPCInvulnerabilityMethod", Settings::NPCRollSettings.invulnerabilityMethod, alloc);
        doc.AddMember("NPCInvincibilityDuration", Settings::NPCRollSettings.invincibilityDuration, alloc);
        doc.AddMember("NPCCancelDodgeWithAttacks", Settings::NPCRollSettings.cancelDodgeWithAttacks, alloc);
        doc.AddMember("NPCCancelAttackWithDodge", Settings::NPCRollSettings.cancelAttackWithDodge, alloc);
        doc.AddMember("NPCCanSpamDodge", Settings::NPCRollSettings.canSpamDodge || Settings::NPCStepSettings.canSpamDodge || Settings::NPCDashSettings.canSpamDodge, alloc);
        doc.AddMember("NPCAllowStaggerDodge", Settings::NPCAllowStaggerDodge, alloc);
        doc.AddMember("NPCRollCost", Settings::NPCRollSettings.cost, alloc);
        doc.AddMember("NPCRollTypeCost", Settings::NPCRollSettings.costType, alloc);
        doc.AddMember("NPCStepCost", Settings::NPCStepSettings.cost, alloc);
        doc.AddMember("NPCStepTypeCost", Settings::NPCStepSettings.costType, alloc);
        doc.AddMember("NPCDashCost", Settings::NPCDashSettings.cost, alloc);
        doc.AddMember("NPCDashTypeCost", Settings::NPCDashSettings.costType, alloc);
        doc.AddMember("NPCRollOnlyLightArmor", Settings::NPCRollOnlyLightArmor, alloc);
        doc.AddMember("NPCMeleeRequireLoS", Settings::NPCMeleeRequireLoS, alloc);
        doc.AddMember("NPCMeleeDodgeEnabled", Settings::NPCMeleeDodgeEnabled, alloc);
        doc.AddMember("NPCDashGapCloseEnabled", Settings::NPCDashGapCloseEnabled, alloc);
        doc.AddMember("NPCRandomDodgeEnabled", Settings::NPCRandomDodgeEnabled, alloc);
        doc.AddMember("NPCRandomDodgeChance", Settings::NPCRandomDodgeChance, alloc);
        doc.AddMember("NPCRandomDodgeUseArea", Settings::NPCRandomDodgeUseArea, alloc);
        doc.AddMember("NPCRandomDodgeRequireLoS", Settings::NPCRandomDodgeRequireLoS, alloc);
        WriteStringJSON(doc, alloc, "NPCRandomDodgeEvents", Settings::NPCRandomDodgeEvents);
        doc.AddMember("NPCSmartDodge", Settings::NPCSmartDodge, alloc);
        doc.AddMember("NPCCancelDodgeWithPowerAttacks", Settings::NPCRollSettings.cancelDodgeWithPowerAttacks, alloc);
        doc.AddMember("NPCDashInvulnerabilityEnabled", Settings::NPCDashSettings.invulnerabilityEnabled, alloc);
        doc.AddMember("NPCDashInvulnerabilityMethod", Settings::NPCDashSettings.invulnerabilityMethod, alloc);
        doc.AddMember("NPCDashInvincibilityDuration", Settings::NPCDashSettings.invincibilityDuration, alloc);
        WriteDodgeTypeSettings(doc, alloc, "NPCRollSettings", Settings::NPCRollSettings);
        WriteDodgeTypeSettings(doc, alloc, "NPCStepSettings", Settings::NPCStepSettings);
        WriteDodgeTypeSettings(doc, alloc, "NPCDashSettings", Settings::NPCDashSettings);
        WriteChance(doc, alloc, "NPCGlobalChance", Settings::NPCGlobalChance);
        WriteChance(doc, alloc, "NPCGlobalDashChance", Settings::NPCGlobalDashChance);
    }

    void ReadPlayerSettings(const rapidjson::Value& doc) {
        ReadJSON(doc, "DodgeRollActionIDs", Settings::DodgeRollActionIDs);
        ReadJSON(doc, "DodgeRollMotionIDs", Settings::DodgeRollMotionIDs);
        ReadJSON(doc, "DodgeStepActionIDs", Settings::DodgeStepActionIDs);
        ReadJSON(doc, "DodgeStepMotionIDs", Settings::DodgeStepMotionIDs);
        ReadJSON(doc, "DashActionIDs", Settings::DashActionIDs);
        ReadJSON(doc, "DashMotionIDs", Settings::DashMotionIDs);
        Settings::DefaultInputsInitialized = doc.HasMember("DefaultInputsInitialized") && doc["DefaultInputsInitialized"].IsBool()
            ? doc["DefaultInputsInitialized"].GetBool()
            : true;

        if (doc.HasMember("DodgeRollCost")) Settings::DodgeRollCost = doc["DodgeRollCost"].GetFloat();
        if (doc.HasMember("DodgeRollTypeCost")) Settings::DodgeRollTypeCost = doc["DodgeRollTypeCost"].GetInt();
        if (doc.HasMember("DodgeStepCost")) Settings::DodgeStepCost = doc["DodgeStepCost"].GetFloat();
        if (doc.HasMember("DodgeStepTypeCost")) Settings::DodgeStepTypeCost = doc["DodgeStepTypeCost"].GetInt();
        if (doc.HasMember("DashCost")) Settings::DashCost = doc["DashCost"].GetFloat();
        if (doc.HasMember("DashTypeCost")) Settings::DashTypeCost = doc["DashTypeCost"].GetInt();
        if (doc.HasMember("DodgeRollEnabled")) Settings::DodgeRollEnabled = doc["DodgeRollEnabled"].GetBool();
        if (doc.HasMember("DodgeStepEnabled")) Settings::DodgeStepEnabled = doc["DodgeStepEnabled"].GetBool();
        if (doc.HasMember("DashEnabled")) Settings::DashEnabled = doc["DashEnabled"].GetBool();
        if (doc.HasMember("CanSpamDodge")) Settings::CanSpamDodge = doc["CanSpamDodge"].GetBool();
        if (doc.HasMember("EnablePerfectDodge")) Settings::EnablePerfectDodge = doc["EnablePerfectDodge"].GetBool();
        if (doc.HasMember("StopTimeOnPerfectDodge")) Settings::StopTimeOnPerfectDodge = doc["StopTimeOnPerfectDodge"].GetBool();
        if (doc.HasMember("PerfectDodgeWindow")) Settings::PerfectDodgeWindow = doc["PerfectDodgeWindow"].GetFloat();
        if (doc.HasMember("TimeStopDuration")) Settings::TimeStopDuration = doc["TimeStopDuration"].GetFloat();
        if (doc.HasMember("DodgeInvulnerabilityMethod")) Settings::DodgeInvulnerabilityMethod = doc["DodgeInvulnerabilityMethod"].GetInt();
        if (doc.HasMember("DodgeInvincibilityDuration")) Settings::DodgeInvincibilityDuration = doc["DodgeInvincibilityDuration"].GetFloat();
        if (doc.HasMember("DodgeRollUpwards")) Settings::DodgeRollUpwards = doc["DodgeRollUpwards"].GetBool();
        if (doc.HasMember("DodgeRollAerialBoost")) Settings::DodgeRollAerialBoost = doc["DodgeRollAerialBoost"].GetFloat();
        if (doc.HasMember("DodgeStepUpwards")) Settings::DodgeStepUpwards = doc["DodgeStepUpwards"].GetBool();
        if (doc.HasMember("DodgeStepAerialBoost")) Settings::DodgeStepAerialBoost = doc["DodgeStepAerialBoost"].GetFloat();
        if (doc.HasMember("CancelDodgeWithAttacks")) Settings::CancelDodgeWithAttacks = doc["CancelDodgeWithAttacks"].GetBool();
        if (doc.HasMember("CancelAttackWithDodge")) Settings::CancelAttackWithDodge = doc["CancelAttackWithDodge"].GetBool();
        if (doc.HasMember("RollOnlyLightArmor")) Settings::RollOnlyLightArmor = doc["RollOnlyLightArmor"].GetBool();
        if (doc.HasMember("CanDodgeSheathed")) Settings::CanDodgeSheathed = doc["CanDodgeSheathed"].GetBool();
        if (doc.HasMember("CancelDodgeWithPowerAttacks")) Settings::CancelDodgeWithPowerAttacks = doc["CancelDodgeWithPowerAttacks"].GetBool();
        if (doc.HasMember("PlayerUseSeparateSheathedDistance")) Settings::PlayerUseSeparateSheathedDistance = doc["PlayerUseSeparateSheathedDistance"].GetBool();
        if (doc.HasMember("DashInvulnerabilityEnabled")) Settings::DashInvulnerabilityEnabled = doc["DashInvulnerabilityEnabled"].GetBool();
        if (doc.HasMember("DashInvulnerabilityMethod")) Settings::DashInvulnerabilityMethod = doc["DashInvulnerabilityMethod"].GetInt();
        if (doc.HasMember("DashInvincibilityDuration")) Settings::DashInvincibilityDuration = doc["DashInvincibilityDuration"].GetFloat();

        Settings::PlayerRollSettings.cost = Settings::DodgeRollCost;
        Settings::PlayerRollSettings.costType = Settings::DodgeRollTypeCost;
        Settings::PlayerRollSettings.invulnerabilityMethod = Settings::DodgeInvulnerabilityMethod;
        Settings::PlayerRollSettings.invincibilityDuration = Settings::DodgeInvincibilityDuration;
        Settings::PlayerRollSettings.enablePerfectDodge = Settings::EnablePerfectDodge;
        Settings::PlayerRollSettings.perfectDodgeWindow = Settings::PerfectDodgeWindow;
        Settings::PlayerRollSettings.stopTimeOnPerfectDodge = Settings::StopTimeOnPerfectDodge;
        Settings::PlayerRollSettings.timeStopDuration = Settings::TimeStopDuration;
        Settings::PlayerRollSettings.cancelDodgeWithAttacks = Settings::CancelDodgeWithAttacks;
        Settings::PlayerRollSettings.cancelDodgeWithPowerAttacks = Settings::CancelDodgeWithPowerAttacks;
        Settings::PlayerRollSettings.cancelAttackWithDodge = Settings::CancelAttackWithDodge;
        Settings::PlayerRollSettings.canSpamDodge = Settings::CanSpamDodge;

        Settings::PlayerStepSettings.cost = Settings::DodgeStepCost;
        Settings::PlayerStepSettings.costType = Settings::DodgeStepTypeCost;
        Settings::PlayerStepSettings.invulnerabilityMethod = Settings::DodgeInvulnerabilityMethod;
        Settings::PlayerStepSettings.invincibilityDuration = Settings::DodgeInvincibilityDuration;
        Settings::PlayerStepSettings.enablePerfectDodge = Settings::EnablePerfectDodge;
        Settings::PlayerStepSettings.perfectDodgeWindow = Settings::PerfectDodgeWindow;
        Settings::PlayerStepSettings.stopTimeOnPerfectDodge = Settings::StopTimeOnPerfectDodge;
        Settings::PlayerStepSettings.timeStopDuration = Settings::TimeStopDuration;
        Settings::PlayerStepSettings.cancelDodgeWithAttacks = Settings::CancelDodgeWithAttacks;
        Settings::PlayerStepSettings.cancelDodgeWithPowerAttacks = Settings::CancelDodgeWithPowerAttacks;
        Settings::PlayerStepSettings.cancelAttackWithDodge = Settings::CancelAttackWithDodge;
        Settings::PlayerStepSettings.canSpamDodge = Settings::CanSpamDodge;

        Settings::PlayerDashSettings.cost = Settings::DashCost;
        Settings::PlayerDashSettings.costType = Settings::DashTypeCost;
        Settings::PlayerDashSettings.invulnerabilityEnabled = Settings::DashInvulnerabilityEnabled;
        Settings::PlayerDashSettings.invulnerabilityMethod = Settings::DashInvulnerabilityMethod;
        Settings::PlayerDashSettings.invincibilityDuration = Settings::DashInvincibilityDuration;
        Settings::PlayerDashSettings.stopTimeOnPerfectDodge = Settings::StopTimeOnPerfectDodge;
        Settings::PlayerDashSettings.timeStopDuration = Settings::TimeStopDuration;
        Settings::PlayerDashSettings.cancelDodgeWithAttacks = Settings::CancelDodgeWithAttacks;
        Settings::PlayerDashSettings.cancelDodgeWithPowerAttacks = Settings::CancelDodgeWithPowerAttacks;
        Settings::PlayerDashSettings.cancelAttackWithDodge = Settings::CancelAttackWithDodge;
        Settings::PlayerDashSettings.canSpamDodge = Settings::CanSpamDodge;

        ReadDodgeTypeSettings(doc, "PlayerRollSettings", Settings::PlayerRollSettings);
        ReadDodgeTypeSettings(doc, "PlayerStepSettings", Settings::PlayerStepSettings);
        ReadDodgeTypeSettings(doc, "PlayerDashSettings", Settings::PlayerDashSettings);

        if (doc.HasMember("PlayerRollDistance") && doc["PlayerRollDistance"].IsObject()) DeserializeDistance(doc["PlayerRollDistance"], Settings::PlayerRollDistance);
        if (doc.HasMember("PlayerStepDistance") && doc["PlayerStepDistance"].IsObject()) DeserializeDistance(doc["PlayerStepDistance"], Settings::PlayerStepDistance);
        if (doc.HasMember("PlayerRollSheathedDistance") && doc["PlayerRollSheathedDistance"].IsObject()) DeserializeDistance(doc["PlayerRollSheathedDistance"], Settings::PlayerRollSheathedDistance);
        if (doc.HasMember("PlayerStepSheathedDistance") && doc["PlayerStepSheathedDistance"].IsObject()) DeserializeDistance(doc["PlayerStepSheathedDistance"], Settings::PlayerStepSheathedDistance);
        if (doc.HasMember("PlayerDashDistance") && doc["PlayerDashDistance"].IsObject()) DeserializeDistance(doc["PlayerDashDistance"], Settings::PlayerDashDistance);
        if (doc.HasMember("PlayerDashSheathedDistance") && doc["PlayerDashSheathedDistance"].IsObject()) DeserializeDistance(doc["PlayerDashSheathedDistance"], Settings::PlayerDashSheathedDistance);

        ReadPerk(doc, "PerkCanDodgeSheathed", Settings::PerkCanDodgeSheathed);
        ReadPerk(doc, "PerkStepDodge", Settings::PerkStepDodge);
        ReadPerk(doc, "PerkRollDodge", Settings::PerkRollDodge);
        ReadPerk(doc, "PerkRollUpwards", Settings::PerkRollUpwards);
        ReadPerk(doc, "PerkStepUpwards", Settings::PerkStepUpwards);
        ReadPerk(doc, "PerkCanSpamDodge", Settings::PerkCanSpamDodge);
        ReadPerk(doc, "PerkEnablePerfectDodge", Settings::PerkEnablePerfectDodge);
        ReadPerk(doc, "PerkCancelDodgeWithAttacks", Settings::PerkCancelDodgeWithAttacks);
        ReadPerk(doc, "PerkCancelAttackWithDodge", Settings::PerkCancelAttackWithDodge);
        ReadPerk(doc, "PerkStopTimeOnPerfectDodge", Settings::PerkStopTimeOnPerfectDodge);
        ReadPerk(doc, "PerkCancelDodgeWithPowerAttacks", Settings::PerkCancelDodgeWithPowerAttacks);
    }

    void ReadNPCSettings(const rapidjson::Value& doc) {
        if (doc.HasMember("NPCGlobalDistance") && doc["NPCGlobalDistance"].IsObject()) {
            DeserializeDistance(doc["NPCGlobalDistance"], Settings::NPCGlobalDistance);
            Settings::NPCGlobalRollDistance = Settings::NPCGlobalDistance;
            Settings::NPCGlobalStepDistance = Settings::NPCGlobalDistance;
        }
        if (doc.HasMember("NPCGlobalRollDistance") && doc["NPCGlobalRollDistance"].IsObject()) DeserializeDistance(doc["NPCGlobalRollDistance"], Settings::NPCGlobalRollDistance);
        if (doc.HasMember("NPCGlobalStepDistance") && doc["NPCGlobalStepDistance"].IsObject()) DeserializeDistance(doc["NPCGlobalStepDistance"], Settings::NPCGlobalStepDistance);
        if (doc.HasMember("NPCGlobalDashDistance") && doc["NPCGlobalDashDistance"].IsObject()) DeserializeDistance(doc["NPCGlobalDashDistance"], Settings::NPCGlobalDashDistance);

        ReadPerk(doc, "NPCDisableDodgePerk", Settings::NPCDisableDodgePerk);
        if (doc.HasMember("NPCDodgeEnabled")) Settings::NPCDodgeEnabled = doc["NPCDodgeEnabled"].GetBool();
        if (doc.HasMember("NPCDodgeProjectiles")) Settings::NPCDodgeProjectiles = doc["NPCDodgeProjectiles"].GetInt();
        if (doc.HasMember("NPCProjRequireLoS")) Settings::NPCProjRequireLoS = doc["NPCProjRequireLoS"].GetBool();
        if (doc.HasMember("NPCMeleeUseArea")) Settings::NPCMeleeUseArea = doc["NPCMeleeUseArea"].GetBool();
        if (doc.HasMember("NPCDodgeType")) Settings::NPCDodgeType = doc["NPCDodgeType"].GetInt();
        if (doc.HasMember("NPCInvulnerabilityMethod")) Settings::NPCInvulnerabilityMethod = doc["NPCInvulnerabilityMethod"].GetInt();
        if (doc.HasMember("NPCInvincibilityDuration")) Settings::NPCInvincibilityDuration = doc["NPCInvincibilityDuration"].GetFloat();
        if (doc.HasMember("NPCCancelDodgeWithAttacks")) Settings::NPCCancelDodgeWithAttacks = doc["NPCCancelDodgeWithAttacks"].GetBool();
        if (doc.HasMember("NPCCancelAttackWithDodge")) Settings::NPCCancelAttackWithDodge = doc["NPCCancelAttackWithDodge"].GetBool();
        if (doc.HasMember("NPCCanSpamDodge")) Settings::NPCCanSpamDodge = doc["NPCCanSpamDodge"].GetBool();
        if (doc.HasMember("NPCAllowStaggerDodge")) Settings::NPCAllowStaggerDodge = doc["NPCAllowStaggerDodge"].GetBool();
        if (doc.HasMember("NPCRollCost")) Settings::NPCRollCost = doc["NPCRollCost"].GetFloat();
        if (doc.HasMember("NPCRollTypeCost")) Settings::NPCRollTypeCost = doc["NPCRollTypeCost"].GetInt();
        if (doc.HasMember("NPCStepCost")) Settings::NPCStepCost = doc["NPCStepCost"].GetFloat();
        if (doc.HasMember("NPCStepTypeCost")) Settings::NPCStepTypeCost = doc["NPCStepTypeCost"].GetInt();
        if (doc.HasMember("NPCRollOnlyLightArmor")) Settings::NPCRollOnlyLightArmor = doc["NPCRollOnlyLightArmor"].GetBool();
        if (doc.HasMember("NPCMeleeRequireLoS")) Settings::NPCMeleeRequireLoS = doc["NPCMeleeRequireLoS"].GetBool();
        if (doc.HasMember("NPCMeleeDodgeEnabled")) Settings::NPCMeleeDodgeEnabled = doc["NPCMeleeDodgeEnabled"].GetBool();
        if (doc.HasMember("NPCDashGapCloseEnabled")) Settings::NPCDashGapCloseEnabled = doc["NPCDashGapCloseEnabled"].GetBool();
        if (doc.HasMember("NPCRandomDodgeEnabled")) Settings::NPCRandomDodgeEnabled = doc["NPCRandomDodgeEnabled"].GetBool();
        if (doc.HasMember("NPCRandomDodgeChance")) Settings::NPCRandomDodgeChance = doc["NPCRandomDodgeChance"].GetFloat();
        if (doc.HasMember("NPCRandomDodgeUseArea")) Settings::NPCRandomDodgeUseArea = doc["NPCRandomDodgeUseArea"].GetBool();
        if (doc.HasMember("NPCRandomDodgeRequireLoS")) Settings::NPCRandomDodgeRequireLoS = doc["NPCRandomDodgeRequireLoS"].GetBool();
        ReadStringJSON(doc, "NPCRandomDodgeEvents", Settings::NPCRandomDodgeEvents);
        if (doc.HasMember("NPCSmartDodge")) Settings::NPCSmartDodge = doc["NPCSmartDodge"].GetBool();
        if (doc.HasMember("NPCCancelDodgeWithPowerAttacks")) Settings::NPCCancelDodgeWithPowerAttacks = doc["NPCCancelDodgeWithPowerAttacks"].GetBool();
        if (doc.HasMember("NPCDashInvulnerabilityEnabled")) Settings::NPCDashInvulnerabilityEnabled = doc["NPCDashInvulnerabilityEnabled"].GetBool();
        if (doc.HasMember("NPCDashInvulnerabilityMethod")) Settings::NPCDashInvulnerabilityMethod = doc["NPCDashInvulnerabilityMethod"].GetInt();
        if (doc.HasMember("NPCDashInvincibilityDuration")) Settings::NPCDashInvincibilityDuration = doc["NPCDashInvincibilityDuration"].GetFloat();
        if (doc.HasMember("NPCDashCost")) Settings::NPCDashCost = doc["NPCDashCost"].GetFloat();
        if (doc.HasMember("NPCDashTypeCost")) Settings::NPCDashTypeCost = doc["NPCDashTypeCost"].GetInt();

        Settings::NPCRollSettings.cost = Settings::NPCRollCost;
        Settings::NPCRollSettings.costType = Settings::NPCRollTypeCost;
        Settings::NPCRollSettings.invulnerabilityMethod = Settings::NPCInvulnerabilityMethod;
        Settings::NPCRollSettings.invincibilityDuration = Settings::NPCInvincibilityDuration;
        Settings::NPCRollSettings.cancelDodgeWithAttacks = Settings::NPCCancelDodgeWithAttacks;
        Settings::NPCRollSettings.cancelDodgeWithPowerAttacks = Settings::NPCCancelDodgeWithPowerAttacks;
        Settings::NPCRollSettings.cancelAttackWithDodge = Settings::NPCCancelAttackWithDodge;
        Settings::NPCRollSettings.canSpamDodge = Settings::NPCCanSpamDodge;

        Settings::NPCStepSettings.cost = Settings::NPCStepCost;
        Settings::NPCStepSettings.costType = Settings::NPCStepTypeCost;
        Settings::NPCStepSettings.invulnerabilityMethod = Settings::NPCInvulnerabilityMethod;
        Settings::NPCStepSettings.invincibilityDuration = Settings::NPCInvincibilityDuration;
        Settings::NPCStepSettings.cancelDodgeWithAttacks = Settings::NPCCancelDodgeWithAttacks;
        Settings::NPCStepSettings.cancelDodgeWithPowerAttacks = Settings::NPCCancelDodgeWithPowerAttacks;
        Settings::NPCStepSettings.cancelAttackWithDodge = Settings::NPCCancelAttackWithDodge;
        Settings::NPCStepSettings.canSpamDodge = Settings::NPCCanSpamDodge;

        Settings::NPCDashSettings.cost = Settings::NPCDashCost;
        Settings::NPCDashSettings.costType = Settings::NPCDashTypeCost;
        Settings::NPCDashSettings.invulnerabilityEnabled = Settings::NPCDashInvulnerabilityEnabled;
        Settings::NPCDashSettings.invulnerabilityMethod = Settings::NPCDashInvulnerabilityMethod;
        Settings::NPCDashSettings.invincibilityDuration = Settings::NPCDashInvincibilityDuration;
        Settings::NPCDashSettings.cancelDodgeWithAttacks = Settings::NPCCancelDodgeWithAttacks;
        Settings::NPCDashSettings.cancelDodgeWithPowerAttacks = Settings::NPCCancelDodgeWithPowerAttacks;
        Settings::NPCDashSettings.cancelAttackWithDodge = Settings::NPCCancelAttackWithDodge;
        Settings::NPCDashSettings.canSpamDodge = Settings::NPCCanSpamDodge;

        ReadDodgeTypeSettings(doc, "NPCRollSettings", Settings::NPCRollSettings);
        ReadDodgeTypeSettings(doc, "NPCStepSettings", Settings::NPCStepSettings);
        ReadDodgeTypeSettings(doc, "NPCDashSettings", Settings::NPCDashSettings);

        ReadChance(doc, "NPCGlobalChance", Settings::NPCGlobalChance);
        Settings::NPCGlobalDashChance = Settings::NPCGlobalChance;
        ReadChance(doc, "NPCGlobalDashChance", Settings::NPCGlobalDashChance);
    }

    void SaveSettings() {
        std::error_code ec;
        std::filesystem::create_directories(MOD_DIR, ec);
        std::filesystem::create_directories(RULES_DIR, ec);

        rapidjson::Document playerDoc;
        playerDoc.SetObject();
        WritePlayerSettings(playerDoc, playerDoc.GetAllocator());
        bool savedPlayer = SaveJSONFile(PLAYER_SETTINGS_PATH, playerDoc);

        rapidjson::Document npcDoc;
        npcDoc.SetObject();
        WriteNPCSettings(npcDoc, npcDoc.GetAllocator());
        bool savedNPC = SaveJSONFile(NPC_SETTINGS_PATH, npcDoc);

        bool savedRules = true;
        for (const auto& rule : Settings::NPCRules) {
            savedRules = SaveRule(rule) && savedRules;
        }

        if (!savedPlayer || !savedNPC || !savedRules) {
            SKSE::log::error("[Settings] Uma ou mais configuracoes falharam ao salvar. Player: {} | NPC: {} | Rules: {}",
                savedPlayer, savedNPC, savedRules);
        }
    }

    void LoadSettings() {
        rapidjson::Document playerDoc;
        bool loadedPlayer = LoadJSONFile(PLAYER_SETTINGS_PATH, playerDoc);
        if (!loadedPlayer) {
            loadedPlayer = LoadJSONFile(OLD_PLAYER_SETTINGS_PATH, playerDoc);
        }
        if (loadedPlayer) {
            ReadPlayerSettings(playerDoc);
        }

        rapidjson::Document npcDoc;
        bool loadedNPC = LoadJSONFile(NPC_SETTINGS_PATH, npcDoc);
        if (!loadedNPC) {
            loadedNPC = LoadJSONFile(OLD_NPC_SETTINGS_PATH, npcDoc);
        }
        if (loadedNPC) {
            ReadNPCSettings(npcDoc);
        }

        if (!loadedPlayer || !loadedNPC) {
            rapidjson::Document legacyDoc;
            bool loadedLegacy = LoadJSONFile(SETTINGS_PATH, legacyDoc);
            if (!loadedLegacy) {
                loadedLegacy = LoadJSONFile(OLD_SETTINGS_PATH, legacyDoc);
            }

            if (loadedLegacy) {
                if (!loadedPlayer) ReadPlayerSettings(legacyDoc);
                if (!loadedNPC) ReadNPCSettings(legacyDoc);
            }
        }

        LoadRules();
    }

    void NPCRender() {
        bool changed = false;

        const char* projTypes[] = { GetLoc("menu.npc_proj_both", "Both (Magic and Arrows)"), GetLoc("menu.npc_proj_magic", "Only Magic"), GetLoc("menu.npc_proj_arrows", "Only Arrows"), GetLoc("menu.npc_proj_disabled", "Disabled") };
        const char* dodgeTypes[] = { GetLoc("menu.npc_type_both", "Both"), GetLoc("menu.npc_type_step", "Only Step"), GetLoc("menu.npc_type_roll", "Only Roll") };
        const char* invulnMethods[] = { GetLoc("menu.invuln_custom", "Custom Time"), GetLoc("menu.invuln_anim", "Animation Time") };
        const char* costTypes[] = { GetLoc("menu.cost_health", "Health"), GetLoc("menu.cost_stamina", "Stamina"), GetLoc("menu.cost_magicka", "Magicka") };

        if (ImGui::Checkbox(GetLoc("menu.npc_enable", "Enable NPC Dodge"), &Settings::NPCDodgeEnabled)) changed = true;
        if (DrawDropdown(GetLoc("menu.npc_disable_perk", "Disable Dodge For NPCs With This Perk"), "Perk", Settings::NPCDisableDodgePerk, 300.0f)) changed = true;
        if (!Settings::NPCDodgeEnabled) {
            if (changed) SaveSettings();
            return;
        }

        ImGui::Separator();

        bool dummyUpward = false; RE::FormID dummyPerk = 0;

        ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "%s", GetLoc("menu.proj_detection_header", "Projectile Detection"));
        if (ImGui::Combo(GetLoc("menu.npc_proj_dodge", "Dodge Projectiles"), &Settings::NPCDodgeProjectiles, projTypes, 4)) changed = true;
        if (Settings::NPCDodgeProjectiles != 3) {
            if (ImGui::Checkbox(GetLoc("menu.proj_require_los", "Require Line of Sight (Vision) - Projectiles"), &Settings::NPCProjRequireLoS)) changed = true;
        }

        ImGui::Spacing();
        ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "%s", GetLoc("menu.melee_detection_header", "Melee Detection"));
        if (ImGui::Checkbox(GetLoc("menu.enable_melee_dodge", "Enable Melee Dodge"), &Settings::NPCMeleeDodgeEnabled)) changed = true;
        if (Settings::NPCMeleeDodgeEnabled) {
            if (ImGui::Checkbox(GetLoc("menu.melee_use_area", "Use Area (AoE) - Melee"), &Settings::NPCMeleeUseArea)) changed = true;
            if (Settings::NPCMeleeUseArea) {
                if (ImGui::Checkbox(GetLoc("menu.melee_require_los", "Require Line of Sight (Vision) - Melee Area"), &Settings::NPCMeleeRequireLoS)) changed = true;
            }
        }
        if (ImGui::Checkbox(GetLoc("menu.npc_dash_gap_close", "Enable Dash For Gap Close"), &Settings::NPCDashGapCloseEnabled)) changed = true;
        ImGui::Separator();
        ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "%s", GetLoc("menu.random_dodge_header", "Random Dodge Events"));
        DrawRandomDodgeUI("npc_random_dodge", Settings::NPCRandomDodgeEnabled, Settings::NPCRandomDodgeChance, Settings::NPCRandomDodgeUseArea, Settings::NPCRandomDodgeRequireLoS, Settings::NPCRandomDodgeEvents, changed);

        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Combo(GetLoc("menu.npc_dodge_type", "Dodge Type"), &Settings::NPCDodgeType, dodgeTypes, 3)) changed = true;
        if (ImGui::Checkbox(GetLoc("menu.roll_only_light_armor", "Roll Dodge Only For Light Armor"), &Settings::NPCRollOnlyLightArmor)) changed = true;
        if (ImGui::Checkbox(GetLoc("menu.npc_allow_stagger", "Allow Dodge While Staggered"), &Settings::NPCAllowStaggerDodge)) changed = true;
        if (ImGui::Checkbox(GetLoc("menu.npc_smart_dodge", "Smart Dodge"), &Settings::NPCSmartDodge)) changed = true;
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", GetLoc("menu.npc_smart_dodge_hover", "Enables advanced pathfinding scanning using NavMesh to dodge obstacles. If disabled, default directional layout is used."));
            ImGui::EndTooltip();
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader(GetLoc("menu.roll_header", "Roll Dodge"))) {
            ImGui::Indent();
            DrawDistanceSettingsUI("npc_global_roll_dist", Settings::NPCGlobalRollDistance, changed, false, dummyUpward, dummyPerk, GetLoc("menu.npc_roll_distance", "NPC Roll Dodge Distance"));
            DrawDodgeTypeRuntimeUI("npc_roll_runtime", Settings::NPCRollSettings, changed, costTypes, invulnMethods);
            ImGui::Unindent();
        }
        if (ImGui::CollapsingHeader(GetLoc("menu.step_header", "Step Dodge"))) {
            ImGui::Indent();
            DrawDistanceSettingsUI("npc_global_step_dist", Settings::NPCGlobalStepDistance, changed, false, dummyUpward, dummyPerk, GetLoc("menu.npc_step_distance", "NPC Step Dodge Distance"));
            DrawDodgeTypeRuntimeUI("npc_step_runtime", Settings::NPCStepSettings, changed, costTypes, invulnMethods);
            ImGui::Unindent();
        }
        if (ImGui::CollapsingHeader(GetLoc("menu.dash_header", "Dash"))) {
            ImGui::Indent();
            DrawChanceUI(GetLoc("menu.npc_global_dash_chance", "Global Dash Chance Configuration"), Settings::NPCGlobalDashChance, changed);
            DrawDistanceSettingsUI("npc_global_dash_dist", Settings::NPCGlobalDashDistance, changed, false, dummyUpward, dummyPerk, GetLoc("menu.npc_dash_distance", "NPC Dash Distance"));
            DrawDodgeTypeRuntimeUI("npc_dash_runtime", Settings::NPCDashSettings, changed, costTypes, invulnMethods);
            ImGui::Unindent();
        }

        ImGui::Separator();
        DrawChanceUI(GetLoc("menu.npc_global_chance", "Global Dodge Chance Configuration"), Settings::NPCGlobalChance, changed);

        if (changed) SaveSettings();
    }

    void RulesRender() {
        bool changed = false;
        bool dummyUpward = false;
        RE::FormID dummyPerk = 0;

        const char* projTypes[] = { GetLoc("menu.npc_proj_both", "Both (Magic and Arrows)"), GetLoc("menu.npc_proj_magic", "Only Magic"), GetLoc("menu.npc_proj_arrows", "Only Arrows"), GetLoc("menu.npc_proj_disabled", "Disabled") };
        const char* dodgeTypes[] = { GetLoc("menu.npc_type_both", "Both"), GetLoc("menu.npc_type_step", "Only Step"), GetLoc("menu.npc_type_roll", "Only Roll") };
        const char* invulnMethods[] = { GetLoc("menu.invuln_custom", "Custom Time"), GetLoc("menu.invuln_anim", "Animation Time") };
        const char* costTypes[] = { GetLoc("menu.cost_health", "Health"), GetLoc("menu.cost_stamina", "Stamina"), GetLoc("menu.cost_magicka", "Magicka") };

        ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "%s", GetLoc("menu.npc_rules_header", "NPC Dodge Rules (By Perk)"));

        if (ImGui::Button(GetLoc("menu.add_rule", "+ Add Rule"))) {
            Settings::NPCDodgeRule newRule;
            newRule.ruleName = "New Rule " + std::to_string(Settings::NPCRules.size() + 1);
            Settings::NPCRules.push_back(newRule);
            changed = true;
        }

        ImGui::Spacing();

        for (size_t i = 0; i < Settings::NPCRules.size(); ) {
            auto& rule = Settings::NPCRules[i];
            ImGui::PushID(static_cast<int>(i));

            if (ImGui::CollapsingHeader(rule.ruleName.c_str())) {
                ImGui::Indent();

                char nameBuf[128];
                strcpy_s(nameBuf, rule.ruleName.c_str());
                if (ImGui::InputText(GetLoc("menu.rule_name", "Rule Name"), nameBuf, sizeof(nameBuf))) {
                    std::string newName(nameBuf);
                    if (newName != rule.ruleName && !newName.empty()) {
                        std::string oldPath = RULES_DIR + rule.ruleName + ".json";
                        if (std::filesystem::exists(oldPath)) std::filesystem::remove(oldPath);
                        rule.ruleName = newName;
                        changed = true;
                    }
                }

                RE::FormID prevPerk = rule.perkID;
                std::string perkLabel = std::string(GetLoc("menu.target_perk", "Target Perk")) + "##" + std::to_string(i);
                if (DrawDropdown(perkLabel.c_str(), "Perk", rule.perkID, 300.0f)) {
                    bool conflict = false;
                    if (rule.perkID != 0) {
                        for (size_t j = 0; j < Settings::NPCRules.size(); j++) {
                            if (i != j && Settings::NPCRules[j].perkID == rule.perkID) {
                                conflict = true;
                                break;
                            }
                        }
                    }
                    if (conflict) rule.perkID = prevPerk;
                    else changed = true;
                }

                ImGui::Separator();
                DrawChanceUI(GetLoc("menu.rule_chance_config", "Rule Dodge Chance Configuration"), rule.chance, changed);
                ImGui::Separator();

                ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "%s", GetLoc("menu.proj_detection_header", "Projectile Detection"));
                if (ImGui::Combo(GetLoc("menu.npc_proj_dodge", "Dodge Projectiles"), &rule.dodgeProjectiles, projTypes, 4)) changed = true;
                if (rule.dodgeProjectiles != 3) {
                    if (ImGui::Checkbox(GetLoc("menu.proj_require_los", "Require Line of Sight (Vision) - Projectiles"), &rule.projRequireLoS)) changed = true;
                }

                ImGui::Spacing();
                ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "%s", GetLoc("menu.melee_detection_header", "Melee Detection"));
                if (ImGui::Checkbox(GetLoc("menu.enable_melee_dodge", "Enable Melee Dodge"), &rule.meleeDodgeEnabled)) changed = true;
                if (rule.meleeDodgeEnabled) {
                    if (ImGui::Checkbox(GetLoc("menu.melee_use_area", "Use Area (AoE) - Melee"), &rule.meleeUseArea)) changed = true;
                    if (rule.meleeUseArea) {
                        if (ImGui::Checkbox(GetLoc("menu.melee_require_los", "Require Line of Sight (Vision) - Melee Area"), &rule.meleeRequireLoS)) changed = true;
                    }
                }
                if (ImGui::Checkbox(GetLoc("menu.npc_dash_gap_close", "Enable Dash For Gap Close"), &rule.dashGapCloseEnabled)) changed = true;
                ImGui::Separator();
                ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "%s", GetLoc("menu.random_dodge_header", "Random Dodge Events"));
                DrawRandomDodgeUI("rule_random_dodge", rule.randomDodgeEnabled, rule.randomDodgeChance, rule.randomDodgeUseArea, rule.randomDodgeRequireLoS, rule.randomDodgeEvents, changed);

                if (ImGui::Combo(GetLoc("menu.npc_dodge_type", "Dodge Type"), &rule.dodgeType, dodgeTypes, 3)) changed = true;
                if (ImGui::Checkbox(GetLoc("menu.roll_only_light_armor", "Roll Dodge Only For Light Armor"), &rule.rollOnlyLightArmor)) changed = true;

                std::string smartDodgeLabel = std::string(GetLoc("menu.npc_smart_dodge", "Smart Dodge")) + "##Rule";
                if (ImGui::Checkbox(smartDodgeLabel.c_str(), &rule.smartDodge)) changed = true;
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", GetLoc("menu.npc_smart_dodge_hover", "Enables advanced pathfinding scanning using NavMesh to dodge obstacles. If disabled, default directional layout is used."));
                    ImGui::EndTooltip();
                }
                if (ImGui::Checkbox(GetLoc("menu.npc_allow_stagger", "Allow Dodge While Staggered"), &rule.allowStaggerDodge)) changed = true;

                ImGui::Separator();
                if (ImGui::CollapsingHeader(GetLoc("menu.roll_header", "Roll Dodge"))) {
                    ImGui::Indent();
                    std::string rollSectionId = "rule_roll_dist_" + std::to_string(i);
                    DrawDistanceSettingsUI(rollSectionId.c_str(), rule.rollDistance, changed, false, dummyUpward, dummyPerk, GetLoc("menu.rule_roll_distance", "Rule Roll Dodge Distance"));
                    DrawDodgeTypeRuntimeUI("rule_roll_runtime", rule.rollSettings, changed, costTypes, invulnMethods);
                    ImGui::Unindent();
                }
                if (ImGui::CollapsingHeader(GetLoc("menu.step_header", "Step Dodge"))) {
                    ImGui::Indent();
                    std::string stepSectionId = "rule_step_dist_" + std::to_string(i);
                    DrawDistanceSettingsUI(stepSectionId.c_str(), rule.stepDistance, changed, false, dummyUpward, dummyPerk, GetLoc("menu.rule_step_distance", "Rule Step Dodge Distance"));
                    DrawDodgeTypeRuntimeUI("rule_step_runtime", rule.stepSettings, changed, costTypes, invulnMethods);
                    ImGui::Unindent();
                }
                if (ImGui::CollapsingHeader(GetLoc("menu.dash_header", "Dash"))) {
                    ImGui::Indent();
                    DrawChanceUI(GetLoc("menu.rule_dash_chance_config", "Rule Dash Chance Configuration"), rule.dashChance, changed);
                    std::string dashSectionId = "rule_dash_dist_" + std::to_string(i);
                    DrawDistanceSettingsUI(dashSectionId.c_str(), rule.dashDistance, changed, false, dummyUpward, dummyPerk, GetLoc("menu.rule_dash_distance", "Rule Dash Distance"));
                    DrawDodgeTypeRuntimeUI("rule_dash_runtime", rule.dashSettings, changed, costTypes, invulnMethods);
                    ImGui::Unindent();
                }

                ImGui::Spacing();
                if (ImGui::Button(GetLoc("menu.remove_rule", "Remove Rule"), { 150, 0 })) {
                    std::string path = RULES_DIR + rule.ruleName + ".json";
                    if (std::filesystem::exists(path)) std::filesystem::remove(path);
                    Settings::NPCRules.erase(Settings::NPCRules.begin() + i);
                    changed = true;
                    ImGui::PopID();
                    continue;
                }

                ImGui::Unindent();
            }
            ImGui::PopID();
            i++;
        }

        if (changed) SaveSettings();
    }

    void UnregisterInputCategory(const std::string& actionId) {
        if (!InputManagerAPI::_API) return;

        if (actionId == "DodgeRoll") {
            for (int id : Settings::DodgeRollActionIDs) InputManagerAPI::_API->UpdateListener(0, id, "Dodge for all", "Dodge Roll", false, nullptr, 0, nullptr, 0);
            for (int id : Settings::DodgeRollMotionIDs) InputManagerAPI::_API->UpdateListener(1, id, "Dodge for all", "Dodge Roll", false, nullptr, 0, nullptr, 0);
        }
        else if (actionId == "DodgeStep") {
            for (int id : Settings::DodgeStepActionIDs) InputManagerAPI::_API->UpdateListener(0, id, "Dodge for all", "Dodge Step", false, nullptr, 0, nullptr, 0);
            for (int id : Settings::DodgeStepMotionIDs) InputManagerAPI::_API->UpdateListener(1, id, "Dodge for all", "Dodge Step", false, nullptr, 0, nullptr, 0);
        }
        else if (actionId == "Dash") {
            for (int id : Settings::DashActionIDs) InputManagerAPI::_API->UpdateListener(0, id, "Dodge for all", "Dash", false, nullptr, 0, nullptr, 0);
            for (int id : Settings::DashMotionIDs) InputManagerAPI::_API->UpdateListener(1, id, "Dodge for all", "Dash", false, nullptr, 0, nullptr, 0);
        }
    }

    void EnsureDefaultInputs() {
        auto* inputManager = InputManagerAPI::_API;
        if (!inputManager || Settings::DefaultInputsInitialized) return;

        constexpr int kTap = 1;
        constexpr int kPress = 4;
        constexpr auto kKeyboardX = static_cast<std::uint32_t>(RE::BSKeyboardDevice::Keys::kX);
        constexpr auto kLeftShift = static_cast<std::uint32_t>(RE::BSKeyboardDevice::Keys::kLeftShift);
        constexpr auto kGamepadA = static_cast<std::uint32_t>(RE::BSWin32GamepadDevice::Keys::kA) + GAMEPAD_OFFSET;

        auto hasValidAction = [&](const std::vector<int>& actionIDs) {
            const auto actionCount = inputManager->GetInputCount(0);
            return std::any_of(actionIDs.begin(), actionIDs.end(), [&](int id) {
                return id >= 0 && static_cast<std::size_t>(id) < actionCount && inputManager->GetActionInfo(id).isValid;
                });
            };

        auto equalsIgnoreCase = [](std::string_view left, std::string_view right) {
            return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                });
            };

        auto findAction = [&](std::initializer_list<std::string_view> names) {
            const auto actionCount = inputManager->GetInputCount(0);
            for (std::size_t i = 0; i < actionCount; ++i) {
                const char* inputName = inputManager->GetInputName(0, static_cast<int>(i));
                if (!inputName) continue;
                if (std::any_of(names.begin(), names.end(), [&](std::string_view name) {
                    return equalsIgnoreCase(inputName, name);
                    })) {
                    return static_cast<int>(i);
                }
            }
            return -1;
            };

        auto ensureAction = [&](std::vector<int>& actionIDs, const char* actionName,
            std::initializer_list<std::string_view> aliases, int pcTapCount, int gamepadTapCount, bool useLeftShift) {
            if (hasValidAction(actionIDs)) return;

            actionIDs.clear();
            int actionID = findAction(aliases);
            bool created = false;
            if (actionID < 0) {
                actionID = inputManager->CreateInput(0, actionName);
                created = actionID >= 0;
            }

            if (actionID < 0) {
                SKSE::log::error("[Input Manager] Falha ao criar a acao padrao '{}'.", actionName);
                return;
            }

            if (created) {
                auto mapping = inputManager->GetActionInfo(actionID);
                if (!mapping.isValid) {
                    inputManager->DeleteInput(0, actionID);
                    SKSE::log::error("[Input Manager] A acao '{}' foi criada, mas nao retornou um mapeamento valido.", actionName);
                    return;
                }

                mapping.pcMainKey = kKeyboardX;
                mapping.pcMainAction = kTap;
                mapping.pcMainTapCount = pcTapCount;
                mapping.pcModifierKey = useLeftShift ? kLeftShift : 0;
                mapping.pcModAction = useLeftShift ? kPress : 0;
                mapping.pcModTapCount = 1;

                mapping.gamepadMainKey = kGamepadA;
                mapping.gamepadMainAction = kTap;
                mapping.gamepadMainTapCount = gamepadTapCount;
                mapping.gamepadModifierKey = 0;
                mapping.gamepadModAction = 0;
                mapping.gamepadModTapCount = 1;

                if (!inputManager->UpdateActionMapping(actionID, mapping)) {
                    inputManager->DeleteInput(0, actionID);
                    SKSE::log::error("[Input Manager] Falha ao configurar o mapeamento padrao '{}'.", actionName);
                    return;
                }

                SKSE::log::info("[Input Manager] Acao padrao '{}' criada e configurada.", actionName);
            }
            else {
                SKSE::log::info("[Input Manager] Acao existente '{}' vinculada ao Dodge for all.", actionName);
            }

            actionIDs.push_back(actionID);
            };

        ensureAction(Settings::DodgeStepActionIDs, "Dodge Step", { "Dodge Step", "Step Dodge" }, 1, 1, false);
        ensureAction(Settings::DodgeRollActionIDs, "Dodge Roll", { "Dodge Roll", "Roll Dodge" }, 2, 2, false);
        ensureAction(Settings::DashActionIDs, "Dash", { "Dash" }, 1, 3, true);

        Settings::DefaultInputsInitialized = true;
        SaveSettings();
    }

    void RegisterAllInputs() {
        if (!InputManagerAPI::_API) return;

        for (int id : Settings::DodgeRollActionIDs) InputManagerAPI::_API->UpdateListener(0, id, "Dodge for all", "Dodge Roll", true, nullptr, 0, nullptr, 0);
        for (int id : Settings::DodgeRollMotionIDs) InputManagerAPI::_API->UpdateListener(1, id, "Dodge for all", "Dodge Roll", true, nullptr, 0, nullptr, 0);

        for (int id : Settings::DodgeStepActionIDs) InputManagerAPI::_API->UpdateListener(0, id, "Dodge for all", "Dodge Step", true, nullptr, 0, nullptr, 0);
        for (int id : Settings::DodgeStepMotionIDs) InputManagerAPI::_API->UpdateListener(1, id, "Dodge for all", "Dodge Step", true, nullptr, 0, nullptr, 0);

        for (int id : Settings::DashActionIDs) InputManagerAPI::_API->UpdateListener(0, id, "Dodge for all", "Dash", true, nullptr, 0, nullptr, 0);
        for (int id : Settings::DashMotionIDs) InputManagerAPI::_API->UpdateListener(1, id, "Dodge for all", "Dash", true, nullptr, 0, nullptr, 0);
    }

    void TweenPauseRegister() {
        auto dispatcher = SKSE::GetModCallbackEventSource();
        if (!dispatcher) return;

        auto EnviarPayload = [&](const char* actionIdStr, const char* actionLabelStr, const std::vector<int>& actions, const std::vector<int>& motions) {
            rapidjson::Document doc;
            doc.SetObject();
            auto& alloc = doc.GetAllocator();

            doc.AddMember("tabId", rapidjson::StringRef("gameplay"), alloc);
            doc.AddMember("tabLabel", rapidjson::StringRef("Mods"), alloc);
            doc.AddMember("categoryId", rapidjson::StringRef("combat"), alloc);
            doc.AddMember("categoryLabel", rapidjson::StringRef("Dodge for all"), alloc);

            rapidjson::Value actionIdVal; actionIdVal.SetString(actionIdStr, alloc);
            doc.AddMember("actionId", actionIdVal, alloc);

            rapidjson::Value actionLabelVal; actionLabelVal.SetString(actionLabelStr, alloc);
            doc.AddMember("actionLabel", actionLabelVal, alloc);

            doc.AddMember("acceptsMotion", true, alloc);

            rapidjson::Value mappedIds(rapidjson::kArrayType);

            for (int id : actions) {
                rapidjson::Value bind(rapidjson::kObjectType);
                bind.AddMember("id", id, alloc);
                bind.AddMember("type", rapidjson::StringRef("action"), alloc);
                mappedIds.PushBack(bind, alloc);
            }
            for (int id : motions) {
                rapidjson::Value bind(rapidjson::kObjectType);
                bind.AddMember("id", id, alloc);
                bind.AddMember("type", rapidjson::StringRef("motion"), alloc);
                mappedIds.PushBack(bind, alloc);
            }

            doc.AddMember("mappedIds", mappedIds, alloc);

            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            doc.Accept(writer);

            SKSE::ModCallbackEvent modEvent{ "TweenPause_RegisterControl", RE::BSFixedString(buffer.GetString()), 0.0f, nullptr };
            dispatcher->SendEvent(&modEvent);
            };

        EnviarPayload("DodgeRoll", GetLoc("menu.dodge_roll", "Dodge Roll"), Settings::DodgeRollActionIDs, Settings::DodgeRollMotionIDs);
        EnviarPayload("DodgeStep", GetLoc("menu.dodge_step", "Dodge Step"), Settings::DodgeStepActionIDs, Settings::DodgeStepMotionIDs);
        EnviarPayload("Dash", GetLoc("menu.dash", "Dash"), Settings::DashActionIDs, Settings::DashMotionIDs);
    }

    struct ConvertedDodgeFile {
        std::filesystem::path originalPath;
        std::string relativeZipPath;
        std::string modifiedContent;
        bool isJson = false;
    };

    // Auxiliar para mapear e extrair tokens de direção dos arquivos .hkx
    bool ParseDodgeFilename(const std::string& filename, std::string& dirName, int& dirVal, std::string& newName) {
        std::string upper = filename;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

        // Regra de segurança: Ignora o arquivo se não contiver "MCO_DODGE" no nome
        if (upper.find("MCO_DODGE") == std::string::npos) return false;

        int type = 0;
        size_t lastDelim = upper.find_last_of("-_");
        if (lastDelim != std::string::npos) {
            std::string typeStr = upper.substr(lastDelim + 1);
            if (typeStr == "1") type = 1;
            else if (typeStr == "2") type = 2;
        }
        if (type == 0) return false;

        std::string remaining = upper.substr(0, lastDelim);
        size_t prevDelim = remaining.find_last_of("-_");
        std::string dirCode = (prevDelim != std::string::npos) ? remaining.substr(prevDelim + 1) : remaining;

        if (dirCode == "N") { dirName = "Neutral";        dirVal = 0; }
        else if (dirCode == "F") { dirName = "Forward";        dirVal = 1; }
        else if (dirCode == "RF") { dirName = "Forward-Right";  dirVal = 2; }
        else if (dirCode == "R") { dirName = "Right";          dirVal = 3; }
        else if (dirCode == "RB") { dirName = "Backward-Right"; dirVal = 4; }
        else if (dirCode == "B") { dirName = "Backward";       dirVal = 5; }
        else if (dirCode == "LB") { dirName = "Backward-Left";  dirVal = 6; }
        else if (dirCode == "L") { dirName = "Left";           dirVal = 7; }
        else if (dirCode == "LF") { dirName = "Forward-Left";   dirVal = 8; }
        else return false;

        newName = (type == 1) ? "BF_stepdodge.hkx" : "BF_rolldodge.hkx";
        return true;
    }

    void ProcessAndBuildOARStructure(std::vector<ConvertedDodgeFile>& operationsList, bool exportToZip, int& filesMoved, int& foldersCreated) {
        namespace fs = std::filesystem;
        fs::path startPath = "Data/meshes";
        if (!fs::exists(startPath)) return;

        // Sets de controlo para garantir que criamos apenas um config de cada tipo por pasta
        std::set<std::string> writtenDescriptionConfigs;
        std::set<std::string> writtenConditionConfigs;

        for (const auto& entry : fs::recursive_directory_iterator(startPath)) {
            if (!entry.is_regular_file()) continue;

            // Validação case-insensitive da extensão (.hkx, .HKX)
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
            if (ext != ".HKX") continue;

            std::string filename = entry.path().stem().string();
            std::string dirName = "";
            int dirVal = 0;
            std::string newHkxName = "";

            if (ParseDodgeFilename(filename, dirName, dirVal, newHkxName)) {
                fs::path parentDir = entry.path().parent_path();
                std::string pStr = parentDir.string();
                std::string origFolderName = parentDir.filename().string(); // Ex: "6001" ou "Dynamic Dodge"

                // -------------------------------------------------------------
                // POINT 1: CORREÇÃO E ENGENHARIA DE PATHS DIRECIONAIS EXATOS
                // -------------------------------------------------------------
                fs::path baseRootPath;
                size_t darPos = pStr.find("DynamicAnimationReplacer");
                size_t oarPos = pStr.find("OpenAnimationReplacer");

                if (darPos != std::string::npos) {
                    baseRootPath = fs::path(pStr.substr(0, darPos));
                }
                else if (oarPos != std::string::npos) {
                    baseRootPath = fs::path(pStr.substr(0, oarPos));
                }
                else {
                    // Fallback para pastas estruturadas fora dos padrões de mods
                    baseRootPath = parentDir.parent_path();
                    if (baseRootPath.filename().string() == "_CustomConditions") {
                        baseRootPath = baseRootPath.parent_path().parent_path();
                    }
                }

                // Nova Estrutura Exata: OpenAnimationReplacer/Nome Original - Dodge for all/
                fs::path targetMainFolder = baseRootPath / "OpenAnimationReplacer" / (origFolderName + " - Dodge for all");
                fs::path targetDirectionFolder = targetMainFolder / dirName;

                // -------------------------------------------------------------
                // LEITURA DO JSON ORIGINAL (SE EXISTIR) PARA COMPATIBILIDADE
                // -------------------------------------------------------------
                bool hasOriginalConfig = false;
                rapidjson::Document origDoc;
                fs::path origConfigPath = parentDir / "config.json";

                if (fs::exists(origConfigPath)) {
                    std::ifstream ifs(origConfigPath);
                    if (ifs.is_open()) {
                        std::stringstream buffer;
                        buffer << ifs.rdbuf();
                        origDoc.Parse(buffer.str().c_str());
                        ifs.close();
                        if (!origDoc.HasParseError() && origDoc.IsObject()) {
                            hasOriginalConfig = true;
                        }
                    }
                }

                // -------------------------------------------------------------
                // POINT 2: MODELO 1 - CONFIG DE DESCRIÇÃO GLOBAL (Na Raiz do Dodge)
                // -------------------------------------------------------------
                std::string mainFolderKey = targetMainFolder.string();
                std::string descriptionJsonStr = "";

                if (!writtenDescriptionConfigs.contains(mainFolderKey)) {
                    rapidjson::Document descDoc;
                    descDoc.SetObject();
                    auto& descAlloc = descDoc.GetAllocator();

                    // Ajuste Estrito: Nome da pasta + " - Dodge for all", Descrição = "Dodge"
                    std::string modName = origFolderName + " - Dodge for all";
                    std::string author = "Converted"; // Default limpo do autor do DMCO
                    std::string description = "Dodge";

                    // Resgata o autor original se ele estiver explicitado no arquivo antigo
                    if (hasOriginalConfig) {
                        if (origDoc.HasMember("author") && origDoc["author"].IsString()) {
                            author = origDoc["author"].GetString();
                        }
                    }

                    descDoc.AddMember("name", rapidjson::Value(modName.c_str(), descAlloc).Move(), descAlloc);
                    descDoc.AddMember("author", rapidjson::Value(author.c_str(), descAlloc).Move(), descAlloc);
                    descDoc.AddMember("description", rapidjson::Value(description.c_str(), descAlloc).Move(), descAlloc);

                    rapidjson::StringBuffer descBuf;
                    rapidjson::PrettyWriter<rapidjson::StringBuffer> descWriter(descBuf);
                    descDoc.Accept(descWriter);
                    descriptionJsonStr = descBuf.GetString();

                    writtenDescriptionConfigs.insert(mainFolderKey);

                    if (!exportToZip) {
                        if (!fs::exists(targetMainFolder)) fs::create_directories(targetMainFolder);
                        std::ofstream ofs(targetMainFolder / "config.json");
                        if (ofs.is_open()) { ofs << descriptionJsonStr; ofs.close(); }
                    }
                }

                // -------------------------------------------------------------
                // MODELO 2: CONFIG DE CONDIÇÃO LOCAL (Em cada pasta Direcional)
                // -------------------------------------------------------------
                std::string dirFolderKey = targetDirectionFolder.string();
                std::string conditionJsonStr = "";

                if (!writtenConditionConfigs.contains(dirFolderKey)) {
                    rapidjson::Document condDoc;
                    condDoc.SetObject();
                    auto& condAlloc = condDoc.GetAllocator();

                    int priority = 7000;
                    try { priority = std::stoi(origFolderName); }
                    catch (...) { priority = 7000; }

                    condDoc.AddMember("name", rapidjson::Value(dirName.c_str(), condAlloc).Move(), condAlloc);
                    condDoc.AddMember("priority", priority, condAlloc);
                    condDoc.AddMember("keepRandomResultsOnLoop", true, condAlloc);

                    rapidjson::Value condsArray(rapidjson::kArrayType);

                    // Se existirem regras de condições originais (como condições de armas extraídas), herda-as
                    if (hasOriginalConfig && origDoc.HasMember("conditions") && origDoc["conditions"].IsArray()) {
                        for (auto& c : origDoc["conditions"].GetArray()) {
                            // Previne duplicados do validador direcional
                            if (c.IsObject() && c.HasMember("condition") && std::string(c["condition"].GetString()) == "CompareValues") {
                                if (c.HasMember("Value B") && c["Value B"].IsObject() && c["Value B"].HasMember("graphVariable")) {
                                    if (std::string(c["Value B"]["graphVariable"].GetString()) == "DirecionalCycleMoveset") {
                                        continue;
                                    }
                                }
                            }
                            rapidjson::Value cCopy;
                            cCopy.CopyFrom(c, condAlloc);
                            condsArray.PushBack(cCopy, condAlloc);
                        }
                    }

                    // Injeta a verificação CompareValues correta para a subpasta do OAR
                    rapidjson::Value newCond(rapidjson::kObjectType);
                    newCond.AddMember("condition", "CompareValues", condAlloc);
                    newCond.AddMember("negated", false, condAlloc);
                    newCond.AddMember("requiredVersion", "1.0.0.0", condAlloc);

                    rapidjson::Value valA(rapidjson::kObjectType);
                    valA.AddMember("value", dirVal, condAlloc);
                    newCond.AddMember("Value A", valA, condAlloc);

                    newCond.AddMember("Comparison", "==", condAlloc);

                    rapidjson::Value valB(rapidjson::kObjectType);
                    valB.AddMember("graphVariable", "DirecionalCycleMoveset", condAlloc);
                    valB.AddMember("graphVariableType", "Int", condAlloc);
                    newCond.AddMember("Value B", valB, condAlloc);

                    condsArray.PushBack(newCond, condAlloc);
                    condDoc.AddMember("conditions", condsArray, condAlloc);

                    rapidjson::StringBuffer condBuf;
                    rapidjson::PrettyWriter<rapidjson::StringBuffer> condWriter(condBuf);
                    condDoc.Accept(condWriter);
                    conditionJsonStr = condBuf.GetString();

                    writtenConditionConfigs.insert(dirFolderKey);

                    if (!exportToZip) {
                        if (!fs::exists(targetDirectionFolder)) {
                            fs::create_directories(targetDirectionFolder);
                            foldersCreated++;
                        }
                        std::ofstream ofs(targetDirectionFolder / "config.json");
                        if (ofs.is_open()) { ofs << conditionJsonStr; ofs.close(); }
                    }
                }

                // -------------------------------------------------------------
                // GRAVAÇÃO DE CONTEÚDO BINÁRIO E EXPORTAÇÃO
                // -------------------------------------------------------------
                if (exportToZip) {
                    if (!descriptionJsonStr.empty()) {
                        std::string zipDescPath = (targetMainFolder / "config.json").string();
                        std::replace(zipDescPath.begin(), zipDescPath.end(), '\\', '/');
                        operationsList.push_back({ origConfigPath, zipDescPath, descriptionJsonStr, true });
                    }

                    if (!conditionJsonStr.empty()) {
                        std::string zipCondPath = (targetDirectionFolder / "config.json").string();
                        std::replace(zipCondPath.begin(), zipCondPath.end(), '\\', '/');
                        operationsList.push_back({ origConfigPath, zipCondPath, conditionJsonStr, true });
                    }

                    std::string internalHkxPath = (targetDirectionFolder / newHkxName).string();
                    std::replace(internalHkxPath.begin(), internalHkxPath.end(), '\\', '/');

                    std::ifstream hkxFile(entry.path(), std::ios::binary);
                    std::string hkxContent((std::istreambuf_iterator<char>(hkxFile)), std::istreambuf_iterator<char>());
                    hkxFile.close();

                    operationsList.push_back({ entry.path(), internalHkxPath, hkxContent, false });
                    filesMoved++;
                }
                else {
                    if (!fs::exists(targetDirectionFolder)) {
                        fs::create_directories(targetDirectionFolder);
                        foldersCreated++;
                    }
                    fs::path destinationHkx = targetDirectionFolder / newHkxName;
                    fs::rename(entry.path(), destinationHkx);
                    filesMoved++;
                }
            }
        }
    }

    void ExportDMCOToZip(const std::vector<ConvertedDodgeFile>& operationsList) {
        if (operationsList.empty()) return;

        std::filesystem::path exportDir = "Data/export";
        std::filesystem::create_directories(exportDir);
        std::string zipPath = (exportDir / "DMCO_OAR_Converted.zip").string();

        mz_zip_archive zip_archive;
        memset(&zip_archive, 0, sizeof(zip_archive));

        if (!mz_zip_writer_init_file(&zip_archive, zipPath.c_str(), 0)) {
            SKSE::log::error("DMCO Converter: Falha ao inicializar o arquivo ZIP.");
            return;
        }

        for (const auto& out : operationsList) {
            if (!mz_zip_writer_add_mem(&zip_archive, out.relativeZipPath.c_str(), out.modifiedContent.data(), out.modifiedContent.size(), MZ_BEST_COMPRESSION)) {
                SKSE::log::error("DMCO Converter: Falha ao empacotar arquivo {}", out.relativeZipPath);
            }
        }

        mz_zip_writer_finalize_archive(&zip_archive);
        mz_zip_writer_end(&zip_archive);
        SKSE::log::info("DMCO Converter: ZIP gerado com sucesso em {}", zipPath);
    }

    void DMCOConverterRender() {
        static bool showConfirmPopup = false;
        static bool showSuccessMessage = false;
        static bool exportToZip = true;
        static int filesMovedCount = 0;
        static int foldersCreatedCount = 0;

        ImGui::Text("%s", GetLoc("menu.dmco_title", "DMCO to OAR Framework Converter"));
        ImGui::Separator(); ImGui::Spacing();

        ImGui::TextWrapped("%s", GetLoc("menu.dmco_desc", "Scans animation folders for legacy dodge files (MCO_Dodge) and dynamically restructures them into directional subfolders compatible with the 'DirectionalCycleMoveset' variable."));
        ImGui::Spacing();

        ImGui::Checkbox(GetLoc("menu.dmco_export_zip", "Export converted files to a ZIP file (Protects the originals)"), &exportToZip);
        ImGui::Spacing();

        if (ImGui::Button(GetLoc("menu.dmco_convert_btn", "Start DMCO Conversion"), { 250, 40 })) {
            showConfirmPopup = true;
            showSuccessMessage = false;
        }

        if (showSuccessMessage) {
            ImGui::Spacing();
            ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "%s: %d", GetLoc("menu.dmco_success_count", "Files successfully processed"), filesMovedCount);
            if (exportToZip) {
                ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "%s", GetLoc("menu.dmco_success_zip", "Structure generated at: Data/export/DMCO_OAR_Converted.zip"));
            }
            else {
                ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "%s: %d", GetLoc("menu.dmco_success_folders", "New OAR folders created on disk"), foldersCreatedCount);
            }
        }

        if (showConfirmPopup) {
            ImGui::OpenPopup(GetLoc("menu.dmco_confirm_popup", "Confirm OAR Restructuring"));
        }

        if (ImGui::BeginPopupModal(GetLoc("menu.dmco_confirm_popup", "Confirm OAR Restructuring"), nullptr, ImGui::ImGuiWindowFlags_AlwaysAutoResize)) {
            if (exportToZip) {
                ImGui::Text("%s", GetLoc("menu.dmco_pop_zip1", "Do you want to perform the search and generate the OAR tree inside the compressed package?"));
                ImGui::Text("%s", GetLoc("menu.dmco_pop_zip2", "Your original files in the meshes folder will NOT be altered."));
            }
            else {
                ImGui::Text("%s", GetLoc("menu.dmco_pop_disc1", "WARNING: You chose to modify the file tree directly on disk!"));
                ImGui::Text("%s", GetLoc("menu.dmco_pop_disc2", "The original files will be permanently moved to the subfolders."));
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button(GetLoc("common.yes", "Yes, Execute"), { 120, 0 })) {
                filesMovedCount = 0;
                foldersCreatedCount = 0;
                std::vector<ConvertedDodgeFile> operations;

                ProcessAndBuildOARStructure(operations, exportToZip, filesMovedCount, foldersCreatedCount);

                if (exportToZip && !operations.empty()) {
                    ExportDMCOToZip(operations);
                }

                showConfirmPopup = false;
                showSuccessMessage = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(GetLoc("common.cancel", "Cancel"), { 120, 0 })) {
                showConfirmPopup = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void Register() {
        if (SKSEMenuFramework::IsInstalled()) {
            LoadLanguage();
            LoadSettings();
            SKSEMenuFramework::SetSection(GetLoc("menu.section", "Dodge for all"));
            SKSEMenuFramework::AddSectionItem(GetLoc("menu.player_settings", "Player Settings"), PlayerRender);
            SKSEMenuFramework::AddSectionItem(GetLoc("menu.npc_settings", "NPC Settings"), NPCRender);
            SKSEMenuFramework::AddSectionItem(GetLoc("menu.rules_settings", "Rules Settings"), RulesRender);
            SKSEMenuFramework::AddSectionItem(GetLoc("menu.dmco_converter", "DMCO Converter"), DMCOConverterRender);
        }
    }
}
