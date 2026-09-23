// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#include "YcodeSourceCodeAccessor.h"

#include "IYcodeLink.h"

#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"

class FYcodeSourceCodeAccessModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		Accessor.RefreshAvailability();
		IModularFeatures::Get().RegisterModularFeature(FYcodeSourceCodeAccessor::FeatureType(), &Accessor);
		// An IDE announcing itself over the link becomes the preferred target.
		if (IYcodeLinkModule* Link = IYcodeLinkModule::GetPtr())
		{
			IdeRegisteredHandle = Link->OnIdeRegistered().AddLambda([this](const FYcodeLinkIdeInfo&)
			{
				Accessor.RefreshAvailability();
			});
		}
	}

	virtual void ShutdownModule() override
	{
		if (IdeRegisteredHandle.IsValid())
		{
			if (IYcodeLinkModule* Link = IYcodeLinkModule::GetPtr())
			{
				Link->OnIdeRegistered().Remove(IdeRegisteredHandle);
			}
		}
		IModularFeatures::Get().UnregisterModularFeature(FYcodeSourceCodeAccessor::FeatureType(), &Accessor);
	}

	virtual bool SupportsDynamicReloading() override { return false; }

private:
	FYcodeSourceCodeAccessor Accessor;
	FDelegateHandle IdeRegisteredHandle;
};

IMPLEMENT_MODULE(FYcodeSourceCodeAccessModule, YcodeSourceCodeAccess);
