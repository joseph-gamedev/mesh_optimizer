#include "SMeshOptimizerTab.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Text/STextBlock.h"
#include "Misc/FileHelper.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Selection.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "Styling/AppStyle.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "SMeshOptimizerTab"

void SMeshOptimizerTab::Construct(const FArguments& InArgs)
{
	Processor = NewObject<UMeshOptimizationProcessor>();
	Processor->AddToRoot();
	Analyzer = NewObject<UMeshAnalyzer>();
	Analyzer->AddToRoot();

	// Listen for editor selection changes
	if (GEditor)
	{
		SelectionChangedHandle = USelection::SelectionChangedEvent.AddSP(this, &SMeshOptimizerTab::OnEditorSelectionChanged);
	}

	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		.Padding(8.0f)
		[
			SNew(SVerticalBox)

			// ── Title ──
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Title", "MESH OPTIMIZER"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
			]

			// ── Selected Mesh Info ──
			+ SVerticalBox::Slot().AutoHeight()
			[
				BuildMeshInfoSection()
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
			[
				SNew(SSeparator)
			]

			// ── Actions ──
			+ SVerticalBox::Slot().AutoHeight()
			[
				BuildActionsSection()
			]
		]
	];

	// Initialize with current selection
	RefreshSelectedMesh();
}

SMeshOptimizerTab::~SMeshOptimizerTab()
{
	USelection::SelectionChangedEvent.Remove(SelectionChangedHandle);

	if (Processor) Processor->RemoveFromRoot();
	if (Analyzer) Analyzer->RemoveFromRoot();
}

// ─── Mesh Info Section ──────────────────────────────────────────────

TSharedRef<SWidget> SMeshOptimizerTab::BuildMeshInfoSection()
{
	auto MakeStat = [this](FText Label, TAttribute<FText> Value, FSlateColor Color = FSlateColor::UseForeground()) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 1)
			[
				SNew(SBox).WidthOverride(120)
				[
					SNew(STextBlock).Text(Label)
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 1)
			[
				SNew(STextBlock).Text(Value)
			];
	};

	return SNew(SVerticalBox)

		// Mesh name
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				return SelectedMesh.IsValid()
					? FText::FromString(SelectedMesh->GetName())
					: LOCTEXT("NoSel", "No static mesh selected - select a mesh actor in viewport");
			})
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
		[
			MakeStat(LOCTEXT("Tris", "Triangles"), TAttribute<FText>::CreateLambda([this]()
			{
				return FText::FromString(FString::FormatAsNumber(SelectedMeshInfo.TriangleCount));
			}))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeStat(LOCTEXT("Verts", "Vertices"), TAttribute<FText>::CreateLambda([this]()
			{
				return FText::FromString(FString::FormatAsNumber(SelectedMeshInfo.VertexCount));
			}))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeStat(LOCTEXT("Mats", "Materials"), TAttribute<FText>::CreateLambda([this]()
			{
				return FText::AsNumber(SelectedMeshInfo.MaterialSlotCount);
			}))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeStat(LOCTEXT("LODs", "LODs"), TAttribute<FText>::CreateLambda([this]()
			{
				return FText::AsNumber(SelectedMeshInfo.LODCount);
			}))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeStat(LOCTEXT("Nanite", "Nanite"), TAttribute<FText>::CreateLambda([this]()
			{
				return SelectedMeshInfo.bHasNanite ? LOCTEXT("Yes", "Enabled") : LOCTEXT("No", "Disabled");
			}))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeStat(LOCTEXT("LmUV", "Lightmap UV"), TAttribute<FText>::CreateLambda([this]()
			{
				return SelectedMeshInfo.bHasLightmapUVs ? LOCTEXT("HasLM", "Yes") : LOCTEXT("NoLM", "Missing");
			}))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeStat(LOCTEXT("Mem", "Memory"), TAttribute<FText>::CreateLambda([this]()
			{
				return FText::FromString(FString::Printf(TEXT("%.2f MB"), SelectedMeshInfo.ApproxMemoryMB));
			}))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeStat(LOCTEXT("Collision", "Collision"), TAttribute<FText>::CreateLambda([this]()
			{
				return SelectedMeshInfo.bHasCollision ? LOCTEXT("HasCol", "Yes") : LOCTEXT("NoCol", "None");
			}))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
		[
			SNew(STextBlock)
			.Visibility_Lambda([this]()
			{
				return LastOperationSummary.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
			})
			.Text_Lambda([this]()
			{
				return FText::FromString(LastOperationSummary);
			})
			.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.85f, 1.0f)))
			.AutoWrapText(true)
		]

		// Refresh button
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Refresh", "Refresh"))
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([this]() { RefreshSelectedMesh(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("RestoreOriginal", "Restore Original"))
				.ToolTipText(LOCTEXT("RestoreOriginalTip", "Restore this mesh from the Mesh Optimizer backup created before the first destructive edit."))
				.OnClicked(this, &SMeshOptimizerTab::OnRestoreOriginalClicked)
				.IsEnabled_Lambda([this]()
				{
					return SelectedMesh.IsValid() && Processor && Processor->HasOriginalBackup(SelectedMesh.Get());
				})
			]
		];
}

// ─── Actions Section ────────────────────────────────────────────────

TSharedRef<SWidget> SMeshOptimizerTab::BuildActionsSection()
{
	auto MakeCheckBoxRow = [](FText Label, TAttribute<ECheckBoxState> State, FOnCheckStateChanged OnChanged, FText ToolTip) -> TSharedRef<SWidget>
	{
		return SNew(SCheckBox)
			.IsChecked(State)
			.OnCheckStateChanged(OnChanged)
			.ToolTipText(ToolTip)
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			];
	};

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("OutputModeHeader", "Output Asset"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bCreateNewAssetCopy ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bCreateNewAssetCopy = (State == ECheckBoxState::Checked); })
				.ToolTipText(LOCTEXT("OutputModeTip", "Unchecked: modify the selected asset. Checked: duplicate the selected mesh as a new asset in the Content Browser and edit the copy instead."))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CreateCopyCheckbox", "Create new asset copy instead of replacing selected asset"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("OutputModeDesc", "New copies are created beside the source mesh with an _Optimized suffix and become the active selected mesh."))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.AutoWrapText(true)
			]
		]

		// ── BIM OPTIMIZE (custom algorithm) ──
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(STextBlock).Text(LOCTEXT("BIMHeader", "BIM Optimize (Coplanar Simplification)"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("BIMDesc", "Removes interior vertices from flat surfaces and welds\nnear-duplicate vertices. Lossless for planar regions.\nBIM/Revit meshes often have 100s of triangles on flat walls\nthat can be reduced to 2 with zero quality loss."))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("WeldThresh", "Weld threshold:"))
			]
			+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)
			[
				SNew(SSpinBox<float>)
				.MinValue(0.001f)
				.MaxValue(1.0f)
				.Delta(0.001f)
				.Value_Lambda([this]() { return BIMWeldThreshold; })
				.OnValueChanged_Lambda([this](float Val) { BIMWeldThreshold = Val; })
				.ToolTipText(LOCTEXT("WeldThreshTip", "Distance threshold for welding near-duplicate vertices (cm)"))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 8, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("PlanarTol", "Angle tolerance:"))
			]
			+ SHorizontalBox::Slot().FillWidth(0.4f).VAlign(VAlign_Center)
			[
				SNew(SSpinBox<float>)
				.MinValue(0.1f)
				.MaxValue(10.0f)
				.Delta(0.1f)
				.Value_Lambda([this]() { return BIMPlanarTolerance; })
				.OnValueChanged_Lambda([this](float Val) { BIMPlanarTolerance = Val; })
				.ToolTipText(LOCTEXT("PlanarTolTip", "Max angle (degrees) between face normals to consider coplanar"))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SButton)
			.Text(LOCTEXT("BIMOptimize", "BIM Optimize"))
			.ToolTipText(LOCTEXT("BIMOptimizeTip", "Custom algorithm: vertex welding + coplanar interior vertex removal"))
			.OnClicked(this, &SMeshOptimizerTab::OnBIMOptimizeClicked)
			.IsEnabled_Lambda([this]() { return SelectedMesh.IsValid(); })
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SafeOptimizeHeader", "Safe Optimize by Percentage"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SafeOptimizeDesc", "Custom planar clustering algorithm for dense CAD/BIM meshes.\nTargets a reduction percentage without using Unreal's default reducer.\nPreserves borders and hard edges by only clustering planar interior vertices."))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("SafeOptimizePercentLabel", "Target reduction:"))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(SSpinBox<float>)
				.MinValue(0.01f)
				.MaxValue(0.9f)
				.Delta(0.01f)
				.Value(this, &SMeshOptimizerTab::GetSafeOptimizePercent)
				.OnValueCommitted_Lambda([this](float Val, ETextCommit::Type) { SafeOptimizePercent = Val; })
				.OnValueChanged_Lambda([this](float Val) { SafeOptimizePercent = Val; })
				.ToolTipText(LOCTEXT("SafeOptimizePercentTip", "Desired triangle reduction ratio for the custom planar clustering pass"))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("SafeOptimizeButton", "Safe Optimize"))
				.ToolTipText(LOCTEXT("SafeOptimizeButtonTip", "Run the custom percentage-driven planar optimization pass"))
				.OnClicked(this, &SMeshOptimizerTab::OnSafeOptimizeClicked)
				.IsEnabled_Lambda([this]() { return SelectedMesh.IsValid(); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				const int32 After = FMath::RoundToInt(SelectedMeshInfo.TriangleCount * (1.0f - SafeOptimizePercent));
				return FText::FromString(FString::Printf(TEXT("  %s -> %s tris target (%.0f%% reduction)"),
					*FString::FormatAsNumber(SelectedMeshInfo.TriangleCount),
					*FString::FormatAsNumber(After),
					SafeOptimizePercent * 100.0f));
			})
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AggressiveOptimizeHeader", "Aggressive Exact Percentage"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AggressiveOptimizeDesc", "Uses Unreal's dynamic-mesh simplifier with optional protections. Uncheck protections to push closer to an exact target triangle count, at higher deformation risk."))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("AggressivePercentLabel", "Target reduction:"))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(SSpinBox<float>)
				.MinValue(0.01f)
				.MaxValue(0.95f)
				.Delta(0.01f)
				.Value(this, &SMeshOptimizerTab::GetAggressiveOptimizePercent)
				.OnValueCommitted_Lambda([this](float Val, ETextCommit::Type) { AggressiveOptimizePercent = Val; })
				.OnValueChanged_Lambda([this](float Val) { AggressiveOptimizePercent = Val; })
				.ToolTipText(LOCTEXT("AggressivePercentTip", "Requested exact final reduction percentage for the aggressive custom optimizer"))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("AggressiveOptimizeButton", "Aggressive Optimize"))
				.ToolTipText(LOCTEXT("AggressiveOptimizeButtonTip", "Run the aggressive custom optimizer that searches for the closest actual built triangle count to your target"))
				.OnClicked(this, &SMeshOptimizerTab::OnAggressiveOptimizeClicked)
				.IsEnabled_Lambda([this]() { return SelectedMesh.IsValid(); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				const int32 After = FMath::RoundToInt(SelectedMeshInfo.TriangleCount * (1.0f - AggressiveOptimizePercent));
				return FText::FromString(FString::Printf(TEXT("  exact target: %s -> %s tris (%.0f%% reduction)"),
					*FString::FormatAsNumber(SelectedMeshInfo.TriangleCount),
					*FString::FormatAsNumber(After),
					AggressiveOptimizePercent * 100.0f));
			})
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AggressiveOptionsHeader", "Aggressive Options"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
				[
					MakeCheckBoxRow(
						LOCTEXT("AggressivePreserveMeshBoundary", "Preserve open mesh boundaries"),
						TAttribute<ECheckBoxState>::CreateLambda([this]() { return bAggressivePreserveMeshBoundary ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }),
						FOnCheckStateChanged::CreateLambda([this](ECheckBoxState State) { bAggressivePreserveMeshBoundary = (State == ECheckBoxState::Checked); }),
						LOCTEXT("AggressivePreserveMeshBoundaryTip", "Keep open border edges fixed. Turn off to allow stronger reduction near openings."))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
				[
					MakeCheckBoxRow(
						LOCTEXT("AggressivePreserveGroupBoundary", "Preserve group boundaries"),
						TAttribute<ECheckBoxState>::CreateLambda([this]() { return bAggressivePreserveGroupBoundary ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }),
						FOnCheckStateChanged::CreateLambda([this](ECheckBoxState State) { bAggressivePreserveGroupBoundary = (State == ECheckBoxState::Checked); }),
						LOCTEXT("AggressivePreserveGroupBoundaryTip", "Keep polygon-group boundaries fixed."))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
				[
					MakeCheckBoxRow(
						LOCTEXT("AggressivePreserveMaterialBoundary", "Preserve material boundaries"),
						TAttribute<ECheckBoxState>::CreateLambda([this]() { return bAggressivePreserveMaterialBoundary ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }),
						FOnCheckStateChanged::CreateLambda([this](ECheckBoxState State) { bAggressivePreserveMaterialBoundary = (State == ECheckBoxState::Checked); }),
						LOCTEXT("AggressivePreserveMaterialBoundaryTip", "Keep section/material boundaries fixed. Turn off for more exact reduction."))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
				[
					MakeCheckBoxRow(
						LOCTEXT("AggressivePreserveAttributeSeams", "Preserve UV/normal seams"),
						TAttribute<ECheckBoxState>::CreateLambda([this]() { return bAggressivePreserveAttributeSeams ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }),
						FOnCheckStateChanged::CreateLambda([this](ECheckBoxState State) { bAggressivePreserveAttributeSeams = (State == ECheckBoxState::Checked); }),
						LOCTEXT("AggressivePreserveAttributeSeamsTip", "Keep UV and normal seams fixed. Turn off for stronger reduction, but expect texture and shading drift."))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
				[
					MakeCheckBoxRow(
						LOCTEXT("AggressivePreventTinyTriangles", "Prevent tiny triangles"),
						TAttribute<ECheckBoxState>::CreateLambda([this]() { return bAggressivePreventTinyTriangles ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }),
						FOnCheckStateChanged::CreateLambda([this](ECheckBoxState State) { bAggressivePreventTinyTriangles = (State == ECheckBoxState::Checked); }),
						LOCTEXT("AggressivePreventTinyTrianglesTip", "Reject collapses that would create extremely small sliver triangles."))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
				[
					MakeCheckBoxRow(
						LOCTEXT("AggressivePreserveBoundaryShape", "Preserve boundary shape"),
						TAttribute<ECheckBoxState>::CreateLambda([this]() { return bAggressivePreserveBoundaryShape ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }),
						FOnCheckStateChanged::CreateLambda([this](ECheckBoxState State) { bAggressivePreserveBoundaryShape = (State == ECheckBoxState::Checked); }),
						LOCTEXT("AggressivePreserveBoundaryShapeTip", "Bias the simplifier to keep boundary vertices on the boundary."))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
				[
					MakeCheckBoxRow(
						LOCTEXT("AggressiveRetainQuadricMemory", "Retain collapse memory"),
						TAttribute<ECheckBoxState>::CreateLambda([this]() { return bAggressiveRetainQuadricMemory ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; }),
						FOnCheckStateChanged::CreateLambda([this](ECheckBoxState State) { bAggressiveRetainQuadricMemory = (State == ECheckBoxState::Checked); }),
						LOCTEXT("AggressiveRetainQuadricMemoryTip", "Higher-quality error accumulation. Usually keep this on."))
				]
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
		[
			SNew(SSeparator)
		]

		// ── REDUCE TRIANGLES ──
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(STextBlock).Text(LOCTEXT("ReduceHeader", "Reduce Triangles"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(SSpinBox<float>)
				.MinValue(0.01f)
				.MaxValue(1.0f)
				.Value(this, &SMeshOptimizerTab::GetReductionPercent)
				.OnValueCommitted_Lambda([this](float Val, ETextCommit::Type) { ReductionPercent = Val; })
				.OnValueChanged_Lambda([this](float Val) { ReductionPercent = Val; })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Reduce", "Reduce"))
				.OnClicked(this, &SMeshOptimizerTab::OnReduceClicked)
				.IsEnabled_Lambda([this]() { return SelectedMesh.IsValid(); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				int32 After = FMath::RoundToInt(SelectedMeshInfo.TriangleCount * ReductionPercent);
				return FText::FromString(FString::Printf(TEXT("  %s -> %s tris (%.0f%%)"),
					*FString::FormatAsNumber(SelectedMeshInfo.TriangleCount),
					*FString::FormatAsNumber(After),
					ReductionPercent * 100.0f));
			})
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SButton)
			.Text(LOCTEXT("OptimizeNaniteFromOriginal", "Optimize + Nanite From Original"))
			.ToolTipText(LOCTEXT("OptimizeNaniteFromOriginalTip", "Starts from the original backup when available, applies baked reduction first, then enables Nanite with fallback triangles targeted from the original mesh."))
			.OnClicked(this, &SMeshOptimizerTab::OnOptimizeNaniteFromOriginalClicked)
			.IsEnabled_Lambda([this]() { return SelectedMesh.IsValid(); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 1)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				const float SafeKeep = FMath::Max(ReductionPercent, 0.01f);
				const float EffectiveFallbackOnReduced = FMath::Clamp(NaniteFallback / SafeKeep, 0.0f, 1.0f);
				return FText::FromString(FString::Printf(
					TEXT("  One-click uses %.0f%% source keep and %.0f%% fallback keep from original (%.0f%% of reduced source)."),
					ReductionPercent * 100.0f,
					NaniteFallback * 100.0f,
					EffectiveFallbackOnReduced * 100.0f));
			})
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.7f, 0.85f)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
		[
			SNew(SSeparator)
		]

		// ── GENERATE LODs ──
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(STextBlock).Text(LOCTEXT("LODHeader", "Generate LODs"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
			[
				SNew(STextBlock).Text(LOCTEXT("NumLODs", "LOD count:"))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(SSpinBox<int32>)
				.MinValue(1)
				.MaxValue(6)
				.Value(this, &SMeshOptimizerTab::GetLODCount)
				.OnValueCommitted_Lambda([this](int32 Val, ETextCommit::Type) { LODCount = Val; })
				.OnValueChanged_Lambda([this](int32 Val) { LODCount = Val; })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("GenLODs", "Generate"))
				.OnClicked(this, &SMeshOptimizerTab::OnGenerateLODsClicked)
				.IsEnabled_Lambda([this]() { return SelectedMesh.IsValid(); })
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
		[
			SNew(SSeparator)
		]

		// ── NANITE ──
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(STextBlock).Text(LOCTEXT("NaniteHeader", "Nanite"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text_Lambda([this]()
				{
					return SelectedMeshInfo.bHasNanite
						? LOCTEXT("DisableNanite", "Disable Nanite")
						: LOCTEXT("EnableNanite", "Enable Nanite");
				})
				.OnClicked(this, &SMeshOptimizerTab::OnToggleNaniteClicked)
				.IsEnabled_Lambda([this]() { return SelectedMesh.IsValid(); })
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
		[
			SNew(SSeparator)
		]

		// ── MATERIALS ──
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(STextBlock).Text(LOCTEXT("MaterialsHeader", "Materials"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("RemoveUnusedMats", "Remove Unused Slots"))
				.ToolTipText(LOCTEXT("RemoveUnusedMatsTip", "Remove material slots that have no geometry assigned"))
				.OnClicked(this, &SMeshOptimizerTab::OnRemoveUnusedMaterialsClicked)
				.IsEnabled_Lambda([this]() { return SelectedMesh.IsValid() && SelectedMeshInfo.MaterialSlotCount > 1; })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("ConsolidateMats", "Merge Duplicate Materials"))
				.ToolTipText(LOCTEXT("ConsolidateMatsTip", "Merge material slots that reference the same material"))
				.OnClicked(this, &SMeshOptimizerTab::OnConsolidateMaterialsClicked)
				.IsEnabled_Lambda([this]() { return SelectedMesh.IsValid() && SelectedMeshInfo.MaterialSlotCount > 1; })
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
		[
			SNew(SSeparator)
		]

		// ── COLLISION ──
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(STextBlock).Text(LOCTEXT("CollisionHeader", "Collision"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("BoxCol", "Add Box Collision"))
				.OnClicked(this, &SMeshOptimizerTab::OnSetBoxCollisionClicked)
				.IsEnabled_Lambda([this]() { return SelectedMesh.IsValid(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("RemoveCol", "Remove Collision"))
				.OnClicked(this, &SMeshOptimizerTab::OnRemoveCollisionClicked)
				.IsEnabled_Lambda([this]() { return SelectedMesh.IsValid(); })
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
		[
			SNew(SSeparator)
		]

		// ── LIGHTMAP UV ──
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(STextBlock).Text(LOCTEXT("LightmapHeader", "Lightmap UV"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SButton)
			.Text(LOCTEXT("GenLMUV", "Generate Lightmap UVs"))
			.OnClicked(this, &SMeshOptimizerTab::OnGenLightmapUVsClicked)
			.IsEnabled_Lambda([this]() { return SelectedMesh.IsValid(); })
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(0, 12)
		[
			SNew(SSeparator)
		]

		// ── BATCH (ENTIRE LEVEL) ──
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(STextBlock).Text(LOCTEXT("BatchHeader", "Batch Operations (Entire Level)"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("AnalyzeLevel", "Analyze Level"))
				.OnClicked(this, &SMeshOptimizerTab::OnAnalyzeLevelClicked)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("BatchReduce", "Reduce All"))
				.OnClicked(this, &SMeshOptimizerTab::OnBatchReduceClicked)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("BatchLODs", "LODs All"))
				.OnClicked(this, &SMeshOptimizerTab::OnBatchLODsClicked)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("BatchNanite", "Nanite All"))
				.OnClicked(this, &SMeshOptimizerTab::OnBatchNaniteClicked)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("ExportCSV", "Export CSV"))
				.OnClicked(this, &SMeshOptimizerTab::OnExportCSVClicked)
			]
		]

		// Batch summary
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				const FSceneAnalysisSummary& S = Analyzer->GetSummary();
				if (S.TotalMeshAssets == 0) return FText::GetEmpty();
				return FText::FromString(FString::Printf(
					TEXT("Level: %d meshes | %lld tris | No LODs: %d | No Nanite: %d | %.1f MB"),
					S.TotalMeshAssets, S.TotalTriangles, S.MeshesWithoutLODs, S.MeshesWithoutNanite, S.TotalApproxMemoryMB));
			})
			.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.8f, 1.0f)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
		];
}

// ─── Getter helpers for SSpinBox (need const method) ────────────────

float SMeshOptimizerTab::GetReductionPercent() const
{
	return ReductionPercent;
}

float SMeshOptimizerTab::GetSafeOptimizePercent() const
{
	return SafeOptimizePercent;
}

float SMeshOptimizerTab::GetAggressiveOptimizePercent() const
{
	return AggressiveOptimizePercent;
}

int32 SMeshOptimizerTab::GetLODCount() const
{
	return LODCount;
}

// ─── Selection Tracking ─────────────────────────────────────────────

UStaticMesh* SMeshOptimizerTab::DuplicateMeshAsset(UStaticMesh* SourceMesh) const
{
	if (!SourceMesh)
	{
		return nullptr;
	}

	const FString SourcePackageName = SourceMesh->GetOutermost()->GetName();
	const FString PackagePath = FPackageName::GetLongPackagePath(SourcePackageName);
	const FString SourceAssetName = FPackageName::GetLongPackageAssetName(SourcePackageName);

	FString CandidateAssetName = SourceAssetName + TEXT("_Optimized");
	FString CandidatePackageName = PackagePath / CandidateAssetName;
	int32 SuffixIndex = 1;
	while (FindPackage(nullptr, *CandidatePackageName) != nullptr || FindObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%s.%s"), *CandidatePackageName, *CandidateAssetName)) != nullptr)
	{
		CandidateAssetName = FString::Printf(TEXT("%s_Optimized%d"), *SourceAssetName, SuffixIndex++);
		CandidatePackageName = PackagePath / CandidateAssetName;
	}

	UPackage* Package = CreatePackage(*CandidatePackageName);
	if (!Package)
	{
		return nullptr;
	}

	UStaticMesh* NewMesh = DuplicateObject<UStaticMesh>(SourceMesh, Package, *CandidateAssetName);
	if (!NewMesh)
	{
		return nullptr;
	}

	NewMesh->SetFlags(RF_Public | RF_Standalone);
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewMesh);

	if (GEditor)
	{
		TArray<UObject*> ObjectsToSync;
		ObjectsToSync.Add(NewMesh);
		GEditor->SyncBrowserToObjects(ObjectsToSync);
	}

	return NewMesh;
}

UStaticMesh* SMeshOptimizerTab::ResolveTargetMeshForEdit()
{
	if (!SelectedMesh.IsValid())
	{
		return nullptr;
	}

	if (!bCreateNewAssetCopy)
	{
		return SelectedMesh.Get();
	}

	UStaticMesh* NewMesh = DuplicateMeshAsset(SelectedMesh.Get());
	if (NewMesh)
	{
		SelectedMesh = NewMesh;
		LastOperationSummary.Empty();
		RefreshSelectedMesh();
	}
	return NewMesh;
}

void SMeshOptimizerTab::OnEditorSelectionChanged(UObject* NewSelection)
{
	RefreshSelectedMesh();
}

UStaticMesh* SMeshOptimizerTab::GetSelectedStaticMesh() const
{
	if (!GEditor) return nullptr;

	// 1. Check viewport actor selection first
	USelection* ActorSelection = GEditor->GetSelectedActors();
	if (ActorSelection && ActorSelection->Num() > 0)
	{
		AActor* Actor = Cast<AActor>(ActorSelection->GetSelectedObject(0));
		if (Actor)
		{
			UStaticMeshComponent* Comp = Actor->FindComponentByClass<UStaticMeshComponent>();
			if (Comp && Comp->GetStaticMesh())
			{
				return Comp->GetStaticMesh();
			}
		}
	}

	// 2. Check Content Browser asset selection
	USelection* AssetSelection = GEditor->GetSelectedObjects();
	if (AssetSelection)
	{
		for (int32 i = 0; i < AssetSelection->Num(); ++i)
		{
			UStaticMesh* Mesh = Cast<UStaticMesh>(AssetSelection->GetSelectedObject(i));
			if (Mesh)
			{
				return Mesh;
			}
		}
	}

	return nullptr;
}

void SMeshOptimizerTab::RefreshSelectedMesh()
{
	TWeakObjectPtr<UStaticMesh> PreviousMesh = SelectedMesh;
	UStaticMesh* Mesh = GetSelectedStaticMesh();
	SelectedMesh = Mesh;

	if (PreviousMesh != SelectedMesh)
	{
		LastOperationSummary.Empty();
	}

	if (Mesh)
	{
		// Analyze inline
		SelectedMeshInfo = FMeshAnalysisResult();
		SelectedMeshInfo.StaticMesh = Mesh;
		SelectedMeshInfo.MeshName = Mesh->GetName();
		SelectedMeshInfo.AssetPath = Mesh->GetPathName();

		if (Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.Num() > 0)
		{
			const FStaticMeshLODResources& LOD0 = Mesh->GetRenderData()->LODResources[0];
			SelectedMeshInfo.TriangleCount = LOD0.GetNumTriangles();
			SelectedMeshInfo.VertexCount = LOD0.GetNumVertices();
			SelectedMeshInfo.UVChannelCount = LOD0.GetNumTexCoords();
		}
		SelectedMeshInfo.LODCount = Mesh->GetNumLODs();
		SelectedMeshInfo.MaterialSlotCount = Mesh->GetStaticMaterials().Num();
		SelectedMeshInfo.bHasNanite = Mesh->GetNaniteSettings().bEnabled;
		SelectedMeshInfo.LightmapResolution = Mesh->GetLightMapResolution();
		SelectedMeshInfo.bHasLightmapUVs = (SelectedMeshInfo.UVChannelCount >= 2);
		SelectedMeshInfo.bHasCollision = (Mesh->GetBodySetup() != nullptr &&
			Mesh->GetBodySetup()->AggGeom.GetElementCount() > 0);

		FBoxSphereBounds Bounds = Mesh->GetBounds();
		SelectedMeshInfo.BoundsSize = Bounds.BoxExtent * 2.0f;

		FResourceSizeEx ResourceSize;
		Mesh->GetResourceSizeEx(ResourceSize);
		SelectedMeshInfo.ApproxMemoryMB = ResourceSize.GetTotalMemoryBytes() / (1024.0f * 1024.0f);
	}
	else
	{
		SelectedMeshInfo = FMeshAnalysisResult();
	}
}

// ─── Per-mesh Actions ───────────────────────────────────────────────

FReply SMeshOptimizerTab::OnReduceClicked()
{
	if (!SelectedMesh.IsValid()) return FReply::Handled();

	UStaticMesh* TargetMesh = ResolveTargetMeshForEdit();
	if (!TargetMesh) return FReply::Handled();

	const int32 BeforeTris = SelectedMeshInfo.TriangleCount;
	const int32 BeforeVerts = SelectedMeshInfo.VertexCount;
	FMeshReductionConfig Config;
	Config.PercentTriangles = ReductionPercent;
	Processor->ReduceMesh(TargetMesh, Config);
	RefreshSelectedMesh();
	LastOperationSummary = FString::Printf(
		TEXT("Last operation: Unreal reduction | Triangles %s -> %s | Vertices %s -> %s"),
		*FString::FormatAsNumber(BeforeTris),
		*FString::FormatAsNumber(SelectedMeshInfo.TriangleCount),
		*FString::FormatAsNumber(BeforeVerts),
		*FString::FormatAsNumber(SelectedMeshInfo.VertexCount));
	return FReply::Handled();
}

FReply SMeshOptimizerTab::OnOptimizeNaniteFromOriginalClicked()
{
	if (!SelectedMesh.IsValid()) return FReply::Handled();

	UStaticMesh* BaseMesh = Processor->GetOptimizationBaseMesh(SelectedMesh.Get());
	if (!BaseMesh) return FReply::Handled();

	UStaticMesh* TargetMesh = nullptr;
	if (bCreateNewAssetCopy)
	{
		TargetMesh = DuplicateMeshAsset(BaseMesh);
	}
	else
	{
		TargetMesh = SelectedMesh.Get();
		if (Processor->HasOriginalBackup(TargetMesh))
		{
			Processor->RestoreOriginalMesh(TargetMesh);
		}
	}

	if (!TargetMesh) return FReply::Handled();

	SelectedMesh = TargetMesh;
	RefreshSelectedMesh();

	const int32 OriginalTris = SelectedMeshInfo.TriangleCount;
	const int32 OriginalVerts = SelectedMeshInfo.VertexCount;

	FMeshReductionConfig ReductionConfig;
	ReductionConfig.PercentTriangles = ReductionPercent;
	const bool bReduced = Processor->ReduceMesh(TargetMesh, ReductionConfig);
	if (!bReduced)
	{
		RefreshSelectedMesh();
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("OptimizeNaniteReduceFailed", "Source reduction failed before Nanite could be applied."));
		return FReply::Handled();
	}

	const float SourceKeepFromOriginal = FMath::Clamp(ReductionPercent, 0.01f, 1.0f);
	const float EffectiveFallbackOnReduced = FMath::Clamp(NaniteFallback / SourceKeepFromOriginal, 0.0f, 1.0f);

	FNaniteConfig NaniteConfig;
	NaniteConfig.bEnableNanite = true;
	NaniteConfig.KeepPercentTriangles = 1.0f;
	NaniteConfig.bUsePercentTrianglesForFallback = true;
	NaniteConfig.bGenerateFallback = true;
	NaniteConfig.FallbackPercentTriangles = EffectiveFallbackOnReduced;
	NaniteConfig.FallbackRelativeError = 0.0f;
	NaniteConfig.bSkipTranslucentMeshes = true;

	const bool bNaniteApplied = Processor->SetNanite(TargetMesh, NaniteConfig);
	RefreshSelectedMesh();

	if (!bNaniteApplied)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("OptimizeNaniteEnableFailed", "Source reduction completed, but Nanite could not be enabled on this mesh."));
	}

	LastOperationSummary = FString::Printf(
		TEXT("Last operation: Optimize + Nanite from original | Original tris %s | Source keep %.0f%% | Fallback keep %.0f%% of original | Current tris %s | Vertices %s -> %s"),
		*FString::FormatAsNumber(OriginalTris),
		SourceKeepFromOriginal * 100.0f,
		NaniteFallback * 100.0f,
		*FString::FormatAsNumber(SelectedMeshInfo.TriangleCount),
		*FString::FormatAsNumber(OriginalVerts),
		*FString::FormatAsNumber(SelectedMeshInfo.VertexCount));

	return FReply::Handled();
}

FReply SMeshOptimizerTab::OnGenerateLODsClicked()
{
	if (!SelectedMesh.IsValid()) return FReply::Handled();

	UStaticMesh* TargetMesh = ResolveTargetMeshForEdit();
	if (!TargetMesh) return FReply::Handled();

	FLODGenerationConfig Config;
	Config.NumLODs = LODCount;
	Config.bAutoComputeScreenSizes = true;
	Processor->GenerateLODs(TargetMesh, Config);
	RefreshSelectedMesh();
	return FReply::Handled();
}

FReply SMeshOptimizerTab::OnToggleNaniteClicked()
{
	if (!SelectedMesh.IsValid()) return FReply::Handled();

	UStaticMesh* TargetMesh = ResolveTargetMeshForEdit();
	if (!TargetMesh) return FReply::Handled();

	FNaniteConfig Config;
	Config.bEnableNanite = !SelectedMeshInfo.bHasNanite; // toggle
	Config.KeepPercentTriangles = 1.0f;
	Config.bUsePercentTrianglesForFallback = true;
	Config.bGenerateFallback = true;
	Config.FallbackPercentTriangles = NaniteFallback;
	Config.FallbackRelativeError = 0.0f;
	Config.bSkipTranslucentMeshes = true;
	Processor->SetNanite(TargetMesh, Config);
	RefreshSelectedMesh();
	return FReply::Handled();
}

FReply SMeshOptimizerTab::OnSetBoxCollisionClicked()
{
	if (!SelectedMesh.IsValid()) return FReply::Handled();

	UStaticMesh* TargetMesh = ResolveTargetMeshForEdit();
	if (!TargetMesh) return FReply::Handled();

	FCollisionConfig Config;
	Config.CollisionType = ECollisionOptimization::BoxCollision;
	Processor->SetCollision(TargetMesh, Config);
	RefreshSelectedMesh();
	return FReply::Handled();
}

FReply SMeshOptimizerTab::OnRemoveCollisionClicked()
{
	if (!SelectedMesh.IsValid()) return FReply::Handled();

	UStaticMesh* TargetMesh = ResolveTargetMeshForEdit();
	if (!TargetMesh) return FReply::Handled();

	FCollisionConfig Config;
	Config.CollisionType = ECollisionOptimization::NoCollision;
	Processor->SetCollision(TargetMesh, Config);
	RefreshSelectedMesh();
	return FReply::Handled();
}

FReply SMeshOptimizerTab::OnGenLightmapUVsClicked()
{
	if (!SelectedMesh.IsValid()) return FReply::Handled();

	UStaticMesh* TargetMesh = ResolveTargetMeshForEdit();
	if (!TargetMesh) return FReply::Handled();

	FLightmapUVConfig Config;
	Config.bGenerateLightmapUVs = true;
	Config.MinLightmapResolution = 64;
	Processor->GenerateLightmapUVs(TargetMesh, Config);
	RefreshSelectedMesh();
	return FReply::Handled();
}

FReply SMeshOptimizerTab::OnBIMOptimizeClicked()
{
	if (!SelectedMesh.IsValid()) return FReply::Handled();

	UStaticMesh* TargetMesh = ResolveTargetMeshForEdit();
	if (!TargetMesh) return FReply::Handled();

	int32 BeforeTris = SelectedMeshInfo.TriangleCount;
	int32 BeforeVerts = SelectedMeshInfo.VertexCount;

	int32 Removed = Processor->BIMOptimizeMesh(TargetMesh, BIMWeldThreshold, BIMPlanarTolerance);
	RefreshSelectedMesh();

	int32 AfterTris = SelectedMeshInfo.TriangleCount;
	int32 AfterVerts = SelectedMeshInfo.VertexCount;

	FMessageDialog::Open(EAppMsgType::Ok,
		FText::FromString(FString::Printf(
			TEXT("BIM Optimize complete.\n\nVertices collapsed: %d\nTriangles: %s -> %s (%.1f%% reduction)\nVertices: %s -> %s"),
			Removed,
			*FString::FormatAsNumber(BeforeTris), *FString::FormatAsNumber(AfterTris),
			BeforeTris > 0 ? (1.0f - (float)AfterTris / (float)BeforeTris) * 100.0f : 0.0f,
			*FString::FormatAsNumber(BeforeVerts), *FString::FormatAsNumber(AfterVerts)
		)));
	LastOperationSummary = FString::Printf(
		TEXT("Last operation: BIM optimize | Collapsed %d vertices | Triangles %s -> %s"),
		Removed,
		*FString::FormatAsNumber(BeforeTris),
		*FString::FormatAsNumber(AfterTris));

	return FReply::Handled();
}

FReply SMeshOptimizerTab::OnSafeOptimizeClicked()
{
	if (!SelectedMesh.IsValid()) return FReply::Handled();

	UStaticMesh* TargetMesh = ResolveTargetMeshForEdit();
	if (!TargetMesh) return FReply::Handled();

	const int32 BeforeTris = SelectedMeshInfo.TriangleCount;
	const int32 BeforeVerts = SelectedMeshInfo.VertexCount;

	const int32 CollapsedVertices = Processor->SafeOptimizeMeshByPercent(TargetMesh, SafeOptimizePercent, BIMPlanarTolerance);
	RefreshSelectedMesh();

	const int32 AfterTris = SelectedMeshInfo.TriangleCount;
	const int32 AfterVerts = SelectedMeshInfo.VertexCount;

	FMessageDialog::Open(EAppMsgType::Ok,
		FText::FromString(FString::Printf(
			TEXT("Safe Optimize complete.\n\nCollapsed vertices: %d\nTriangles: %s -> %s (%.1f%% reduction)\nVertices: %s -> %s"),
			CollapsedVertices,
			*FString::FormatAsNumber(BeforeTris), *FString::FormatAsNumber(AfterTris),
			BeforeTris > 0 ? (1.0f - (float)AfterTris / (float)BeforeTris) * 100.0f : 0.0f,
			*FString::FormatAsNumber(BeforeVerts), *FString::FormatAsNumber(AfterVerts)
		)));
	LastOperationSummary = FString::Printf(
		TEXT("Last operation: Safe optimize %.0f%% target | Triangles %s -> %s | Vertices %s -> %s"),
		SafeOptimizePercent * 100.0f,
		*FString::FormatAsNumber(BeforeTris),
		*FString::FormatAsNumber(AfterTris),
		*FString::FormatAsNumber(BeforeVerts),
		*FString::FormatAsNumber(AfterVerts));

	return FReply::Handled();
}

FReply SMeshOptimizerTab::OnAggressiveOptimizeClicked()
{
	if (!SelectedMesh.IsValid()) return FReply::Handled();

	UStaticMesh* TargetMesh = ResolveTargetMeshForEdit();
	if (!TargetMesh) return FReply::Handled();

	const int32 BeforeTris = SelectedMeshInfo.TriangleCount;
	const int32 BeforeVerts = SelectedMeshInfo.VertexCount;
	const int32 TargetTris = FMath::RoundToInt((float)BeforeTris * (1.0f - AggressiveOptimizePercent));
	FAggressiveOptimizeOptions Options;
	Options.bPreserveMeshBoundary = bAggressivePreserveMeshBoundary;
	Options.bPreserveGroupBoundary = bAggressivePreserveGroupBoundary;
	Options.bPreserveMaterialBoundary = bAggressivePreserveMaterialBoundary;
	Options.bPreserveAttributeSeams = bAggressivePreserveAttributeSeams;
	Options.bPreventTinyTriangles = bAggressivePreventTinyTriangles;
	Options.bPreserveBoundaryShape = bAggressivePreserveBoundaryShape;
	Options.bRetainQuadricMemory = bAggressiveRetainQuadricMemory;

	const int32 CollapsedVertices = Processor->AggressiveOptimizeMeshByPercent(
		TargetMesh,
		AggressiveOptimizePercent,
		FMath::Max(BIMPlanarTolerance, 3.0f),
		Options);
	RefreshSelectedMesh();

	const int32 AfterTris = SelectedMeshInfo.TriangleCount;
	const int32 AfterVerts = SelectedMeshInfo.VertexCount;

	FMessageDialog::Open(EAppMsgType::Ok,
		FText::FromString(FString::Printf(
			TEXT("Aggressive Optimize complete.\n\nCollapsed vertices: %d\nTarget triangles: %s\nActual triangles: %s -> %s\nActual reduction: %.1f%%\nVertices: %s -> %s"),
			CollapsedVertices,
			*FString::FormatAsNumber(TargetTris),
			*FString::FormatAsNumber(BeforeTris),
			*FString::FormatAsNumber(AfterTris),
			BeforeTris > 0 ? (1.0f - (float)AfterTris / (float)BeforeTris) * 100.0f : 0.0f,
			*FString::FormatAsNumber(BeforeVerts),
			*FString::FormatAsNumber(AfterVerts)
		)));

	LastOperationSummary = FString::Printf(
		TEXT("Last operation: Aggressive optimize %.0f%% target | Target tris %s | Actual tris %s -> %s"),
		AggressiveOptimizePercent * 100.0f,
		*FString::FormatAsNumber(TargetTris),
		*FString::FormatAsNumber(BeforeTris),
		*FString::FormatAsNumber(AfterTris));

	return FReply::Handled();
}

FReply SMeshOptimizerTab::OnRestoreOriginalClicked()
{
	if (!SelectedMesh.IsValid()) return FReply::Handled();

	const int32 BeforeTris = SelectedMeshInfo.TriangleCount;
	const int32 BeforeVerts = SelectedMeshInfo.VertexCount;

	const bool bRestored = Processor->RestoreOriginalMesh(SelectedMesh.Get());
	RefreshSelectedMesh();

	if (!bRestored)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("RestoreOriginalMissingBackup", "No Mesh Optimizer backup was found for this mesh."));
		return FReply::Handled();
	}

	LastOperationSummary = FString::Printf(
		TEXT("Last operation: Restored original | Triangles %s -> %s | Vertices %s -> %s"),
		*FString::FormatAsNumber(BeforeTris),
		*FString::FormatAsNumber(SelectedMeshInfo.TriangleCount),
		*FString::FormatAsNumber(BeforeVerts),
		*FString::FormatAsNumber(SelectedMeshInfo.VertexCount));

	return FReply::Handled();
}

FReply SMeshOptimizerTab::OnRemoveUnusedMaterialsClicked()
{
	if (!SelectedMesh.IsValid()) return FReply::Handled();

	UStaticMesh* TargetMesh = ResolveTargetMeshForEdit();
	if (!TargetMesh) return FReply::Handled();

	int32 Removed = Processor->RemoveUnusedMaterialSlots(TargetMesh);
	RefreshSelectedMesh();

	FMessageDialog::Open(EAppMsgType::Ok,
		FText::FromString(FString::Printf(TEXT("Removed %d unused material slot(s)."), Removed)));

	return FReply::Handled();
}

FReply SMeshOptimizerTab::OnConsolidateMaterialsClicked()
{
	if (!SelectedMesh.IsValid()) return FReply::Handled();

	UStaticMesh* TargetMesh = ResolveTargetMeshForEdit();
	if (!TargetMesh) return FReply::Handled();

	int32 Merged = Processor->ConsolidateDuplicateMaterials(TargetMesh);
	RefreshSelectedMesh();

	FMessageDialog::Open(EAppMsgType::Ok,
		FText::FromString(FString::Printf(TEXT("Consolidated %d duplicate material slot(s)."), Merged)));

	return FReply::Handled();
}

// ─── Batch Actions ──────────────────────────────────────────────────

FReply SMeshOptimizerTab::OnAnalyzeLevelClicked()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UMeshOptimizationSettings* TempSettings = NewObject<UMeshOptimizationSettings>();
	TempSettings->SelectionMode = EMeshSelectionMode::EntireLevel;
	Analyzer->AnalyzeMeshes(TempSettings, World);
	return FReply::Handled();
}

FReply SMeshOptimizerTab::OnBatchReduceClicked()
{
	if (Analyzer->GetResults().Num() == 0)
	{
		OnAnalyzeLevelClicked();
	}

	FMeshReductionConfig Config;
	Config.PercentTriangles = ReductionPercent;
	int32 Count = Processor->BatchReduceMeshes(Analyzer->GetResults(), Config);
	FMessageDialog::Open(EAppMsgType::Ok,
		FText::FromString(FString::Printf(TEXT("Reduced %d meshes to %.0f%%."), Count, ReductionPercent * 100.f)));
	OnAnalyzeLevelClicked(); // refresh stats
	return FReply::Handled();
}

FReply SMeshOptimizerTab::OnBatchLODsClicked()
{
	if (Analyzer->GetResults().Num() == 0)
	{
		OnAnalyzeLevelClicked();
	}

	FLODGenerationConfig Config;
	Config.NumLODs = LODCount;
	Config.bAutoComputeScreenSizes = true;
	int32 Count = Processor->BatchGenerateLODs(Analyzer->GetResults(), Config);
	FMessageDialog::Open(EAppMsgType::Ok,
		FText::FromString(FString::Printf(TEXT("Generated %d LODs for %d meshes."), LODCount, Count)));
	OnAnalyzeLevelClicked();
	return FReply::Handled();
}

FReply SMeshOptimizerTab::OnBatchNaniteClicked()
{
	if (Analyzer->GetResults().Num() == 0)
	{
		OnAnalyzeLevelClicked();
	}

	FNaniteConfig Config;
	Config.bEnableNanite = true;
	Config.KeepPercentTriangles = 1.0f;
	Config.bUsePercentTrianglesForFallback = true;
	Config.bGenerateFallback = true;
	Config.FallbackPercentTriangles = NaniteFallback;
	Config.FallbackRelativeError = 0.0f;
	Config.bSkipTranslucentMeshes = true;
	int32 Count = Processor->BatchSetNanite(Analyzer->GetResults(), Config);
	FMessageDialog::Open(EAppMsgType::Ok,
		FText::FromString(FString::Printf(TEXT("Enabled Nanite on %d meshes."), Count)));
	OnAnalyzeLevelClicked();
	return FReply::Handled();
}

FReply SMeshOptimizerTab::OnExportCSVClicked()
{
	if (Analyzer->GetResults().Num() == 0)
	{
		OnAnalyzeLevelClicked();
	}

	FString CSV = Analyzer->ExportToCSV();
	if (CSV.IsEmpty()) return FReply::Handled();

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return FReply::Handled();

	TArray<FString> OutFiles;
	DesktopPlatform->SaveFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Export Mesh Analysis"),
		FPaths::ProjectSavedDir(),
		TEXT("MeshAnalysis.csv"),
		TEXT("CSV Files (*.csv)|*.csv"),
		0,
		OutFiles);

	if (OutFiles.Num() > 0)
	{
		FFileHelper::SaveStringToFile(CSV, *OutFiles[0]);
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
