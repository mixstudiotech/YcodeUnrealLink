// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#include "YcodeAgentToolSets.h"

#include "Editor.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	FYcodeLinkToolResult Spawn(const TSharedRef<FJsonObject>& Args)
	{
		if (!GEditor)
		{
			return YcodeAgentJson::Failure(TEXT("GEditor is unavailable"));
		}
		const FString AssetPath = YcodeLinkJson::GetString(Args, TEXT("assetPath"));
		if (AssetPath.IsEmpty())
		{
			return YcodeAgentJson::Failure(TEXT("assetPath is required"));
		}
		UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
		if (!Asset)
		{
			Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
		}
		if (!Asset)
		{
			return YcodeAgentJson::Failure(FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath));
		}
		UEditorActorSubsystem* Actors = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
		if (!Actors)
		{
			return YcodeAgentJson::Failure(TEXT("EditorActorSubsystem unavailable"));
		}

		const FVector Location = YcodeAgentJson::ToVector(YcodeLinkJson::GetObject(Args, TEXT("location")), FVector::ZeroVector);
		const FRotator Rotation = YcodeAgentJson::ToRotator(YcodeLinkJson::GetObject(Args, TEXT("rotation")), FRotator::ZeroRotator);

		// A class (generated *_C) or a Blueprint spawns an instance of its actor
		// class; a mesh or other template goes through SpawnActorFromObject.
		AActor* Actor = nullptr;
		UClass* SpawnClass = Cast<UClass>(Asset);
		if (!SpawnClass)
		{
			if (const UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
			{
				SpawnClass = Blueprint->GeneratedClass;
			}
		}
		if (SpawnClass)
		{
			if (!SpawnClass->IsChildOf(AActor::StaticClass()))
			{
				return YcodeAgentJson::Failure(FString::Printf(TEXT("Asset class is not an Actor subclass: %s"), *AssetPath));
			}
			Actor = Actors->SpawnActorFromClass(SpawnClass, Location, Rotation);
		}
		else
		{
			Actor = Actors->SpawnActorFromObject(Asset, Location, Rotation);
		}
		if (!Actor)
		{
			return YcodeAgentJson::Failure(FString::Printf(TEXT("Could not spawn an actor from %s (not a placeable mesh or actor class?)"), *AssetPath));
		}

		const FVector Scale = YcodeAgentJson::ToVector(YcodeLinkJson::GetObject(Args, TEXT("scale")), FVector::OneVector);
		if (!Scale.IsZero())
		{
			Actor->SetActorScale3D(Scale);
		}
		const FString Label = YcodeLinkJson::GetString(Args, TEXT("label"));
		if (!Label.IsEmpty())
		{
			Actor->SetActorLabel(Label);
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Result->SetStringField(TEXT("actorName"), Actor->GetName());
		Result->SetObjectField(TEXT("location"), YcodeAgentJson::FromVector(Actor->GetActorLocation()));
		Result->SetStringField(TEXT("error"), FString());
		return FYcodeLinkToolResult::Object(Result);
	}
}

FYcodeActorSpawner::FYcodeActorSpawner()
{
	Add(TEXT("ue_spawn_actor"), TEXT("Place an actor in the current level from an asset: a StaticMesh (spawns a StaticMeshActor), a Blueprint or an actor class. Location/rotation/scale are optional; label sets the Outliner name."),
		FYcodeLinkSchema()
			.String(TEXT("assetPath"), TEXT("Long object path, e.g. /Engine/BasicShapes/Cube.Cube or /Game/Heroes/BP_Hero.BP_Hero"), true)
			.NumberObject(TEXT("location"), TEXT("World location (cm)"), { TEXT("x"), TEXT("y"), TEXT("z") })
			.NumberObject(TEXT("rotation"), TEXT("Rotation (degrees)"), { TEXT("pitch"), TEXT("yaw"), TEXT("roll") })
			.NumberObject(TEXT("scale"), TEXT("Per-axis scale, default 1,1,1"), { TEXT("x"), TEXT("y"), TEXT("z") })
			.String(TEXT("label"), TEXT("Outliner label"))
			.Build(), false,
		[](const TSharedRef<FJsonObject>& Args, const FYcodeLinkToolCallback& Done)
		{
			Done(Spawn(Args));
		});
}
