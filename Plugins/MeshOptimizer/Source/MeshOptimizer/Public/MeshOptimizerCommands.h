#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

class FMeshOptimizerCommands : public TCommands<FMeshOptimizerCommands>
{
public:
	FMeshOptimizerCommands()
		: TCommands<FMeshOptimizerCommands>(
			TEXT("MeshOptimizer"),
			NSLOCTEXT("Contexts", "MeshOptimizer", "Mesh Optimizer Plugin"),
			NAME_None,
			FAppStyle::GetAppStyleSetName())
	{
	}

	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> OpenMeshOptimizer;
};
