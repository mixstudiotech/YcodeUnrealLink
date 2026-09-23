// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#include "YcodeShaderPermutations.h"

#include "YcodeLinkLog.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "MaterialShader.h"
#include "MaterialShaderType.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "MeshMaterialShader.h"
#include "MeshMaterialShaderType.h"
#include "Misc/Paths.h"
#include "RHIShaderPlatform.h"
#include "RHIStrings.h"
#include "Serialization/MemoryLayout.h"
#include "Shader.h"
#include "ShaderCompiler.h"
#include "ShaderCompilerCore.h"
#include "ShaderCore.h"
#include "VertexFactory.h"

namespace
{

using FDefines = TMap<FString, FString>;
using EKind = FShaderType::EShaderTypeForDynamicCast;

// `// #define NAME VALUE` lines are the only public view of the definitions.
FDefines DefinesOf(const FShaderCompilerEnvironment& Environment)
{
	FDefines Defines;
	TArray<FString> Lines;
	Environment.GetDefinitionsAsCommentedCode().ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		FString Rest;
		if (!Line.Split(TEXT("#define "), nullptr, &Rest))
		{
			continue;
		}
		FString Name, Value;
		if (!Rest.Split(TEXT(" "), &Name, &Value))
		{
			Name = Rest;
		}
		Defines.Add(Name.TrimStartAndEnd(), Value.TrimStartAndEnd());
	}
	return Defines;
}

// Absolute paths map back through the shader source directory mappings;
// virtual paths pass through.
FString VirtualShaderPath(const FString& File)
{
	if (File.StartsWith(TEXT("/")))
	{
		return File;
	}
	FString Full = FPaths::ConvertRelativePathToFull(File);
	FPaths::NormalizeFilename(Full);
	for (const TPair<FString, FString>& Mapping : AllShaderSourceDirectoryMappings())
	{
		FString Real = FPaths::ConvertRelativePathToFull(Mapping.Value);
		FPaths::NormalizeFilename(Real);
		if (Full.StartsWith(Real + TEXT("/"), ESearchCase::IgnoreCase))
		{
			return Mapping.Key + Full.Mid(Real.Len());
		}
	}
	return FString();
}

const TCHAR* KindName(EKind Kind)
{
	switch (Kind)
	{
	case EKind::Global: return TEXT("Global");
	case EKind::Material: return TEXT("Material");
	case EKind::MeshMaterial: return TEXT("MeshMaterial");
	case EKind::Niagara: return TEXT("Niagara");
	case EKind::OCIO: return TEXT("OCIO");
	case EKind::ComputeKernel: return TEXT("ComputeKernel");
	case EKind::NNERuntimeIREE: return TEXT("NNERuntimeIREE");
	default: return TEXT("Unknown");
	}
}

TSharedRef<FJsonObject> ToJson(const FDefines& Defines)
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	for (const TPair<FString, FString>& Pair : Defines)
	{
		Json->SetStringField(Pair.Key, Pair.Value);
	}
	return Json;
}

// Everything a permutation query shares across shader types.
struct FQuery
{
	EShaderPlatform Platform = SP_NumPlatforms;
	EShaderPermutationFlags Flags = EShaderPermutationFlags::None;
	int32 Limit = 1024;
	bool bIncludePermutations = true;

	// Material/MeshMaterial shader types evaluate against one material; the
	// conservative parameters are built from asset data and need no translation.
	FString MaterialPath;
	TOptional<FConservativeShaderParameters> Material;
	FString VertexFactoryName;
	const FVertexFactoryType* VertexFactory = nullptr;
};

// Runs the same environment setup the compile jobs use for one permutation.
class FPermutationEvaluator
{
public:
	FPermutationEvaluator(const FShaderType& InType, const FQuery& InQuery) : Type(InType), Query(InQuery) {}

	bool Setup(int32 PermutationId, FShaderCompilerEnvironment& Environment) const
	{
		switch (Type.GetTypeForDynamicCast())
		{
		case EKind::Global:
		{
			const FGlobalShaderPermutationParameters Parameters(FName(Type.GetName()), Query.Platform, PermutationId, Query.Flags);
			Type.AsGlobalShaderType()->SetupCompileEnvironment(Query.Platform, PermutationId, Query.Flags, Environment);
			return Type.ShouldCompilePermutation(Parameters);
		}
		case EKind::Material:
		{
			const FMaterialShaderPermutationParameters Parameters(Query.Platform, *Query.Material, PermutationId, Query.Flags);
			Type.ModifyCompilationEnvironment(Parameters, Environment);
			return Type.ShouldCompilePermutation(Parameters);
		}
		case EKind::MeshMaterial:
		{
			const FVertexFactoryShaderPermutationParameters VertexFactoryParameters(Query.Platform, *Query.Material, Query.VertexFactory, &Type, Query.Flags);
			const FMeshMaterialShaderPermutationParameters Parameters(Query.Platform, *Query.Material, Query.VertexFactory, PermutationId, Query.Flags);
			Query.VertexFactory->ModifyCompilationEnvironment(VertexFactoryParameters, Environment);
			Type.ModifyCompilationEnvironment(Parameters, Environment);
			return Query.VertexFactory->ShouldCache(VertexFactoryParameters) && Type.ShouldCompilePermutation(Parameters);
		}
		default:
			return false;
		}
	}

	// What GlobalBeginCompileShader layers on top of the shader type's own
	// environment: frequency, shading path, stereo, platform features.
	FDefines PlatformDefines() const
	{
		if (!GShaderCompilingManager)
		{
			return {};
		}
		FShaderCompilerInput Input;
		Setup(0, Input.Environment);
		const FDefines Own = DefinesOf(Input.Environment);
		const FVertexFactoryType* VertexFactory = Type.GetTypeForDynamicCast() == EKind::MeshMaterial ? Query.VertexFactory : nullptr;
		GlobalBeginCompileShader(TEXT("Ycode"), VertexFactory, &Type, nullptr, 0, Type.GetShaderFilename(), Type.GetFunctionName(),
			FShaderTarget(Type.GetFrequency(), Query.Platform), Input, /*bAllowDevelopmentShaderCompile=*/true);
		FDefines Added;
		for (const TPair<FString, FString>& Pair : DefinesOf(Input.Environment))
		{
			const FString* Before = Own.Find(Pair.Key);
			if (!Before || *Before != Pair.Value)
			{
				Added.Add(Pair.Key, Pair.Value);
			}
		}
		return Added;
	}

private:
	const FShaderType& Type;
	const FQuery& Query;
};

// Splits the enumerated environments into defines that never change (fixed)
// and defines whose value follows the permutation (dimensions).
struct FPermutationTable
{
	TArray<FString> Keys;
	TMap<FString, int32> KeyIndex;
	TArray<TArray<TOptional<FString>>> Values;   // [key][permutation]
	TArray<bool> Compiles;

	void Add(const FDefines& Defines, bool bCompiles)
	{
		const int32 Permutation = Compiles.Add(bCompiles);
		for (TArray<TOptional<FString>>& Column : Values)
		{
			Column.SetNum(Permutation + 1);
		}
		for (const TPair<FString, FString>& Pair : Defines)
		{
			int32* Index = KeyIndex.Find(Pair.Key);
			if (!Index)
			{
				Index = &KeyIndex.Add(Pair.Key, Keys.Add(Pair.Key));
				Values.AddDefaulted_GetRef().SetNum(Permutation + 1);
			}
			Values[*Index][Permutation] = Pair.Value;
		}
	}

	void Write(FJsonObject& Json, int32 RowLimit) const
	{
		TSharedRef<FJsonObject> Fixed = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Dimensions;
		TArray<int32> DimensionKeys;
		TArray<TArray<TOptional<FString>>> DimensionValues;   // distinct values per dimension
		for (int32 Key = 0; Key < Keys.Num(); ++Key)
		{
			const TArray<TOptional<FString>>& Column = Values[Key];
			TArray<TOptional<FString>> Distinct;
			for (const TOptional<FString>& Value : Column)
			{
				Distinct.AddUnique(Value);
			}
			if (Distinct.Num() == 1 && Distinct[0].IsSet())
			{
				Fixed->SetStringField(Keys[Key], *Distinct[0]);
				continue;
			}
			TArray<TSharedPtr<FJsonValue>> ValueList;
			for (const TOptional<FString>& Value : Distinct)
			{
				ValueList.Add(Value.IsSet() ? TSharedPtr<FJsonValue>(MakeShared<FJsonValueString>(*Value))
				                            : TSharedPtr<FJsonValue>(MakeShared<FJsonValueNull>()));
			}
			TSharedRef<FJsonObject> Dimension = MakeShared<FJsonObject>();
			Dimension->SetStringField(TEXT("define"), Keys[Key]);
			Dimension->SetArrayField(TEXT("values"), ValueList);
			Dimensions.Add(MakeShared<FJsonValueObject>(Dimension));
			DimensionKeys.Add(Key);
			DimensionValues.Add(MoveTemp(Distinct));
		}
		Json.SetObjectField(TEXT("fixedDefines"), Fixed);
		Json.SetArrayField(TEXT("dimensions"), Dimensions);
		if (RowLimit <= 0)
		{
			return;
		}
		TArray<TSharedPtr<FJsonValue>> Permutations;
		for (int32 Permutation = 0; Permutation < FMath::Min(Compiles.Num(), RowLimit); ++Permutation)
		{
			TArray<TSharedPtr<FJsonValue>> Indices;
			for (int32 Dimension = 0; Dimension < DimensionKeys.Num(); ++Dimension)
			{
				Indices.Add(MakeShared<FJsonValueNumber>(DimensionValues[Dimension].IndexOfByKey(Values[DimensionKeys[Dimension]][Permutation])));
			}
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("id"), Permutation);
			Entry->SetBoolField(TEXT("compiles"), Compiles[Permutation]);
			Entry->SetArrayField(TEXT("values"), Indices);
			Permutations.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Json.SetArrayField(TEXT("permutations"), Permutations);
	}
};

bool NeedsMaterial(EKind Kind)
{
	return Kind == EKind::Material || Kind == EKind::MeshMaterial;
}

// Resolves the material (and vertex factory) the material shader types are
// evaluated against; a failure is reported per shader type, not for the file.
FString ResolveMaterial(FQuery& Query, const FString& RequestedMaterial, const FString& RequestedVertexFactory)
{
	UMaterialInterface* Material = RequestedMaterial.IsEmpty() ? nullptr : LoadObject<UMaterialInterface>(nullptr, *RequestedMaterial);
	if (!Material)
	{
		if (!RequestedMaterial.IsEmpty())
		{
			return FString::Printf(TEXT("material not found: %s"), *RequestedMaterial);
		}
		Material = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	const FMaterialResource* Resource = Material ? Material->GetMaterialResource(Query.Platform) : nullptr;
	if (!Resource)
	{
		return TEXT("material has no resource for this platform");
	}
	Query.MaterialPath = Material->GetPathName();
	Query.Material.Emplace(Resource);

	Query.VertexFactoryName = RequestedVertexFactory.IsEmpty() ? TEXT("FLocalVertexFactory") : RequestedVertexFactory;
	Query.VertexFactory = FVertexFactoryType::GetVFByName(FHashedName(*Query.VertexFactoryName));
	return FString();
}

TSharedRef<FJsonObject> ReportType(const FShaderType& Type, const FQuery& Query, const FString& MaterialError)
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	const EKind Kind = Type.GetTypeForDynamicCast();
	Json->SetStringField(TEXT("name"), Type.GetName());
	Json->SetStringField(TEXT("kind"), KindName(Kind));
	Json->SetStringField(TEXT("entryPoint"), Type.GetFunctionName());
	Json->SetStringField(TEXT("frequency"), GetShaderFrequencyString(Type.GetFrequency(), /*bIncludePrefix=*/false));
	Json->SetNumberField(TEXT("permutationCount"), Type.GetPermutationCount());

	FString Unsupported;
	if (Kind != EKind::Global && !NeedsMaterial(Kind))
	{
		Unsupported = FString::Printf(TEXT("%s shader types are not evaluated"), KindName(Kind));
	}
	else if (NeedsMaterial(Kind) && !MaterialError.IsEmpty())
	{
		Unsupported = MaterialError;
	}
	else if (Kind == EKind::MeshMaterial && !Query.VertexFactory)
	{
		Unsupported = FString::Printf(TEXT("vertex factory not found: %s"), *Query.VertexFactoryName);
	}
	Json->SetBoolField(TEXT("supported"), Unsupported.IsEmpty());
	if (!Unsupported.IsEmpty())
	{
		Json->SetStringField(TEXT("reason"), Unsupported);
		return Json;
	}
	if (NeedsMaterial(Kind))
	{
		Json->SetStringField(TEXT("material"), Query.MaterialPath);
		Json->SetStringField(TEXT("materialParameters"), TEXT("conservative"));
		// The material's own defines (MATERIALBLENDING_*, MATERIAL_SHADINGMODEL_*, …)
		// come from translating the material and are not part of this answer.
		Json->SetBoolField(TEXT("materialEnvironment"), false);
		if (Kind == EKind::MeshMaterial)
		{
			Json->SetStringField(TEXT("vertexFactory"), Query.VertexFactoryName);
		}
	}

	// The dimension analysis walks the permutation space independently of how
	// many rows the caller wants back; one permutation costs ~0.1 ms, so the
	// cap keeps a worst-case shader type under a second on the game thread.
	constexpr int32 MaxEnumerated = 8192;
	const FPermutationEvaluator Evaluator(Type, Query);
	const int32 Enumerated = FMath::Min(Type.GetPermutationCount(), MaxEnumerated);
	FPermutationTable Table;
	for (int32 PermutationId = 0; PermutationId < Enumerated; ++PermutationId)
	{
		FShaderCompilerEnvironment Environment;
		const bool bCompiles = Evaluator.Setup(PermutationId, Environment);
		Table.Add(DefinesOf(Environment), bCompiles);
	}
	Json->SetNumberField(TEXT("enumerated"), Enumerated);
	Json->SetBoolField(TEXT("truncated"), Enumerated < Type.GetPermutationCount());
	Json->SetObjectField(TEXT("platformDefines"), ToJson(Evaluator.PlatformDefines()));
	Table.Write(*Json, Query.bIncludePermutations ? Query.Limit : 0);
	return Json;
}

} // namespace

FYcodeShaderPermutations::FYcodeShaderPermutations()
{
	Add(TEXT("ue_shader_permutations"),
		TEXT("Shader types compiled from a .usf and, for each, the exact compile defines the engine produces per permutation: "
			 "fixedDefines (same in every permutation), dimensions (defines whose value follows the permutation, with their value sets), "
			 "platformDefines (added by GlobalBeginCompileShader for the platform) and an optional per-permutation table with "
			 "ShouldCompilePermutation results. Material/MeshMaterial shader types are evaluated against one material "
			 "(default: the engine default surface material) and vertex factory; the material's own defines are not included."),
		FYcodeLinkSchema()
			.String(TEXT("file"), TEXT("Shader source: virtual path (/Engine/Private/PostProcessTonemap.usf) or an absolute path inside a mapped shader directory"), true)
			.String(TEXT("shaderType"), TEXT("Only this shader type (registered C++ class name, e.g. FTonemapPS)"))
			.String(TEXT("platform"), TEXT("Shader platform name (PCD3D_SM6, PCD3D_SM5, VULKAN_SM6, …); default: the editor's current platform"))
			.String(TEXT("material"), TEXT("Material asset path used for Material/MeshMaterial shader types"))
			.String(TEXT("vertexFactory"), TEXT("Vertex factory type name for MeshMaterial shader types (default FLocalVertexFactory)"))
			.Integer(TEXT("permutationLimit"), TEXT("Maximum rows in the per-permutation table (default 1024); fixedDefines/dimensions always cover the first 8192 permutations"))
			.Boolean(TEXT("includePermutations"), TEXT("Include the per-permutation table (default true)"))
			.Build(),
		true,
		[](const TSharedRef<FJsonObject>& Args, const FYcodeLinkToolCallback& Done)
		{
			const FString File = YcodeLinkJson::GetString(Args, TEXT("file"));
			const FString VirtualPath = VirtualShaderPath(File);
			if (VirtualPath.IsEmpty())
			{
				Done(FYcodeLinkToolResult::Error(FString::Printf(TEXT("%s is not inside a mapped shader directory"), *File)));
				return;
			}

			FQuery Query;
			Query.Platform = GMaxRHIShaderPlatform;
			const FString PlatformName = YcodeLinkJson::GetString(Args, TEXT("platform"));
			if (!PlatformName.IsEmpty())
			{
				Query.Platform = FDataDrivenShaderPlatformInfo::GetShaderPlatformFromName(FName(*PlatformName));
				if (Query.Platform == SP_NumPlatforms)
				{
					Done(FYcodeLinkToolResult::Error(FString::Printf(TEXT("unknown shader platform: %s"), *PlatformName)));
					return;
				}
			}
			FPlatformTypeLayoutParameters LayoutParameters;
			LayoutParameters.InitializeForCurrent();
			Query.Flags = GetShaderPermutationFlags(LayoutParameters);
			Query.Limit = FMath::Clamp(YcodeLinkJson::GetInt(Args, TEXT("permutationLimit"), 1024), 1, 65536);
			Query.bIncludePermutations = YcodeLinkJson::GetBool(Args, TEXT("includePermutations"), true);
			const FString TypeFilter = YcodeLinkJson::GetString(Args, TEXT("shaderType"));

			TArray<const FShaderType*> Types;
			for (TLinkedList<FShaderType*>::TIterator It(FShaderType::GetTypeList()); It; It.Next())
			{
				const FShaderType* Type = *It;
				if (VirtualPath.Equals(Type->GetShaderFilename(), ESearchCase::IgnoreCase)
					&& (TypeFilter.IsEmpty() || TypeFilter == Type->GetName()))
				{
					Types.Add(Type);
				}
			}

			FString MaterialError;
			if (Types.ContainsByPredicate([](const FShaderType* Type) { return NeedsMaterial(Type->GetTypeForDynamicCast()); }))
			{
				MaterialError = ResolveMaterial(Query, YcodeLinkJson::GetString(Args, TEXT("material")), YcodeLinkJson::GetString(Args, TEXT("vertexFactory")));
			}

			TArray<TSharedPtr<FJsonValue>> Reports;
			for (const FShaderType* Type : Types)
			{
				Reports.Add(MakeShared<FJsonValueObject>(ReportType(*Type, Query, MaterialError)));
			}
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("file"), VirtualPath);
			Result->SetStringField(TEXT("platform"), LexToString(Query.Platform));
			Result->SetArrayField(TEXT("shaderTypes"), Reports);
			Done(FYcodeLinkToolResult::Object(Result));
		});
}
