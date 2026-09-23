// Copyright Pix Philosophy (HK) Limited.

#include "YcodeAgentToolSets.h"

#include "Editor.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetMathLibrary.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Subsystems/UnrealEditorSubsystem.h"

namespace
{
	UUnrealEditorSubsystem* EditorSubsystem()
	{
		return GEditor ? GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>() : nullptr;
	}

	bool GetPose(FVector& OutLocation, FRotator& OutRotation)
	{
		UUnrealEditorSubsystem* Subsystem = EditorSubsystem();
		return Subsystem && Subsystem->GetLevelViewportCameraInfo(OutLocation, OutRotation);
	}

	void SetPose(const FVector& Location, const FRotator& Rotation)
	{
		if (UUnrealEditorSubsystem* Subsystem = EditorSubsystem())
		{
			Subsystem->SetLevelViewportCameraInfo(Location, Rotation);
		}
	}

	// Outliner label first, then the internal name.
	AActor* FindActorByLabel(const FString& Name)
	{
		if (Name.IsEmpty() || !GEditor)
		{
			return nullptr;
		}
		UEditorActorSubsystem* Actors = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
		if (!Actors)
		{
			return nullptr;
		}
		const TArray<AActor*> All = Actors->GetAllLevelActors();
		for (AActor* Actor : All)
		{
			if (Actor && Actor->GetActorLabel() == Name)
			{
				return Actor;
			}
		}
		for (AActor* Actor : All)
		{
			if (Actor && Actor->GetName() == Name)
			{
				return Actor;
			}
		}
		return nullptr;
	}

	FYcodeLinkToolResult CameraOk(const FVector& Location, const FRotator& Rotation, const FString& ActorResolved = FString())
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetBoolField(TEXT("success"), true);
		Json->SetObjectField(TEXT("location"), YcodeAgentJson::FromVector(Location));
		Json->SetObjectField(TEXT("rotation"), YcodeAgentJson::FromRotator(Rotation));
		if (!ActorResolved.IsEmpty())
		{
			Json->SetStringField(TEXT("actorResolved"), ActorResolved);
		}
		Json->SetStringField(TEXT("error"), FString());
		return FYcodeLinkToolResult::Object(Json);
	}

	FYcodeLinkToolResult Dispatch(const TSharedRef<FJsonObject>& Args)
	{
		const FString Action = YcodeLinkJson::GetString(Args, TEXT("action"), TEXT("Get"));
		FVector Location;
		FRotator Rotation;
		if (!GetPose(Location, Rotation))
		{
			return YcodeAgentJson::Failure(TEXT("GetLevelViewportCameraInfo failed (no level viewport)"));
		}

		if (Action == TEXT("Get"))
		{
			return CameraOk(Location, Rotation);
		}
		if (Action == TEXT("Set"))
		{
			Location = YcodeAgentJson::ToVector(YcodeLinkJson::GetObject(Args, TEXT("location")), Location);
			Rotation = YcodeAgentJson::ToRotator(YcodeLinkJson::GetObject(Args, TEXT("rotation")), Rotation);
			SetPose(Location, Rotation);
			return CameraOk(Location, Rotation);
		}
		if (Action == TEXT("Move"))
		{
			if (const TSharedPtr<FJsonObject> Delta = YcodeLinkJson::GetObject(Args, TEXT("delta")))
			{
				const FVector Raw = YcodeAgentJson::ToVector(Delta, FVector::ZeroVector);
				if (YcodeLinkJson::GetBool(Args, TEXT("relative"), false))
				{
					// x forward, y right, z up in the camera frame.
					const FRotationMatrix Frame(Rotation);
					Location += Rotation.Vector() * Raw.X + Frame.GetScaledAxis(EAxis::Y) * Raw.Y + Frame.GetScaledAxis(EAxis::Z) * Raw.Z;
				}
				else
				{
					Location += Raw;
				}
			}
			if (const TSharedPtr<FJsonObject> RotationDelta = YcodeLinkJson::GetObject(Args, TEXT("rotationDelta")))
			{
				Rotation += YcodeAgentJson::ToRotator(RotationDelta, FRotator::ZeroRotator);
			}
			SetPose(Location, Rotation);
			return CameraOk(Location, Rotation);
		}
		if (Action == TEXT("LookAt"))
		{
			const TSharedPtr<FJsonObject> Target = YcodeLinkJson::GetObject(Args, TEXT("target"));
			if (!Target.IsValid())
			{
				return YcodeAgentJson::Failure(TEXT("target is required for LookAt"));
			}
			const FRotator NewRotation = UKismetMathLibrary::FindLookAtRotation(Location, YcodeAgentJson::ToVector(Target, Location));
			SetPose(Location, NewRotation);
			return CameraOk(Location, NewRotation);
		}
		if (Action == TEXT("FocusOnActor"))
		{
			const FString ActorName = YcodeLinkJson::GetString(Args, TEXT("actorName"));
			if (ActorName.IsEmpty())
			{
				return YcodeAgentJson::Failure(TEXT("actorName is required for FocusOnActor"));
			}
			AActor* Actor = FindActorByLabel(ActorName);
			if (!Actor)
			{
				return YcodeAgentJson::Failure(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
			}
			FVector Origin;
			FVector Extent;
			Actor->GetActorBounds(/*bOnlyCollidingComponents=*/false, Origin, Extent);
			const double Radius = FMath::Max3(Extent.X, Extent.Y, FMath::Max(Extent.Z, 50.0));
			const double MinDistance = YcodeLinkJson::GetNumber(Args, TEXT("minDistance"), 0.0);
			const double Distance = FMath::Max(Radius * 3.0, MinDistance > 0.0 ? MinDistance : 200.0);
			// Behind-and-above framing.
			const FVector NewLocation(Origin.X - Distance, Origin.Y - Distance, Origin.Z + Distance * 0.6);
			const FRotator NewRotation = UKismetMathLibrary::FindLookAtRotation(NewLocation, Origin);
			SetPose(NewLocation, NewRotation);
			return CameraOk(NewLocation, NewRotation, Actor->GetActorLabel());
		}
		return YcodeAgentJson::Failure(FString::Printf(TEXT("Unknown action '%s'"), *Action));
	}
}

FYcodeViewportCamera::FYcodeViewportCamera()
{
	Add(TEXT("ue_viewport_camera"), TEXT("Read or move the level editor viewport camera. Get returns the pose; Set replaces location/rotation; Move applies a delta (relative=true means x forward, y right, z up); LookAt aims at a world position; FocusOnActor frames an actor by Outliner label or name."),
		FYcodeLinkSchema()
			.Enum(TEXT("action"), TEXT("Camera action"), { TEXT("Get"), TEXT("Set"), TEXT("Move"), TEXT("LookAt"), TEXT("FocusOnActor") }, true)
			.NumberObject(TEXT("location"), TEXT("Set: world location (cm)"), { TEXT("x"), TEXT("y"), TEXT("z") })
			.NumberObject(TEXT("rotation"), TEXT("Set: rotation (degrees)"), { TEXT("pitch"), TEXT("yaw"), TEXT("roll") })
			.NumberObject(TEXT("delta"), TEXT("Move: translation delta"), { TEXT("x"), TEXT("y"), TEXT("z") })
			.Boolean(TEXT("relative"), TEXT("Move: delta is in the camera frame"))
			.NumberObject(TEXT("rotationDelta"), TEXT("Move: rotation delta"), { TEXT("pitch"), TEXT("yaw"), TEXT("roll") })
			.NumberObject(TEXT("target"), TEXT("LookAt: world position to aim at"), { TEXT("x"), TEXT("y"), TEXT("z") })
			.String(TEXT("actorName"), TEXT("FocusOnActor: Outliner label or actor name"))
			.Number(TEXT("minDistance"), TEXT("FocusOnActor: lower bound for the framing distance (cm)"))
			.Build(), false,
		[](const TSharedRef<FJsonObject>& Args, const FYcodeLinkToolCallback& Done)
		{
			Done(Dispatch(Args));
		});
}
