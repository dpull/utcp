#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UTcpNetConnection.h"
#include "IpNetDriver.h"
#include "NetConnectionDebug.h"
#include "abstract/utcp.hpp"
#include "UTcpNetDriver.generated.h"

UCLASS(transient, config = OnlineSubsystemUTcp)
class UUTcpNetDriver : public UIpNetDriver, public utcp::listener
{
	GENERATED_BODY()

public:
	virtual bool InitBase(bool bInitAsClient, FNetworkNotify* InNotify, const FURL& URL, bool bReuseAddressAndPort, FString& Error) override;
	virtual void InitConnectionlessHandler() override;
	virtual void TickDispatch(float DeltaTime) override;
	virtual void LowLevelSend(TSharedPtr<const FInternetAddr> Address, void* Data, int32 CountBits, FOutPacketTraits& Traits) override;
	virtual void Shutdown() override;
	virtual void PostTickDispatch() override;

	FNetConnectionDebug* GetNetConnectionDebug() { return &NetConnectionDebug; }

protected:
	virtual void on_accept(bool reconnect) override;
	virtual void on_outgoing(const void* data, int len) override;

private:
	FNetConnectionDebug NetConnectionDebug;
	TSharedPtr<FInternetAddr> LastRecvAddress;
	TArray<uint8, TFixedAllocator<MAX_PACKET_SIZE>> LastRecvData;
};

inline void DumpBin(const TCHAR* InPrefix, const void* InData, int32 InDataLen)
{
	FString Str;

	for (auto i = 0; i < InDataLen; ++i)
	{
		if (i != 0)
		{
			Str.Append(TEXT(", "));
		}

		Str.Appendf(TEXT("0x%hhX"), ((const uint8_t*)InData)[i]);
	}
	UE_LOG(LogUTcp, Log, TEXT("[DUMP]%s\t%d\t{%s}"), InPrefix, InDataLen, *Str);
}
