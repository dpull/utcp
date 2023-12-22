#pragma once

#include "CoreMinimal.h"
#include "IpConnection.h"
#include "abstract/utcp.hpp"
#include "Engine/ControlChannel.h"
#include "Engine/ActorChannel.h"
#include "Engine/NetConnection.h"
#include "UTcpNetConnection.generated.h"

UCLASS(transient, config = OnlineSubsystemUTcp)
class UUTcpConnection : public UIpConnection, public utcp::conn
{
public:
	GENERATED_BODY()

	virtual void InitSequence(int32 IncomingSequence, int32 OutgoingSequence) override;
	virtual void InitRemoteConnection(UNetDriver* InDriver, class FSocket* InSocket, const FURL& InURL, const class FInternetAddr& InRemoteAddr, EConnectionState InState, int32 InMaxPacket = 0, int32 InPacketOverhead = 0) override;

	virtual void ReceivedRawPacket(void* Data, int32 Count) override;

	virtual void Tick(float DeltaSeconds) override;
	virtual void FlushNet(bool bIgnoreSimulation) override;
	virtual void LowLevelSend(void* Data, int32 CountBits, FOutPacketTraits& Traits) override;

public:
	int32 InternalSendRawBunch(FOutBunch* InBunch, bool Merge);
	void InternalPostTickDispatch();

protected:
	virtual void on_disconnect(int close_reason) override;
	virtual void on_outgoing(const void* data, int len) override;
	virtual void on_recv_bunch(struct utcp_bunch* const bunches[], int count) override;
	virtual void on_delivery_status(int32_t packet_id, bool ack) override;

private:
	void InternalAck(int32 AckPacketId, FChannelsToClose& OutChannelsToClose);
	void InternalNak(int32 NakPacketId);
};

class UUTcpChannel : public UChannel
{
public:
	void UTcpTick();
	FPacketIdRange UTcpSendBunch(FOutBunch* Bunch, bool Merge);
	void UTcpOnRecv(FInBunch& Bunch);

private:
	FOutBunch* UTcpPrepBunch(FOutBunch* Bunch, FOutBunch* OutBunch, bool Merge);
	int32 UTcpSendRawBunch(FOutBunch* OutBunch, bool Merge);
};
static_assert(sizeof(UUTcpChannel) == sizeof(UChannel));

UCLASS(transient, customConstructor)
class UUTcpControlChannel : public UControlChannel
{
	GENERATED_UCLASS_BODY()

	UUTcpControlChannel(const FObjectInitializer& ObjectInitializer)
		: UControlChannel(ObjectInitializer)
	{
	}

	virtual FPacketIdRange SendBunch(FOutBunch* Bunch, bool Merge) override
	{
		return ((UUTcpChannel*)this)->UTcpSendBunch(Bunch, Merge);
	}

	virtual void ReceivedBunch(FInBunch& Bunch)
	{
		((UUTcpChannel*)this)->UTcpOnRecv(Bunch);
		Super::ReceivedBunch(Bunch);
	}
};

UCLASS(transient, customConstructor)
class UUTcpActorChannel : public UActorChannel
{
	GENERATED_UCLASS_BODY()

	UUTcpActorChannel(const FObjectInitializer& ObjectInitializer)
		: UActorChannel(ObjectInitializer)
	{
	}

	virtual void Tick() override
	{
		Super::Tick();
		if (Connection->Driver->IsServer())
			((UUTcpChannel*)this)->UTcpTick();
	}

	virtual FPacketIdRange SendBunch(FOutBunch* Bunch, bool Merge) override
	{
		return ((UUTcpChannel*)this)->UTcpSendBunch(Bunch, Merge);
	}

	virtual void ReceivedBunch(FInBunch& Bunch)
	{
		((UUTcpChannel*)this)->UTcpOnRecv(Bunch);
		Super::ReceivedBunch(Bunch);
	}
};
