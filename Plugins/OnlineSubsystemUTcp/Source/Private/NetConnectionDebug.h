#pragma once
#include "CoreMinimal.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include <thread>
#include <mutex>
#include <map>
#include <list>
#include <unordered_map>

class UNetConnection;
class FOutBunch;
class FInBunch;
class AActor;
class FNetConnectionDebug
{
public:
	FNetConnectionDebug();
	~FNetConnectionDebug();

	void Init(bool bInEnable, bool bInVerbose);

	void OnSend(const UNetConnection* InConn, const FOutBunch* InBunch, int32 InPacketId);
	void OnRecv(const UNetConnection* InConn, const FInBunch* InBunch);

private:
	struct FWriteInfo
	{
		FString ConnName;

		uint32 CRC;
		int32 NumBits;

		int32 ChIndex;
		int32 PacketId;

		uint8 bOpen : 1;
		uint8 bClose : 1;
		uint8 bReliable : 1;
		uint8 bPartial : 1;
		uint8 bPartialInitial : 1;
		uint8 bPartialFinal : 1;
		uint8 bHasPackageMapExports : 1;

		uint8 bSendBunch : 1;
		uint8 bIsServer : 1;

		uint64 StackTrace[16];
	};

	void ThreadFunction();
	void WriteInfo(FWriteInfo* InInfo);
	void WriteStackInfo(FWriteInfo* InInfo, IFileHandle* InFileHandle);
	void CacheInfo(FWriteInfo* InInfo);
	IFileHandle* GetOrOpenFile(const FString& InConnName, bool bInSend, bool bIsServer);

	TAnsiStringBuilder<1024 * 1024> StringBuilder;
	std::list<FWriteInfo*> WriteList;
	std::mutex WriteListMutex;
	std::thread WriteThread;
	std::condition_variable WriteThreadNotify;
	std::map<FString, IFileHandle*> ConnName2FD;
	std::unordered_map<uint64, std::string> Addr2Line;
	std::atomic<int32> bThreadExit;
	bool bVerbose;
	bool bEnable;
};
