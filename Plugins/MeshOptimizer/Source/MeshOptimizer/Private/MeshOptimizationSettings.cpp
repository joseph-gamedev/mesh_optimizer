#include "MeshOptimizationSettings.h"

UMeshOptimizationSettings::UMeshOptimizationSettings()
{
	ApplyPreset(EMeshOptimizationPreset::Balanced);
}

void UMeshOptimizationSettings::ApplyPreset(EMeshOptimizationPreset InPreset)
{
	Preset = InPreset;

	switch (InPreset)
	{
	case EMeshOptimizationPreset::Preview:
		ReductionSettings.PercentTriangles = 0.1f;
		ReductionSettings.MaxDeviation = 5.0f;
		ReductionSettings.WeldingThreshold = 1.0f;
		ReductionSettings.SilhouetteImportance = EMeshOptImportance::Low;
		ReductionSettings.TextureImportance = EMeshOptImportance::Low;
		ReductionSettings.ShadingImportance = EMeshOptImportance::Low;
		break;

	case EMeshOptimizationPreset::Balanced:
		ReductionSettings.PercentTriangles = 0.4f;
		ReductionSettings.MaxDeviation = 1.0f;
		ReductionSettings.WeldingThreshold = 0.1f;
		ReductionSettings.SilhouetteImportance = EMeshOptImportance::Normal;
		ReductionSettings.TextureImportance = EMeshOptImportance::Normal;
		ReductionSettings.ShadingImportance = EMeshOptImportance::Normal;
		break;

	case EMeshOptimizationPreset::Quality:
		ReductionSettings.PercentTriangles = 0.7f;
		ReductionSettings.MaxDeviation = 0.5f;
		ReductionSettings.WeldingThreshold = 0.01f;
		ReductionSettings.SilhouetteImportance = EMeshOptImportance::High;
		ReductionSettings.TextureImportance = EMeshOptImportance::High;
		ReductionSettings.ShadingImportance = EMeshOptImportance::High;
		break;

	case EMeshOptimizationPreset::Lossless:
		ReductionSettings.PercentTriangles = 1.0f;
		ReductionSettings.MaxDeviation = 0.01f;
		ReductionSettings.bUseMaxDeviation = true;
		ReductionSettings.WeldingThreshold = 0.001f;
		ReductionSettings.SilhouetteImportance = EMeshOptImportance::High;
		ReductionSettings.TextureImportance = EMeshOptImportance::High;
		ReductionSettings.ShadingImportance = EMeshOptImportance::High;
		break;

	case EMeshOptimizationPreset::Custom:
		// Don't change custom settings
		break;
	}
}
