// Copyright Pix Philosophy (HK) Limited.

#pragma once

#include "CoreMinimal.h"
#include "YcodeLinkToolSet.h"

// The exact compile environment of every shader type bound to a .usf,
// evaluated by the engine itself: FShaderType::ShouldCompilePermutation and
// ModifyCompilationEnvironment for each permutation, plus what
// GlobalBeginCompileShader adds for the platform. This is the authority the
// IDE's define strip uses while the editor runs; the C++ source scan is only
// the offline fallback.
class FYcodeShaderPermutations : public FYcodeLinkToolSet
{
public:
	FYcodeShaderPermutations();
};
