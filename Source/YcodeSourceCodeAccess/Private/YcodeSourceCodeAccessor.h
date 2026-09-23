// Copyright Pix Philosophy (HK) Limited.

#pragma once

#include "CoreMinimal.h"
#include "ISourceCodeAccessor.h"

// Opens the project and source files in Ycode. The IDE's command line is the
// VS Code grammar Ycode already accepts from Unity:
//   ycode "<project.uproject>" -g "<file>:<line>:<column>"
// A running workbench takes the request over its single-instance channel;
// otherwise the launch starts one.
class FYcodeSourceCodeAccessor final : public ISourceCodeAccessor
{
public:
	static FName FeatureType() { return TEXT("SourceCodeAccessor"); }

	// ISourceCodeAccessor
	virtual void RefreshAvailability() override;
	virtual bool CanAccessSourceCode() const override;
	virtual FName GetFName() const override;
	virtual FText GetNameText() const override;
	virtual FText GetDescriptionText() const override;
	virtual bool OpenSolution() override;
	virtual bool OpenSolutionAtPath(const FString& InSolutionPath) override;
	virtual bool DoesSolutionExist() const override;
	virtual bool OpenFileAtLine(const FString& FullPath, int32 LineNumber, int32 ColumnNumber = 0) override;
	virtual bool OpenSourceFiles(const TArray<FString>& AbsoluteSourcePaths) override;
	virtual bool AddSourceFiles(const TArray<FString>& AbsoluteSourcePaths, const TArray<FString>& AvailableModules) override;
	virtual bool SaveAllOpenDocuments() const override;
	virtual void Tick(const float DeltaTime) override {}

private:
	// The .uproject, or the engine root for an engine-only session.
	static FString ProjectArgument();
	bool Launch(const FString& Arguments) const;

	FString ExecutablePath;
};
