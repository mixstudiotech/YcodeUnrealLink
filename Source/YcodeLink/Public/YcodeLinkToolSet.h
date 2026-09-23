// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "IYcodeLink.h"
#include "YcodeLinkTool.h"

// Base for a feature that contributes tools to the link. Tools registered
// through Add() are unregistered when the set is destroyed, so a module
// shutdown removes exactly what it added.
class FYcodeLinkToolSet
{
public:
	virtual ~FYcodeLinkToolSet()
	{
		UnregisterAll();
	}

protected:
	void Add(const TCHAR* Name, const TCHAR* Description, const TSharedRef<FJsonObject>& Schema, bool bReadOnly, FYcodeLinkToolHandler Handler)
	{
		IYcodeLinkModule* Link = IYcodeLinkModule::GetPtr();
		if (!Link)
		{
			return;
		}
		FYcodeLinkTool Tool;
		Tool.Name = Name;
		Tool.Description = Description;
		Tool.InputSchema = Schema;
		Tool.bReadOnly = bReadOnly;
		Tool.Handler = MoveTemp(Handler);
		Link->RegisterTool(Tool);
		Names.AddUnique(Tool.Name);
	}

	void Notify(const TCHAR* Method, const TSharedPtr<FJsonObject>& Params) const
	{
		if (IYcodeLinkModule* Link = IYcodeLinkModule::GetPtr())
		{
			Link->Notify(Method, Params);
		}
	}

	void UnregisterAll()
	{
		if (IYcodeLinkModule* Link = IYcodeLinkModule::GetPtr())
		{
			for (const FString& Name : Names)
			{
				Link->UnregisterTool(Name);
			}
		}
		Names.Reset();
	}

private:
	TArray<FString> Names;
};
