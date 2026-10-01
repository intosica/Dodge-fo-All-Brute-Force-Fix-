#pragma once

#include "RE/B/BShkbAnimationGraph.h"
#include "RE/H/hkbClipGenerator.h"
#include "RE/H/hkbContext.h"
#include "RE/M/MotionDataContainer.h"
#include "RE/B/bhkCharacterState.h"
#include "RE/B/bhkCharacterController.h"

namespace RE
{
	class hkpCharacterMovementUtil
	{
	public:

		struct hkpMovementUtilInput
		{
			/// Forward direction in world space
			hkVector4 forward;

			/// Up direction in world space
			hkVector4 up;

			/// Normal of the surface we're standing on in world space
			hkVector4 surfaceNormal;

			/// Our current velocity in world space
			hkVector4 currentVelocity;

			/// Our desired velocity in the surface frame
			hkVector4 desiredVelocity;

			/// Velocity of the surface we're standing on in world space
			hkVector4 surfaceVelocity;

			/// Gain for the character controller.
			/// This variable controls the acceleration of the character. It should be
			/// scaled by the current timestep to ensure that the characters acceleration
			/// is not timestep dependent.
			float gain;

			/// Limit the maximum acceleration of the character
			float maxVelocityDelta;
		};

		/// Calculate a new output velocity based on the input
		static void CalculateMovement(const hkpMovementUtilInput& a_input, hkVector4& a_velocityOut)
		{
			using func_t = decltype(CalculateMovement);
			REL::Relocation<func_t> func{ RELOCATION_ID(78988, 81002) };

			func(a_input, a_velocityOut);
		}
	};

}

namespace hooks
{
	class hkbClipGeneratorHook
	{
	public:
		static void HookActivate(RE::hkbClipGenerator* a_this, const RE::hkbContext& a_context);
		static void HookDeactivate(RE::hkbClipGenerator* a_this, const RE::hkbContext& a_context);

		static inline REL::Relocation<void(*)(RE::hkbClipGenerator*, const RE::hkbContext&)> OriginalActivate;
		static inline REL::Relocation<void(*)(RE::hkbClipGenerator*, const RE::hkbContext&)> OriginalDeactivate;
	};

	class MotionDataContainerHook
	{
	public:
		static void ProcessTranslationData_Hook(RE::MotionDataContainer* a_this, float a_motionTime, RE::NiPoint3& a_translation,
			const RE::BSFixedString* a_clipName, RE::Character* a_character);

		static void ProcessRotationData_Hook(RE::MotionDataContainer* a_this, float a_motionTime, RE::NiQuaternion& a_rotation,
			const RE::BSFixedString* a_clipName, RE::Character* a_character);

		static inline REL::Relocation<void(*)(std::uintptr_t*, float, RE::NiPoint3&)> OriginalProcessTranslationData;
		static inline REL::Relocation<void(*)(std::uintptr_t*, float, RE::NiQuaternion&)> OriginalProcessRotationData;
	};

	class hkpCharacterContextHook
	{
	public:
		static RE::hkpCharacterStateType GetCharacterState_Hook(RE::hkpCharacterContext* a_this);
	};

	void SimulateStatePhysics_Hook(RE::bhkCharacterStateOnGround* a_this, RE::bhkCharacterController* a_characterController);

	void Install();
}