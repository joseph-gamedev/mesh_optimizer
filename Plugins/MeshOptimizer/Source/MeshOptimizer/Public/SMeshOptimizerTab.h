#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "MeshAnalyzer.h"
#include "MeshOptimizationSettings.h"
#include "MeshOptimizationProcessor.h"

class SMeshOptimizerTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMeshOptimizerTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SMeshOptimizerTab();

private:
	// UI builders
	TSharedRef<SWidget> BuildMeshInfoSection();
	TSharedRef<SWidget> BuildActionsSection();

	// Selection tracking
	void OnEditorSelectionChanged(UObject* NewSelection);
	void RefreshSelectedMesh();
	UStaticMesh* GetSelectedStaticMesh() const;
	UStaticMesh* ResolveTargetMeshForEdit();
	UStaticMesh* DuplicateMeshAsset(UStaticMesh* SourceMesh) const;

	// Direct actions on selected mesh
	FReply OnReduceClicked();
	FReply OnOptimizeNaniteFromOriginalClicked();
	FReply OnGenerateLODsClicked();
	FReply OnToggleNaniteClicked();
	FReply OnSetBoxCollisionClicked();
	FReply OnRemoveCollisionClicked();
	FReply OnGenLightmapUVsClicked();
	FReply OnRemoveUnusedMaterialsClicked();
	FReply OnConsolidateMaterialsClicked();
	FReply OnBIMOptimizeClicked();
	FReply OnSafeOptimizeClicked();
	FReply OnAggressiveOptimizeClicked();
	FReply OnRestoreOriginalClicked();

	// Batch actions on all level meshes
	FReply OnAnalyzeLevelClicked();
	FReply OnBatchReduceClicked();
	FReply OnBatchLODsClicked();
	FReply OnBatchNaniteClicked();
	FReply OnExportCSVClicked();

	// Core objects
	TObjectPtr<UMeshOptimizationProcessor> Processor;
	TObjectPtr<UMeshAnalyzer> Analyzer;

	// Currently selected mesh info
	TWeakObjectPtr<UStaticMesh> SelectedMesh;
	FMeshAnalysisResult SelectedMeshInfo;
	FString LastOperationSummary;

	// Controllable values + getters for SSpinBox binding
	float ReductionPercent = 0.5f;
	float SafeOptimizePercent = 0.25f;
	float AggressiveOptimizePercent = 0.5f;
	bool bCreateNewAssetCopy = false;
	bool bAggressivePreserveMeshBoundary = true;
	bool bAggressivePreserveGroupBoundary = true;
	bool bAggressivePreserveMaterialBoundary = true;
	bool bAggressivePreserveAttributeSeams = true;
	bool bAggressivePreventTinyTriangles = true;
	bool bAggressivePreserveBoundaryShape = true;
	bool bAggressiveRetainQuadricMemory = true;
	int32 LODCount = 3;
	float NaniteFallback = 0.1f;
	float BIMWeldThreshold = 0.01f;
	float BIMPlanarTolerance = 1.0f;
	float GetReductionPercent() const;
	float GetSafeOptimizePercent() const;
	float GetAggressiveOptimizePercent() const;
	int32 GetLODCount() const;

	// Delegate handle for selection change
	FDelegateHandle SelectionChangedHandle;
};
