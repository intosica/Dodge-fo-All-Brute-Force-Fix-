#pragma once
#include "Events.h"

namespace RE
{
    namespace Offset
    {
        typedef void(_fastcall* _destroyProjectile)(RE::Projectile* a_projectile);
        inline static REL::Relocation<_destroyProjectile> destroyProjectile{ RELOCATION_ID(42930, 44110) };

    }
}

class Hook_OnMeleeHit
{
public:
    static void install()
    {
        auto& trampoline = SKSE::GetTrampoline();

        constexpr size_t size_per_hook = 14;
        constexpr size_t NUM_TRAMPOLINE_HOOKS = 3;
        SKSE::AllocTrampoline(size_per_hook * NUM_TRAMPOLINE_HOOKS);
        REL::Relocation<uintptr_t> hook{ RELOCATION_ID(37673, 38627) };  //140628C20       14064E760
        _ProcessHit = trampoline.write_call<5>(hook.address() + REL::Relocate(0x3C0, 0x4A8), processHit);
        logger::info("hook:OnMeleeHit");
    }

private:
    static void processHit(RE::Actor* victim, RE::HitData& hitData);
    static inline REL::Relocation<decltype(processHit)> _ProcessHit;  //140626400       14064BAB0

};

class Hook_OnProjectileCollision
{
public:
    static void install()
    {
        REL::Relocation<std::uintptr_t> arrowProjectileVtbl{ RE::VTABLE_ArrowProjectile[0] };
        REL::Relocation<std::uintptr_t> missileProjectileVtbl{ RE::VTABLE_MissileProjectile[0] };

        _arrowCollission = arrowProjectileVtbl.write_vfunc(190, OnArrowCollision);
        _missileCollission = missileProjectileVtbl.write_vfunc(190, OnMissileCollision);
        logger::info("hook:OnProjectileCollision");
    };

private:
    static void OnArrowCollision(RE::Projectile* a_this, RE::hkpAllCdPointCollector* a_AllCdPointCollector);

    static void OnMissileCollision(RE::Projectile* a_this, RE::hkpAllCdPointCollector* a_AllCdPointCollector);
    static inline REL::Relocation<decltype(OnArrowCollision)> _arrowCollission;
    static inline REL::Relocation<decltype(OnMissileCollision)> _missileCollission;
};


//namespace MeuModAnimacao
//{
//    // ==========================================
//    // 1. HOOK: ACTIVATE (Índice 0x4)
//    // ==========================================
//    using Activate_t = void(RE::hkbClipGenerator* a_this, const RE::hkbContext& a_context);
//    inline Activate_t* _Original_Activate = nullptr;
//
//    static void Hook_Activate(RE::hkbClipGenerator* a_this, const RE::hkbContext& a_context)
//    {
//        if (a_this && a_this->animationName.c_str()) {
//            std::string name = a_this->animationName.c_str();
//
//            size_t pos = name.find("BF_rolldodge.hkx");
//
//        }
//
//        if (_Original_Activate) {
//            _Original_Activate(a_this, a_context);
//        }
//    }
//
//    // ==========================================
//    // 2. HOOK: UPDATE (Índice 0x5)
//    // ==========================================
//    using Update_t = void(RE::hkbClipGenerator* a_this, const RE::hkbContext& a_context, float a_timestep);
//    inline Update_t* _Original_Update = nullptr;
//
//    static void Hook_Update(RE::hkbClipGenerator* a_this, const RE::hkbContext& a_context, float a_timestep)
//    {
//        // Primeiro executa o update original para a engine calcular o tempo atual (localTime)
//        if (_Original_Update) {
//            _Original_Update(a_this, a_context, a_timestep);
//        }
//
//        if (a_this && a_this->animationName.c_str()) {
//            std::string name = a_this->animationName.c_str();
//
//            if (name.find("BF_rolldodge.hkx") != std::string::npos) {
//                // Exemplo: Monitorar o tempo aqui para evitar cortes secos
//                // Se a animação está quase acabando, você pode injetar alguma lógica ou conferir o a_this->localTime
//            }
//        }
//    }
//
//    // ==========================================
//    // 3. HOOK: DEACTIVATE (Índice 0x7)
//    // ==========================================
//    using Deactivate_t = void(RE::hkbClipGenerator* a_this, const RE::hkbContext& a_context);
//    inline Deactivate_t* _Original_Deactivate = nullptr;
//
//    static void Hook_Deactivate(RE::hkbClipGenerator* a_this, const RE::hkbContext& a_context)
//    {
//        //if (a_this && a_this->animationName.c_str()) {
//        //    std::string name = a_this->animationName.c_str();
//        //    if (name.find("BF_rolldodge.hkx") != std::string::npos) {
//        //        // Executa a suavização de saída (Ease Out) se o controle de animação for válido
//        //        if (a_this->animationControl) {
//        //            auto control = a_this->animationControl.get();
//
//        //            // Modifica o estado do mixer para suavizar a saída (Ease Out)
//        //            control->easeStatus = RE::hkaDefaultAnimationControl::EaseStatus::kEasingOut;
//
//        //            // Define o tempo de transição (ex: 0.2 segundos)
//        //            // Como a variável armazena o inverso da duração (easeInvDuration), fazemos 1.0f / segundos
//        //            control->easeInvDuration = 1.0f / 0.2f;
//
//        //            // Reseta o progresso do tempo da transição curve
//        //            control->easeT = 0.0f;
//
//        //            float novoWeight = 0.0f;
//
//
//        //            // Aplica o enfraquecimento. O Idle por baixo vai assumindo o controle sem trancos!
//        //            control->masterWeight = novoWeight;
//        //        }
//        //    }
//        //}
//
//        if (_Original_Deactivate) {
//            _Original_Deactivate(a_this, a_context);
//        }
//    }
//
//    // ==========================================
//    // 4. HOOK: QUEUE (AnimationFileManagerSingleton - Índice 0x1)
//    // ==========================================
//    using Queue_t = std::int32_t(RE::AnimationFileManagerSingleton* a_this,
//        const RE::hkbContext& a_hkbContext,
//        RE::hkbClipGenerator* a_clipGenerator,
//        RE::BSSynchronizedClipGenerator* a_synchronizedClipGenerator);
//
//    static inline Queue_t* _Original_Queue = nullptr;
//
//    static std::int32_t Hook_Queue(RE::AnimationFileManagerSingleton* a_this,
//        const RE::hkbContext& a_hkbContext,
//        RE::hkbClipGenerator* a_clipGenerator,
//        RE::BSSynchronizedClipGenerator* a_synchronizedClipGenerator)
//    {
//        if (a_clipGenerator && a_clipGenerator->animationName.c_str()) {
//            std::string animName = a_clipGenerator->animationName.c_str();
//            // Intercepta ANTES do jogo carregar o Roll Dodge do disco
//            //if (animName.find("BF_rolldodge.hkx") != std::string::npos) {
//
//            //    // Altera o caminho para o Step Dodge. A engine vai carregar este arquivo de verdade!
//            //    a_clipGenerator->animationName = "BF_stepdodge.hkx";
//
//            //    logger::info("Substituição de arquivo realizada no carregador: BF_stepdodge.hkx");
//            //}
//
//        }
//
//        return _Original_Queue(a_this, a_hkbContext, a_clipGenerator, a_synchronizedClipGenerator);
//    }
//
//    // ==========================================
//    // INSTALAÇÃO DOS HOOKS
//    // ==========================================
//    static void Install()
//    {
//        // Obtém a tabela virtual do hkbClipGenerator
//        REL::Relocation<std::uintptr_t> clipGenVtbl{ RE::VTABLE_hkbClipGenerator[0] };
//
//        // Instala o Activate (Índice 4)
//        _Original_Activate = reinterpret_cast<Activate_t*>(
//            clipGenVtbl.write_vfunc(0x4, reinterpret_cast<std::uintptr_t>(Hook_Activate))
//            );
//
//        // Instala o Update (Índice 5)
//        _Original_Update = reinterpret_cast<Update_t*>(
//            clipGenVtbl.write_vfunc(0x5, reinterpret_cast<std::uintptr_t>(Hook_Update))
//            );
//
//        // Instala o Deactivate (Índice 7)
//        _Original_Deactivate = reinterpret_cast<Deactivate_t*>(
//            clipGenVtbl.write_vfunc(0x7, reinterpret_cast<std::uintptr_t>(Hook_Deactivate))
//            );
//
//        // Obtém e instala o hook do gerenciador de arquivos de animação
//        auto manager = RE::AnimationFileManagerSingleton::GetSingleton();
//
//        if (manager) {
//            REL::Relocation<std::uintptr_t> managerVtbl{ RE::VTABLE_AnimationFileManagerSingleton[0] };
//
//            // Instala o Queue (Índice 1)
//            _Original_Queue = reinterpret_cast<Queue_t*>(
//                managerVtbl.write_vfunc(0x1, reinterpret_cast<std::uintptr_t>(Hook_Queue))
//                );
//        }
//    }
//}