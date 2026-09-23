// Copyright Pix Philosophy (HK) Limited.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

// A tool result in MCP `CallToolResult` shape: `content` (text / image blocks),
// optional `structuredContent`, and `isError`. Built through the factories so
// every module produces the same wire shape.
struct YCODELINK_API FYcodeLinkToolResult
{
	TSharedPtr<FJsonObject> Json;

	static FYcodeLinkToolResult Text(const FString& Text, bool bIsError = false);
	// Text content carries the compact JSON so text-only clients still get the
	// data; `structuredContent` carries the object itself.
	static FYcodeLinkToolResult Object(const TSharedRef<FJsonObject>& Structured, bool bIsError = false);
	static FYcodeLinkToolResult Error(const FString& Message);

	// Appends an image block (base64 PNG) to an existing result.
	FYcodeLinkToolResult& WithImage(const TArray<uint8>& PngBytes);
};

// Tools complete through this callback exactly once. It may be invoked from
// any thread; the server hops to the game thread before writing the response.
using FYcodeLinkToolCallback = TFunction<void(const FYcodeLinkToolResult&)>;
// Handlers run on the game thread. Arguments are the MCP `arguments` object
// (never null; missing fields simply are not present).
using FYcodeLinkToolHandler = TFunction<void(const TSharedRef<FJsonObject>& Args, const FYcodeLinkToolCallback& OnComplete)>;

// Small builder for a JSON Schema `{"type":"object","properties":...}`, which
// is what every MCP tool must publish as its `inputSchema`.
class YCODELINK_API FYcodeLinkSchema
{
public:
	FYcodeLinkSchema();

	FYcodeLinkSchema& String(const TCHAR* Name, const TCHAR* Description, bool bRequired = false);
	FYcodeLinkSchema& Integer(const TCHAR* Name, const TCHAR* Description, bool bRequired = false);
	FYcodeLinkSchema& Number(const TCHAR* Name, const TCHAR* Description, bool bRequired = false);
	FYcodeLinkSchema& Boolean(const TCHAR* Name, const TCHAR* Description, bool bRequired = false);
	FYcodeLinkSchema& Enum(const TCHAR* Name, const TCHAR* Description, const TArray<FString>& Values, bool bRequired = false);
	FYcodeLinkSchema& StringArray(const TCHAR* Name, const TCHAR* Description, bool bRequired = false);
	// Nested object with numeric fields, e.g. a vector {x,y,z}.
	FYcodeLinkSchema& NumberObject(const TCHAR* Name, const TCHAR* Description, const TArray<FString>& Fields, bool bRequired = false);

	TSharedRef<FJsonObject> Build() const;

private:
	FYcodeLinkSchema& Add(const TCHAR* Name, const TSharedRef<FJsonObject>& Property, bool bRequired);

	TSharedRef<FJsonObject> Properties;
	TArray<FString> Required;
};

// One registered MCP tool. `bReadOnly` becomes the standard
// `annotations.readOnlyHint`, which the Ycode agent uses to skip approval.
struct YCODELINK_API FYcodeLinkTool
{
	FString Name;
	FString Description;
	TSharedPtr<FJsonObject> InputSchema;
	bool bReadOnly = false;
	FYcodeLinkToolHandler Handler;
};

// JSON conveniences shared by the tool implementations.
namespace YcodeLinkJson
{
	YCODELINK_API FString GetString(const TSharedRef<FJsonObject>& Object, const TCHAR* Field, const FString& Default = FString());
	YCODELINK_API int32 GetInt(const TSharedRef<FJsonObject>& Object, const TCHAR* Field, int32 Default);
	YCODELINK_API double GetNumber(const TSharedRef<FJsonObject>& Object, const TCHAR* Field, double Default);
	YCODELINK_API bool GetBool(const TSharedRef<FJsonObject>& Object, const TCHAR* Field, bool Default);
	YCODELINK_API TSharedPtr<FJsonObject> GetObject(const TSharedRef<FJsonObject>& Object, const TCHAR* Field);
	YCODELINK_API TArray<FString> GetStringArray(const TSharedRef<FJsonObject>& Object, const TCHAR* Field);

	YCODELINK_API FString ToCompactString(const TSharedRef<FJsonObject>& Object);
	YCODELINK_API TSharedPtr<FJsonObject> Parse(const FString& Text);
	// UTF-8 bytes of the compact serialization (what the HTTP layer writes).
	YCODELINK_API TArray<uint8> ToUtf8(const TSharedRef<FJsonObject>& Object);
}
