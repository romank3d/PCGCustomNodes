// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGCActorSelectorExtended.h"

#include "PCGComponent.h"
#include "PCGModule.h"
//#include "Grid/PCGPartitionActor.h"
#include "Helpers/PCGActorHelpers.h"

#include "GameFramework/Actor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGCActorSelectorExtended)

namespace PCGCActorSelectorExtended
{
	// Filter is required if it is not disabled and if we are gathering all world actors or gathering all children.
	bool FilterRequired(const FPCGCActorSelectorExtendedSettings& InSettings)
	{
		return (InSettings.ActorFilterCustom == EPCGActorFilterExtended::AllWorldActors || InSettings.bIncludeChildren) && !InSettings.bDisableFilter;
	}

	// Need to pass a pointer of pointer to the found actor. The lambda will capture this pointer and modify its value when an actor is found.
	TFunction<bool(AActor*)> GetFilteringFunction(const FPCGCActorSelectorExtendedSettings& InSettings, const TFunction<bool(const AActor*)>& BoundsCheck, const TFunction<bool(const AActor*)>& SelfIgnoreCheck, TArray<AActor*>& InFoundActors)
	{
		if (!FilterRequired(InSettings))
		{
			return [&InFoundActors, &BoundsCheck, &SelfIgnoreCheck](AActor* Actor) -> bool
			{
				if (BoundsCheck(Actor) && SelfIgnoreCheck(Actor))
				{
					InFoundActors.Add(Actor);
				}
				return true;
			};
		}

		const bool bMultiSelect = InSettings.bSelectMultiple;

		switch (InSettings.ActorSelection)
		{
		case EPCGActorSelection::ByTag:
			return[ActorSelectionTag = InSettings.ActorSelectionTag, &InFoundActors, bMultiSelect, &BoundsCheck, &SelfIgnoreCheck](AActor* Actor) -> bool
			{
				if (Actor && Actor->ActorHasTag(ActorSelectionTag) && BoundsCheck(Actor) && SelfIgnoreCheck(Actor))
				{
					InFoundActors.Add(Actor);
					return bMultiSelect;
				}

				return true;
			};

		case EPCGActorSelection::ByClass:
			return[ActorSelectionClass = InSettings.ActorSelectionClass, &InFoundActors, bMultiSelect, &BoundsCheck, &SelfIgnoreCheck](AActor* Actor) -> bool
			{
				if (Actor && Actor->IsA(ActorSelectionClass) && BoundsCheck(Actor) && SelfIgnoreCheck(Actor))
				{
					InFoundActors.Add(Actor);
					return bMultiSelect;
				}

				return true;
			};

		case EPCGActorSelection::ByName:
			UE_LOG(LogPCG, Error, TEXT("PCGCActorSelectorExtended::GetFilteringFunction: Unsupported value for EPCGActorSelectionExtended - selection by name is no longer supported."));
			break;

		default:
			break;
		}

		return [](AActor* Actor) -> bool { return false; };
	}

	TArray<AActor*> FindActors(const FPCGCActorSelectorExtendedSettings& Settings, const UPCGComponent* InComponent, const TFunction<bool(const AActor*)>& BoundsCheck, const TFunction<bool(const AActor*)>& SelfIgnoreCheck)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PCGCActorSelector::FindActor);

		UWorld* World = InComponent ? InComponent->GetWorld() : nullptr;
		AActor* Self = InComponent ? InComponent->GetOwner() : nullptr;

		TArray<AActor*> FoundActors;

		if (!World)
		{
			return FoundActors;
		}

		// Early out if we have not the information necessary.
		const bool bNoTagInfo = Settings.ActorSelection == EPCGActorSelection::ByTag && Settings.ActorSelectionTag == NAME_None;
		const bool bNoClassInfo = Settings.ActorSelection == EPCGActorSelection::ByClass && !Settings.ActorSelectionClass;

		if (FilterRequired(Settings) && (bNoTagInfo || bNoClassInfo))
		{
			return FoundActors;
		}

		// We pass FoundActor ref, that will be captured by the FilteringFunction
		// It will modify the FoundActor pointer to the found actor, if found.
		TFunction<bool(AActor*)> FilteringFunction = PCGCActorSelectorExtended::GetFilteringFunction(Settings, BoundsCheck, SelfIgnoreCheck, FoundActors);

		// In case of iterating over all actors in the world, call our filtering function and get out.
		if (Settings.ActorFilterCustom == EPCGActorFilterExtended::AllWorldActors)
		{
			// A potential optimization if we know the sought actors are collide-able could be to obtain overlaps via a collision query.
			UPCGActorHelpers::ForEachActorInWorld<AActor>(World, FilteringFunction);

			// FoundActor is set by the FilteringFunction (captured)
			return FoundActors;
		}

		// Otherwise, gather all the actors we need to check
		TArray<AActor*> ActorsToCheck;
		switch (Settings.ActorFilterCustom)
		{
		case EPCGActorFilterExtended::Self:
			if (Self)
			{
				ActorsToCheck.Add(Self);
			}
			break;

		case EPCGActorFilterExtended::Parent:
			if (Self)
			{
				if (AActor* Parent = Self->GetParentActor())
				{
					ActorsToCheck.Add(Parent);
				}
				else
				{
					// If there is no parent, set the owner as the parent.
					ActorsToCheck.Add(Self);
				}
			}
			break;

		case EPCGActorFilterExtended::Root:
		{
			AActor* Current = Self;
			while (Current != nullptr)
			{
				AActor* Parent = Current->GetParentActor();
				if (Parent == nullptr)
				{
					ActorsToCheck.Add(Current);
					break;
				}
				Current = Parent;
			}

			break;
		}

		//case EPCGActorFilter::Original:
		//{
		//	APCGPartitionActor* PartitionActor = Cast<APCGPartitionActor>(Self);
		//	UPCGComponent* OriginalComponent = (PartitionActor && InComponent) ? PartitionActor->GetOriginalComponent(InComponent) : nullptr;
		//	AActor* OriginalActor = OriginalComponent ? OriginalComponent->GetOwner() : nullptr;
		//	if (OriginalActor)
		//	{
		//		ActorsToCheck.Add(OriginalActor);
		//	}
		//	else if (Self)
		//	{
		//		ActorsToCheck.Add(Self);
		//	}
		//}

		default:
			break;
		}

		if (Settings.bIncludeChildren)
		{
			const int32 InitialCount = ActorsToCheck.Num();
			for (int32 i = 0; i < InitialCount; ++i)
			{
				ActorsToCheck[i]->GetAttachedActors(ActorsToCheck, /*bResetArray=*/ false, /*bRecursivelyIncludeAttachedActors=*/ true);
			}
		}

		for (AActor* Actor : ActorsToCheck)
		{
			// FoundActor is set by the FilteringFunction (captured)
			if (!FilteringFunction(Actor))
			{
				break;
			}
		}

		return FoundActors;
	}

	AActor* FindActor(const FPCGCActorSelectorExtendedSettings& InSettings, UPCGComponent* InComponent, const TFunction<bool(const AActor*)>& BoundsCheck, const TFunction<bool(const AActor*)>& SelfIgnoreCheck)
	{
		// In order to make sure we don't try to select multiple, we'll do a copy of the settings here.
		FPCGCActorSelectorExtendedSettings Settings = InSettings;
		Settings.bSelectMultiple = false;

		TArray<AActor*> Actors = FindActors(Settings, InComponent, BoundsCheck, SelfIgnoreCheck);
		return Actors.IsEmpty() ? nullptr : Actors[0];
	}
}

FPCGActorSelectionKeyExtended::FPCGActorSelectionKeyExtended(EPCGActorFilter InFilter)
{
	check(InFilter != EPCGActorFilter::AllWorldActors);
	ActorFilter = InFilter;
}

FPCGActorSelectionKeyExtended::FPCGActorSelectionKeyExtended(FName InTag)
{
	Selection = EPCGActorSelection::ByTag;
	Tag = InTag;
	ActorFilter = EPCGActorFilter::AllWorldActors;
}

FPCGActorSelectionKeyExtended::FPCGActorSelectionKeyExtended(TSubclassOf<AActor> InSelectionClass)
{
	Selection = EPCGActorSelection::ByClass;
	ActorSelectionClass = InSelectionClass;
	ActorFilter = EPCGActorFilter::AllWorldActors;
}

void FPCGActorSelectionKeyExtended::SetExtraDependency(const UClass* InExtraDependency)
{
	OptionalExtraDependency = InExtraDependency;
}

bool FPCGActorSelectionKeyExtended::IsMatching(const AActor* InActor, const UPCGComponent* InComponent) const
{
	if (!InActor)
	{
		return false;
	}
	
	// If we filter something else than all world actors, matching depends on the component.
	// Re-use the same mecanism than Get Actor Data, which should be cheap since we don't look for all actors in the world.
	if (ActorFilter != EPCGActorFilter::AllWorldActors)
	{
		// InKey provide the info for selecting a given actor.
		// We reconstruct the selector settings from this key, and we also force it to SelectMultiple, since
		// we want to gather all the actors that matches this given key, to find if ours matches.
		FPCGCActorSelectorExtendedSettings SelectorSettings = FPCGCActorSelectorExtendedSettings::ReconstructFromKey(*this);
		SelectorSettings.bSelectMultiple = true;
		TArray<AActor*> AllActors = PCGCActorSelectorExtended::FindActors(SelectorSettings, InComponent, [](const AActor*) { return true; }, [](const AActor*) { return true; });
		return AllActors.Contains(InActor);
	}

	switch (Selection)
	{
	case EPCGActorSelection::ByTag:
		return InActor->ActorHasTag(Tag);
	case EPCGActorSelection::ByClass:
		return InActor->IsA(ActorSelectionClass);
	default:
		return false;
	}
}
#if WITH_EDITOR
bool FPCGActorSelectionKey::operator==(const FPCGActorSelectionKey& InOther) const
{
	if (ActorFilter != InOther.ActorFilter || Selection != InOther.Selection || OptionalExtraDependency != InOther.OptionalExtraDependency)
	{
		return false;
	}

	switch (Selection)
	{
	case EPCGActorSelection::ByTag:
		return Tag == InOther.Tag;
	case EPCGActorSelection::ByClass:
		return ActorSelectionClass == InOther.ActorSelectionClass;
	case EPCGActorSelection::Unknown: // Fall-through
	case EPCGActorSelection::ByName:
		return true;
	default:
	{
		checkNoEntry();
		return true;
	}
	}
}

uint32 GetTypeHash(const FPCGActorSelectionKey& In)
{
	uint32 HashResult = HashCombine(GetTypeHash(In.ActorFilter), GetTypeHash(In.Selection));
	HashResult = HashCombine(HashResult, GetTypeHash(In.Tag));
	HashResult = HashCombine(HashResult, GetTypeHash(In.ActorSelectionClass));
	HashResult = HashCombine(HashResult, GetTypeHash(In.OptionalExtraDependency));

	return HashResult;
}


FText FPCGCActorSelectorExtendedSettings::GetTaskNameSuffix() const
{
	if (ActorFilterCustom == EPCGActorFilterExtended::AllWorldActors)
	{
		if (ActorSelection == EPCGActorSelection::ByClass)
		{
			return (ActorSelectionClass.Get() ? ActorSelectionClass->GetDisplayNameText() : FText::FromName(NAME_None));
		}
		else if (ActorSelection == EPCGActorSelection::ByTag)
		{
			return FText::FromName(ActorSelectionTag);
		}
	}
	else if(const UEnum* EnumPtr = StaticEnum<EPCGActorFilter>())
	{
		return EnumPtr->GetDisplayNameTextByValue(static_cast<__underlying_type(EPCGActorFilterExtended)>(ActorFilterCustom));
	}

	return FText();
}

FName FPCGCActorSelectorExtendedSettings::GetTaskName(const FText& Prefix) const
{
	return FName(FText::Format(NSLOCTEXT("PCGCActorSelectorExtendedSettings", "NodeTitleFormat", "{0} ({1})"), Prefix, GetTaskNameSuffix()).ToString());
}
#endif // WITH_EDITOR

FPCGActorSelectionKeyExtended FPCGCActorSelectorExtendedSettings::GetAssociatedKey() const
{
	if (ActorFilterCustom != EPCGActorFilterExtended::AllWorldActors)
	{
		
		return FPCGActorSelectionKeyExtended(static_cast<EPCGActorFilter>(ActorFilterCustom));
	}

	switch (ActorSelection)
	{
	case EPCGActorSelection::ByTag:
		return FPCGActorSelectionKeyExtended(ActorSelectionTag);
	case EPCGActorSelection::ByClass:
		return FPCGActorSelectionKeyExtended(ActorSelectionClass);
	default:
		return FPCGActorSelectionKeyExtended();
	}
}

FPCGCActorSelectorExtendedSettings FPCGCActorSelectorExtendedSettings::ReconstructFromKey(const FPCGActorSelectionKey& InKey)
{
	FPCGCActorSelectorExtendedSettings Result{};
	Result.ActorFilterCustom = static_cast<EPCGActorFilterExtended>(InKey.ActorFilter);
	Result.ActorFilter = InKey.ActorFilter;
	Result.ActorSelection = InKey.Selection;
	Result.ActorSelectionTag = InKey.Tag;
	Result.ActorSelectionClass = InKey.ActorSelectionClass;

	return Result;
}