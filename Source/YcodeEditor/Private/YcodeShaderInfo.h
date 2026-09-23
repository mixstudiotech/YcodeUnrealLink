// Copyright Pix Philosophy (HK) Limited.

#pragma once

#include "CoreMinimal.h"
#include "YcodeLinkToolSet.h"

// Virtual shader directory mappings ("/Engine" -> .../Engine/Shaders,
// "/Plugin/X" -> ...) written to Intermediate/Ycode/ShaderSourceMappings.ini
// once the engine has registered them, so the IDE can resolve `#include`
// directives in .usf/.ush files without a running editor.
class FYcodeShaderInfo final : public FYcodeLinkToolSet
{
public:
	FYcodeShaderInfo();
	~FYcodeShaderInfo() override;

	static FString MappingFilePath();

private:
	void WriteMappings() const;
	TSharedRef<FJsonObject> MappingsJson() const;

	FDelegateHandle PostEngineInitHandle;
};
