#include "MeshOptimizerModule.h"
#include "MeshOptimizerCommands.h"
#include "SMeshOptimizerTab.h"

#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Styling/AppStyle.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "FMeshOptimizerModule"

const FName FMeshOptimizerModule::MeshOptimizerTabName("MeshOptimizerTab");

void FMeshOptimizerModule::StartupModule()
{
	FMeshOptimizerCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);
	PluginCommands->MapAction(
		FMeshOptimizerCommands::Get().OpenMeshOptimizer,
		FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(FName("MeshOptimizerTab"));
		}),
		FCanExecuteAction());

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		MeshOptimizerTabName,
		FOnSpawnTab::CreateRaw(this, &FMeshOptimizerModule::OnSpawnTab))
		.SetDisplayName(LOCTEXT("TabTitle", "Mesh Optimizer"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Open the Mesh Optimizer tool for batch static mesh optimization"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FMeshOptimizerModule::RegisterMenus));
}

void FMeshOptimizerModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FMeshOptimizerCommands::Unregister();
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(MeshOptimizerTabName);
}

void FMeshOptimizerModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	// Add to Tools menu
	UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
	FToolMenuSection& Section = ToolsMenu->FindOrAddSection("MeshOptimizer");
	Section.AddMenuEntryWithCommandList(
		FMeshOptimizerCommands::Get().OpenMeshOptimizer,
		PluginCommands,
		LOCTEXT("MenuEntry", "Mesh Optimizer"),
		LOCTEXT("MenuEntryTooltip", "Open Mesh Optimizer for batch static mesh optimization"));

	// Add toolbar button
	UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
	FToolMenuSection& ToolbarSection = ToolbarMenu->FindOrAddSection("MeshOptimizer");
	ToolbarSection.AddEntry(FToolMenuEntry::InitToolBarButton(
		FMeshOptimizerCommands::Get().OpenMeshOptimizer,
		LOCTEXT("ToolbarButton", "Mesh Optimizer"),
		LOCTEXT("ToolbarTooltip", "Open Mesh Optimizer"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports")));
}

TSharedRef<SDockTab> FMeshOptimizerModule::OnSpawnTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("TabLabel", "Mesh Optimizer"))
		[
			SNew(SMeshOptimizerTab)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMeshOptimizerModule, MeshOptimizer)
