namespace Tracking
{
    inline std::uint32_t PerfectDodge = 0;
    inline std::uint32_t NormalDodge = 0;

    inline void SyncPlayerGraphCounters(RE::Actor* actor = nullptr)
    {
        auto player = actor ? actor : RE::PlayerCharacter::GetSingleton();
        if (!player || !player->IsPlayerRef()) return;

        player->SetGraphVariableInt("PerfectDodgedHitsSTM", static_cast<int>(PerfectDodge));
        player->SetGraphVariableInt("DodgedHitsSTM", static_cast<int>(NormalDodge));
    }

    inline void RecordPerfectDodge(RE::Actor* actor)
    {
        if (!actor || !actor->IsPlayerRef()) return;
        PerfectDodge++;
        SyncPlayerGraphCounters(actor);
    }

    inline void RecordNormalDodge(RE::Actor* actor)
    {
        if (!actor || !actor->IsPlayerRef()) return;
        NormalDodge++;
        SyncPlayerGraphCounters(actor);
    }

    namespace Serialization
    {
        constexpr std::uint32_t kSerializationID = 'VIN1';
        constexpr std::uint32_t kDataVersion = 1;
        constexpr std::uint32_t kPerfectDodgeRecord = 'PDOD';
        constexpr std::uint32_t kNormalDodgeRecord = 'NDOD';

        inline bool hasLoadedData = false;

        static void SaveCallback(SKSE::SerializationInterface* a_intfc)
        {
            if (a_intfc->OpenRecord(kPerfectDodgeRecord, kDataVersion)) {
                a_intfc->WriteRecordData(PerfectDodge);
            }
            else {
                SKSE::log::error("Falha ao abrir record de Perfect Dodge!");
            }

            if (a_intfc->OpenRecord(kNormalDodgeRecord, kDataVersion)) {
                a_intfc->WriteRecordData(NormalDodge);
            }
            else {
                SKSE::log::error("Falha ao abrir record de Normal Dodge!");
            }

            SyncPlayerGraphCounters();
        }

        static void LoadCallback(SKSE::SerializationInterface* a_intfc)
        {
            std::uint32_t type;
            std::uint32_t version;
            std::uint32_t length;

            hasLoadedData = false;
            while (a_intfc->GetNextRecordInfo(type, version, length)) {
                switch (type) {
                case kPerfectDodgeRecord:
                    a_intfc->ReadRecordData(PerfectDodge);
                    hasLoadedData = true;
                    break;

                case kNormalDodgeRecord:
                    a_intfc->ReadRecordData(NormalDodge);
                    hasLoadedData = true;
                    break;

                default:
                    SKSE::log::warn("Sub-record desconhecido encontrado: {:X}", type);
                    break;
                }
            }

            SyncPlayerGraphCounters();
            SKSE::log::info("Dados carregados - Perfect: {} | Normal: {}", PerfectDodge, NormalDodge);
        }

        static void RevertCallback(SKSE::SerializationInterface*)
        {
            PerfectDodge = 0;
            NormalDodge = 0;
            SyncPlayerGraphCounters();
            hasLoadedData = false;
        }
    }
}
