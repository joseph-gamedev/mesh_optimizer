#include "MeshOptimizerCommands.h"

#define LOCTEXT_NAMESPACE "FMeshOptimizerModule"

void FMeshOptimizerCommands::RegisterCommands()
{
	UI_COMMAND(OpenMeshOptimizer, "Mesh Optimizer", "Open the Mesh Optimizer tool window", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
