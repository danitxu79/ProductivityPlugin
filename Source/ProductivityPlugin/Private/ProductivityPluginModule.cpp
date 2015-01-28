#include "ProductivityPluginModulePCH.h"

#include "SlateBasics.h"
#include "SlateExtras.h"

#include "ProductivityPluginStyle.h"
#include "ProductivityPluginCommands.h"

#include "ILayers.h"
#include "LevelEditor.h"
#include "ScopedTransaction.h"

#include "ProductivityTypes.h"

static const FName ProductivityPluginTabName("ProductivityPlugin");

#define LOCTEXT_NAMESPACE "ProductivityPlugin"

void FProductivityPluginModule::StartupModule()
{
	// This code will execute after your module is loaded into memory (but after global variables are initialized, of course.)
	
	FProductivityPluginStyle::Initialize();
	//FProductivityPluginStyle::ReloadTextures();

	FProductivityPluginCommands::Register();
	FProductivityPluginCommands::BindGlobalStaticToInstancedActions();

#if WITH_EDITOR
	
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FProductivityPluginCommands::Get().StaticToInstanced,
		FExecuteAction::CreateRaw(this, &FProductivityPluginModule::StaticToInstancedClicked),
		FCanExecuteAction());
		
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	
	{
		TSharedPtr<FExtender> MenuExtender = MakeShareable(new FExtender());
		MenuExtender->AddMenuExtension("WindowLayout", EExtensionHook::After, PluginCommands, FMenuExtensionDelegate::CreateRaw(this, &FProductivityPluginModule::AddMenuExtension));

		LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);
	}
	
	{
		TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
		ToolbarExtender->AddToolBarExtension("SourceControl", EExtensionHook::After, PluginCommands, FToolBarExtensionDelegate::CreateRaw(this, &FProductivityPluginModule::AddToolbarExtension));
		
		LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
	}

#endif
}

void FProductivityPluginModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	
	FProductivityPluginStyle::Shutdown();

	FProductivityPluginCommands::Unregister();
}

void FProductivityPluginModule::StaticToInstancedClicked()
{
#if WITH_EDITOR

	const FScopedTransaction Transaction(LOCTEXT("StaticToInstanced", "Convert Statics to Instances and back"));

	{

		/* Set up selected info */
		TArray<AStaticMeshActor*> SelectedSMAs;
		USelection* SMASelection = GEditor->GetSelectedSet(AStaticMeshActor::StaticClass());
		SMASelection->GetSelectedObjects<AStaticMeshActor>(SelectedSMAs);

		TArray<AInstancedMeshWrapper*> SelectedIMWs;
		USelection* IMWSelection = GEditor->GetSelectedSet(AInstancedMeshWrapper::StaticClass());
		IMWSelection->GetSelectedObjects<AInstancedMeshWrapper>(SelectedIMWs);

		SMASelection->Modify();
		IMWSelection->Modify();

		GEditor->GetSelectedActors()->DeselectAll();
		GEditor->GetSelectedObjects()->DeselectAll();
		GEditor->SelectNone(true, true, false);
		GEditor->NoteSelectionChange(true);

		/* Static Mesh to Instanced */
		TArray<FMeshInfo> MeshInfos;
		TArray< TArray<FTransform> > Transforms;

		for (AStaticMeshActor* MeshActor : SelectedSMAs)
		{
			FMeshInfo info;
			info.StaticMesh = MeshActor->GetStaticMeshComponent()->StaticMesh;
			MeshActor->GetStaticMeshComponent()->GetUsedMaterials(info.Materials);

			int32 idx = 0;

			if (MeshInfos.Find(info, idx))
			{
				Transforms[idx].Add(MeshActor->GetTransform());
			}
			else
			{
				TArray<FTransform> newTransformArray;
				newTransformArray.Add(MeshActor->GetTransform());
				MeshInfos.Add(info);
				Transforms.Add(newTransformArray);
			}
		}

		for (int i = 0; i < SelectedSMAs.Num(); ++i)
		{
			SelectedSMAs[i]->GetLevel()->Modify();
			GEditor->Layers->DisassociateActorFromLayers(SelectedSMAs[i]);
			SelectedSMAs[i]->GetWorld()->EditorDestroyActor(SelectedSMAs[i], false);
		}

		SelectedSMAs.Empty();

		for (int i = 0; i < MeshInfos.Num(); ++i)
		{
			AInstancedMeshWrapper* Wrapper = Cast<AInstancedMeshWrapper>(GEditor->AddActor(GEditor->LevelViewportClients[0]->GetWorld()->GetLevel(0), AInstancedMeshWrapper::StaticClass(), FTransform::Identity));
			if (Wrapper)
			{
				Wrapper->Modify();
				Wrapper->InstancedMeshes->SetStaticMesh(MeshInfos[i].StaticMesh);
				for (int j = 0; j < MeshInfos[i].Materials.Num(); ++j)
				{
					Wrapper->InstancedMeshes->SetMaterial(j, MeshInfos[i].Materials[j]);
				}

				for (FTransform aTransform : Transforms[i])
				{
					Wrapper->InstancedMeshes->AddInstanceWorldSpace(aTransform);
				}
			}
		}

		/* Instanced To Static Mesh */

		for (AInstancedMeshWrapper* IMW : SelectedIMWs)
		{
			int32 InstanceCount = IMW->InstancedMeshes->GetInstanceCount();
			UStaticMesh* IMWMesh = IMW->InstancedMeshes->StaticMesh;
			UE_LOG(LogProductivityPlugin, Verbose, TEXT("IMW Mesh: %s"), *IMWMesh->GetFullName());
			
			bool bGroupResultingMeshes = FProductivityPluginCommandCallbacks::OnToggleStaticToInstancedResultGroupedEnabled();
			
			TArray<AStaticMeshActor*> ActorsToGroup;

			for (int i = 0; i < InstanceCount; ++i)
			{
				FTransform InstanceTransform;
				IMW->InstancedMeshes->GetInstanceTransform(i, InstanceTransform, true);

				AStaticMeshActor* SMA = Cast<AStaticMeshActor>(GEditor->AddActor(GEditor->LevelViewportClients[0]->GetWorld()->GetLevel(0), AStaticMeshActor::StaticClass(), InstanceTransform));
				SMA->Modify();
				//@TODO: Figure out why editor is skipping names
				SMA->SetActorLabel(*IMWMesh->GetName());
				SMA->SetMobility(EComponentMobility::Movable);
				SMA->GetStaticMeshComponent()->SetStaticMesh(IMWMesh);
				SMA->SetMobility(EComponentMobility::Static);

				TArray<UMaterialInterface*> Materials;
				IMW->InstancedMeshes->GetUsedMaterials(Materials);

				for (int j = 0; j < Materials.Num(); ++j)
				{
					SMA->GetStaticMeshComponent()->SetMaterial(j, Materials[j]);
				}

				ActorsToGroup.Add(SMA);
			}

			if (bGroupResultingMeshes)
			{
				if (ActorsToGroup.Num() > 1)
				{

					// Store off the current level and make the level that contain the actors to group as the current level
					UWorld* World = ActorsToGroup[0]->GetWorld();
					check(World);
					{
						FActorSpawnParameters SpawnInfo;
						SpawnInfo.OverrideLevel = GEditor->LevelViewportClients[0]->GetWorld()->GetLevel(0);
						AGroupActor* SpawnedGroupActor = World->SpawnActor<AGroupActor>(SpawnInfo);

						for (int32 ActorIndex = 0; ActorIndex < ActorsToGroup.Num(); ++ActorIndex)
						{
							SpawnedGroupActor->Add(*ActorsToGroup[ActorIndex]);
						}

						SpawnedGroupActor->CenterGroupLocation();
						SpawnedGroupActor->bLocked = true;
					}
				}
			}

			IMW->Modify();
			IMW->GetLevel()->Modify();
			GEditor->Layers->DisassociateActorFromLayers(IMW);
			IMW->GetWorld()->EditorDestroyActor(IMW, false);
		}

		SelectedIMWs.Empty();

		// Remove all references to destroyed actors once at the end, instead of once for each Actor destroyed..
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
#endif
	}
}

void FProductivityPluginModule::AddMenuExtension(FMenuBuilder& builder)
{
	{
		
		//builder.AddMenuEntry(
		//	FProductivityPluginCommands::Get().StaticToInstanced,
		//	NAME_None,
		//	FProductivityPluginCommands::Get().StaticToInstanced->GetLabel(),
		//	FProductivityPluginCommands::Get().StaticToInstanced->GetDescription(),
		//	FProductivityPluginCommands::Get().StaticToInstanced->GetIcon(),
		//	NAME_None);
	}
}

void FProductivityPluginModule::AddToolbarExtension(FToolBarBuilder &builder)
{
	builder.BeginSection("Productivity");

	builder.AddToolBarButton(
		FProductivityPluginCommands::Get().StaticToInstanced,
		NAME_None,
		FProductivityPluginCommands::Get().StaticToInstanced->GetLabel(),
		FProductivityPluginCommands::Get().StaticToInstanced->GetDescription(),
		FProductivityPluginCommands::Get().StaticToInstanced->GetIcon(),
		NAME_None);

	FUIAction StaticToInstancedOptionsMenuAction;

	builder.AddComboButton(
		StaticToInstancedOptionsMenuAction,
		FOnGetContent::CreateStatic(&FProductivityPluginCommands::GenerateStaticToInstancedMenuContent, FProductivityPluginCommands::GlobalStaticToInstancedActions.ToSharedRef()),
		LOCTEXT("StaticToInstancedOptions_Label", "Static<>Instanced Options"),
		LOCTEXT("StaticToInstancedOptions_Tooltip", "Options for converting static meshes to instanced meshes and vice versa."),
		FProductivityPluginCommands::Get().StaticToInstanced->GetIcon(),
		true
		);

	builder.EndSection();
}

DEFINE_LOG_CATEGORY(LogProductivityPlugin)

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FProductivityPluginModule, ProductivityPlugin)