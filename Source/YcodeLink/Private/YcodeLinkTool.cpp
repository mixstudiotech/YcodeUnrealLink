// Copyright Pix Philosophy (HK) Limited.
// SPDX-License-Identifier: MIT

#include "YcodeLinkTool.h"

#include "Misc/Base64.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	TSharedRef<FJsonObject> MakeTextBlock(const FString& Text)
	{
		TSharedRef<FJsonObject> Block = MakeShared<FJsonObject>();
		Block->SetStringField(TEXT("type"), TEXT("text"));
		Block->SetStringField(TEXT("text"), Text);
		return Block;
	}

	TSharedRef<FJsonObject> MakeProperty(const TCHAR* Type, const TCHAR* Description)
	{
		TSharedRef<FJsonObject> Property = MakeShared<FJsonObject>();
		Property->SetStringField(TEXT("type"), Type);
		if (Description && *Description)
		{
			Property->SetStringField(TEXT("description"), Description);
		}
		return Property;
	}
}

// ---------------------------------------------------------------- results --

FYcodeLinkToolResult FYcodeLinkToolResult::Text(const FString& Text, bool bIsError)
{
	FYcodeLinkToolResult Result;
	Result.Json = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Content;
	Content.Add(MakeShared<FJsonValueObject>(MakeTextBlock(Text)));
	Result.Json->SetArrayField(TEXT("content"), Content);
	Result.Json->SetBoolField(TEXT("isError"), bIsError);
	return Result;
}

FYcodeLinkToolResult FYcodeLinkToolResult::Object(const TSharedRef<FJsonObject>& Structured, bool bIsError)
{
	FYcodeLinkToolResult Result = Text(YcodeLinkJson::ToCompactString(Structured), bIsError);
	Result.Json->SetObjectField(TEXT("structuredContent"), Structured);
	return Result;
}

FYcodeLinkToolResult FYcodeLinkToolResult::Error(const FString& Message)
{
	return Text(Message, /*bIsError=*/true);
}

FYcodeLinkToolResult& FYcodeLinkToolResult::WithImage(const TArray<uint8>& PngBytes)
{
	if (!Json.IsValid() || PngBytes.IsEmpty())
	{
		return *this;
	}
	TArray<TSharedPtr<FJsonValue>> Content = Json->GetArrayField(TEXT("content"));
	TSharedRef<FJsonObject> Block = MakeShared<FJsonObject>();
	Block->SetStringField(TEXT("type"), TEXT("image"));
	Block->SetStringField(TEXT("data"), FBase64::Encode(PngBytes));
	Block->SetStringField(TEXT("mimeType"), TEXT("image/png"));
	Content.Add(MakeShared<FJsonValueObject>(Block));
	Json->SetArrayField(TEXT("content"), Content);
	return *this;
}

// ----------------------------------------------------------------- schema --

FYcodeLinkSchema::FYcodeLinkSchema()
	: Properties(MakeShared<FJsonObject>())
{
}

FYcodeLinkSchema& FYcodeLinkSchema::Add(const TCHAR* Name, const TSharedRef<FJsonObject>& Property, bool bRequired)
{
	Properties->SetObjectField(Name, Property);
	if (bRequired)
	{
		Required.AddUnique(Name);
	}
	return *this;
}

FYcodeLinkSchema& FYcodeLinkSchema::String(const TCHAR* Name, const TCHAR* Description, bool bRequired)
{
	return Add(Name, MakeProperty(TEXT("string"), Description), bRequired);
}

FYcodeLinkSchema& FYcodeLinkSchema::Integer(const TCHAR* Name, const TCHAR* Description, bool bRequired)
{
	return Add(Name, MakeProperty(TEXT("integer"), Description), bRequired);
}

FYcodeLinkSchema& FYcodeLinkSchema::Number(const TCHAR* Name, const TCHAR* Description, bool bRequired)
{
	return Add(Name, MakeProperty(TEXT("number"), Description), bRequired);
}

FYcodeLinkSchema& FYcodeLinkSchema::Boolean(const TCHAR* Name, const TCHAR* Description, bool bRequired)
{
	return Add(Name, MakeProperty(TEXT("boolean"), Description), bRequired);
}

FYcodeLinkSchema& FYcodeLinkSchema::Enum(const TCHAR* Name, const TCHAR* Description, const TArray<FString>& Values, bool bRequired)
{
	TSharedRef<FJsonObject> Property = MakeProperty(TEXT("string"), Description);
	TArray<TSharedPtr<FJsonValue>> Enum;
	for (const FString& Value : Values)
	{
		Enum.Add(MakeShared<FJsonValueString>(Value));
	}
	Property->SetArrayField(TEXT("enum"), Enum);
	return Add(Name, Property, bRequired);
}

FYcodeLinkSchema& FYcodeLinkSchema::StringArray(const TCHAR* Name, const TCHAR* Description, bool bRequired)
{
	TSharedRef<FJsonObject> Property = MakeProperty(TEXT("array"), Description);
	Property->SetObjectField(TEXT("items"), MakeProperty(TEXT("string"), nullptr));
	return Add(Name, Property, bRequired);
}

FYcodeLinkSchema& FYcodeLinkSchema::NumberObject(const TCHAR* Name, const TCHAR* Description, const TArray<FString>& Fields, bool bRequired)
{
	TSharedRef<FJsonObject> Property = MakeProperty(TEXT("object"), Description);
	TSharedRef<FJsonObject> Nested = MakeShared<FJsonObject>();
	for (const FString& Field : Fields)
	{
		Nested->SetObjectField(Field, MakeProperty(TEXT("number"), nullptr));
	}
	Property->SetObjectField(TEXT("properties"), Nested);
	return Add(Name, Property, bRequired);
}

TSharedRef<FJsonObject> FYcodeLinkSchema::Build() const
{
	TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	Schema->SetObjectField(TEXT("properties"), Properties);
	if (!Required.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> RequiredValues;
		for (const FString& Name : Required)
		{
			RequiredValues.Add(MakeShared<FJsonValueString>(Name));
		}
		Schema->SetArrayField(TEXT("required"), RequiredValues);
	}
	return Schema;
}

// ------------------------------------------------------------------- json --

namespace YcodeLinkJson
{
	FString GetString(const TSharedRef<FJsonObject>& Object, const TCHAR* Field, const FString& Default)
	{
		FString Value;
		return Object->TryGetStringField(Field, Value) ? Value : Default;
	}

	int32 GetInt(const TSharedRef<FJsonObject>& Object, const TCHAR* Field, int32 Default)
	{
		int32 Value = 0;
		return Object->TryGetNumberField(Field, Value) ? Value : Default;
	}

	double GetNumber(const TSharedRef<FJsonObject>& Object, const TCHAR* Field, double Default)
	{
		double Value = 0.0;
		return Object->TryGetNumberField(Field, Value) ? Value : Default;
	}

	bool GetBool(const TSharedRef<FJsonObject>& Object, const TCHAR* Field, bool Default)
	{
		bool Value = false;
		return Object->TryGetBoolField(Field, Value) ? Value : Default;
	}

	TSharedPtr<FJsonObject> GetObject(const TSharedRef<FJsonObject>& Object, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Value = nullptr;
		return Object->TryGetObjectField(Field, Value) && Value ? *Value : nullptr;
	}

	TArray<FString> GetStringArray(const TSharedRef<FJsonObject>& Object, const TCHAR* Field)
	{
		TArray<FString> Values;
		Object->TryGetStringArrayField(Field, Values);
		return Values;
	}

	FString ToCompactString(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	TSharedPtr<FJsonObject> Parse(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		return FJsonSerializer::Deserialize(Reader, Object) && Object.IsValid() ? Object : nullptr;
	}

	TArray<uint8> ToUtf8(const TSharedRef<FJsonObject>& Object)
	{
		const FString Text = ToCompactString(Object);
		const FTCHARToUTF8 Converted(*Text, Text.Len());
		TArray<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
		return Bytes;
	}
}
