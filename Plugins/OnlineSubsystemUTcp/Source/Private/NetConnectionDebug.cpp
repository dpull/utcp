#include "NetConnectionDebug.h"
#include "Net/DataBunch.h"
#include "Misc/Paths.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformStackWalk.h"

constexpr int IGNORE_STACK_COUNT = 5;

FNetConnectionDebug::FNetConnectionDebug()
{
	bThreadExit.store(0);
	bEnable = false;
	bVerbose = false;
	WriteThread = std::thread([this]() {
		this->ThreadFunction();
	});
}

FNetConnectionDebug::~FNetConnectionDebug()
{
	bThreadExit.store(1);
	WriteThreadNotify.notify_one();
	WriteThread.join();
	for (auto Item : WriteList)
	{
		delete Item;
	}
	WriteList.clear();

	for (auto& Item : ConnName2FD)
	{
		if (Item.second)
			delete Item.second;
	}
	ConnName2FD.clear();
}

void FNetConnectionDebug::Init(bool bInEnable, bool bInVerbose)
{
	bEnable = bInEnable;
	bVerbose = bInVerbose;
}

void FNetConnectionDebug::OnSend(const UNetConnection* InConn, const FOutBunch* InBunch, int32 InPacketId)
{
	if (!bEnable)
		return;

	if (!IsValid(InConn))
		return;

	auto Info = new FWriteInfo;
	auto Driver = InConn->GetDriver();
	Info->bIsServer = Driver->IsServer();
	Info->bSendBunch = true;
	Info->ConnName = InConn->GetName();

	Info->NumBits = InBunch->GetNumBits();
	Info->ChIndex = InBunch->ChIndex;
	Info->PacketId = InPacketId;
	Info->bOpen = InBunch->bOpen;
	Info->bClose = InBunch->bClose;
	Info->bReliable = InBunch->bReliable;
	Info->bPartial = InBunch->bPartial;
	Info->bPartialInitial = InBunch->bPartialInitial;
	Info->bPartialFinal = InBunch->bPartialFinal;
	Info->bHasPackageMapExports = InBunch->bHasPackageMapExports;

	if (bVerbose)
	{
		Info->CRC = FCrc::MemCrc32(InBunch->GetData(), InBunch->GetNumBytes());
		FPlatformStackWalk::CaptureStackBackTrace(Info->StackTrace, std::size(Info->StackTrace));
	}
	CacheInfo(Info);
}

void FNetConnectionDebug::OnRecv(const UNetConnection* InConn, const FInBunch* InBunch)
{
	if (!bEnable)
		return;

	if (!IsValid(InConn))
		return;

	auto Info = new FWriteInfo;
	auto Driver = InConn->GetDriver();
	Info->bIsServer = Driver->IsServer();
	Info->bSendBunch = false;
	Info->ConnName = InConn->GetName();

	Info->NumBits = InBunch->GetNumBits();
	Info->ChIndex = InBunch->ChIndex;
	Info->PacketId = InBunch->PacketId;
	Info->bOpen = InBunch->bOpen;
	Info->bClose = InBunch->bClose;
	Info->bReliable = InBunch->bReliable;
	Info->bPartial = InBunch->bPartial;
	Info->bPartialInitial = InBunch->bPartialInitial;
	Info->bPartialFinal = InBunch->bPartialFinal;
	Info->bHasPackageMapExports = InBunch->bHasPackageMapExports;

	if (bVerbose)
	{
		Info->CRC = FCrc::MemCrc32(InBunch->GetData(), InBunch->GetNumBytes());
	}

	CacheInfo(Info);
}

void FNetConnectionDebug::ThreadFunction()
{
	while (bThreadExit.load() == 0)
	{
		FWriteInfo* Info = nullptr;

		{
			std::lock_guard<decltype(WriteListMutex)> Lock(WriteListMutex);
			if (!WriteList.empty())
			{
				Info = WriteList.front();
				WriteList.pop_front();
			}
		}

		if (Info)
		{
			WriteInfo(Info);
			delete Info;
		}
		else
		{
			std::unique_lock<decltype(WriteListMutex)> Lock(WriteListMutex);
			WriteThreadNotify.wait(Lock);
		}
	}
}

void FNetConnectionDebug::WriteInfo(FWriteInfo* InInfo)
{
	auto FileHandle = GetOrOpenFile(InInfo->ConnName, !!InInfo->bSendBunch, !!InInfo->bIsServer);
	if (!FileHandle)
		return;

	StringBuilder.Reset();
	StringBuilder.Appendf(
		"ChIndex:%d, PacketId:%d, NumBits:%d, Open:%d, Close:%d, Reliable:%d, Partial:%d[%d, %d], HasPackageMapExports:%d\n",
		InInfo->ChIndex,
		InInfo->PacketId,
		InInfo->NumBits,
		InInfo->bOpen,
		InInfo->bClose,
		InInfo->bReliable,
		InInfo->bPartial, InInfo->bPartialInitial, InInfo->bPartialFinal,
		InInfo->bHasPackageMapExports);

	if (bVerbose)
	{
		StringBuilder.Appendf("\tCRC:%u\n", InInfo->CRC);
	}

	FileHandle->Write((uint8*)StringBuilder.GetData(), StringBuilder.Len());
	WriteStackInfo(InInfo, FileHandle);
}

void FNetConnectionDebug::WriteStackInfo(FWriteInfo* InInfo, IFileHandle* InFileHandle)
{
	if (!InInfo->bSendBunch || !bVerbose)
		return;

	StringBuilder.Reset();
	for (int i = IGNORE_STACK_COUNT; i < std::size(InInfo->StackTrace); ++i)
	{
		auto Addr = InInfo->StackTrace[i];
		if (!Addr)
			break;

		auto it = Addr2Line.find(Addr);
		if (it == Addr2Line.end())
		{
			FProgramCounterSymbolInfo SymbolInfo;
#if PLATFORM_WINDOWS
			__try
			{
#endif
				FPlatformStackWalk::ProgramCounterToSymbolInfo(InInfo->StackTrace[i], SymbolInfo);
#if PLATFORM_WINDOWS
			}
			__except (true)
			{
			}
#endif
			char Buffer[4096];
			snprintf(Buffer, sizeof(Buffer), "\t%s [%s:%d]\n", SymbolInfo.FunctionName, SymbolInfo.Filename, SymbolInfo.LineNumber);
			Buffer[sizeof(Buffer) - 1] = '\0';
			auto itInsert = Addr2Line.insert(std::make_pair(Addr, Buffer));
			it = itInsert.first;
		}
		StringBuilder.Append(it->second.c_str(), it->second.size());
	}
	InFileHandle->Write((uint8*)StringBuilder.GetData(), StringBuilder.Len());
}

void FNetConnectionDebug::CacheInfo(FWriteInfo* InInfo)
{
	{
		std::lock_guard<decltype(WriteListMutex)> Lock(WriteListMutex);
		WriteList.push_back(InInfo);
	}
	WriteThreadNotify.notify_one();
}

IFileHandle* FNetConnectionDebug::GetOrOpenFile(const FString& InConnName, bool bInSend, bool bIsServer)
{
	FString FileName = InConnName;
	FileName.Append(bIsServer ? TEXT("_S_") : TEXT("_C_"));
	FileName.Append(bInSend ? TEXT("Send.txt") : TEXT("Recv.txt"));

	auto it = ConnName2FD.insert(std::make_pair(FileName, nullptr));
	if (it.second)
	{
		auto FilePath = FPaths::Combine(FPaths::ProjectSavedDir(), FileName);
		auto& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		while (PlatformFile.FileExists(*FilePath))
		{
			FilePath.Append(".txt");
		}
		auto File = PlatformFile.OpenWrite(*FilePath, false, false);
		it.first->second = File;
	}

	ensure(it.first->second);
	return it.first->second;
}