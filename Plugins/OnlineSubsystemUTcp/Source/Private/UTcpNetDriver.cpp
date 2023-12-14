#include "UTcpNetDriver.h"
#include "EngineUtils.h"
#include "Sockets.h"
#include "Net/RepLayout.h"
#include "GameFramework/PlayerController.h"
#include "PacketHandlers/StatelessConnectHandlerComponent.h"
#include "Modules/ModuleManager.h"

class FOnlineSubsystemUTcpModule : public IModuleInterface
{
};

IMPLEMENT_MODULE(FOnlineSubsystemUTcpModule, OnlineSubsystemUTcp);
DEFINE_LOG_CATEGORY(LogUTcp);

static void LogWrapper(int InLevel, const char* InFormat, va_list InArgs)
{
	char TempString[1024];
	FCStringAnsi::GetVarArgs(TempString, UE_ARRAY_COUNT(TempString), InFormat, InArgs);
	UE_LOG(LogUTcp, Log, TEXT("[UTCP]%s"), UTF8_TO_TCHAR(TempString));
}

bool UUTcpNetDriver::InitBase(bool bInitAsClient, FNetworkNotify* InNotify, const FURL& URL, bool bReuseAddressAndPort,
	FString& Error)
{
	NetConnectionDebug.Init(true, false);
	return Super::InitBase(bInitAsClient, InNotify, URL, bReuseAddressAndPort, Error);
}

void UUTcpNetDriver::InitConnectionlessHandler()
{
	check(!ConnectionlessHandler.IsValid());
	auto CVarNetIpNetDriverUseReceiveThread = IConsoleManager::Get().FindConsoleVariable(TEXT("net.IpNetDriverUseReceiveThread"));
	check(CVarNetIpNetDriverUseReceiveThread->GetInt() == 0);

	config(LogWrapper);
	enbale_dump_data(true);
}

void UUTcpNetDriver::TickDispatch(float DeltaTime)
{
	if (!IsServer())
	{
		Super::TickDispatch(DeltaTime);
		return;
	}

	if (!LastRecvAddress.IsValid())
		LastRecvAddress = GetSocketSubsystem()->CreateInternetAddr();

	while (true)
	{
		int32 BytesRead = 0;
		bool bReceivedPacket = GetSocket()->RecvFrom(LastRecvData.GetData(), MAX_PACKET_SIZE, BytesRead, *LastRecvAddress);
		if (!bReceivedPacket)
			return;

		UNetConnection** Result = MappedClientConnections.Find(LastRecvAddress.ToSharedRef());
		if (Result == nullptr || !IsValid(*Result))
		{
			auto Str = LastRecvAddress->ToString(false);
			incoming(TCHAR_TO_UTF8(*Str), LastRecvData.GetData(), BytesRead);
			return;
		}

		auto Connection = Cast<UUTcpConnection>(*Result);
		check(Connection);
		Connection->ReceivedRawPacket(LastRecvData.GetData(), BytesRead);
	}
}

void UUTcpNetDriver::LowLevelSend(TSharedPtr<const FInternetAddr> Address, void* Data, int32 CountBits, FOutPacketTraits& Traits)
{
	if (IsServer())
		check(false);
	Super::LowLevelSend(Address, Data, CountBits, Traits);
}

void UUTcpNetDriver::Shutdown()
{
	Super::Shutdown();
}

void UUTcpNetDriver::PostTickDispatch()
{
	Super::PostTickDispatch();

	TArray<UNetConnection*> ClientConnCopy = ClientConnections;
	for (auto CurConn : ClientConnCopy)
	{
		if (IsValid(CurConn))
		{
			Cast<UUTcpConnection>(CurConn)->InternalPostTickDispatch();
		}
	}
}

void UUTcpNetDriver::on_accept(bool reconnect)
{
	UIpConnection* ReturnVal = nullptr;
	const TSharedPtr<const FInternetAddr>& Address = LastRecvAddress;

	if (reconnect)
	{
		UIpConnection* FoundConn = nullptr;
		for (UNetConnection* CurConn : ClientConnections)
		{
			auto Conn = Cast<UUTcpConnection>(CurConn);
			if (does_restarted_handshake_match(Conn))
			{
				FoundConn = Cast<UIpConnection>(CurConn);
				break;
			}
		}

		if (FoundConn != nullptr)
		{
			UNetConnection* RemovedConn = nullptr;
			TSharedRef<FInternetAddr> RemoteAddrRef = FoundConn->RemoteAddr.ToSharedRef();

			verify(MappedClientConnections.RemoveAndCopyValue(RemoteAddrRef, RemovedConn) && RemovedConn == FoundConn);

			// @todo: There needs to be a proper/standardized copy API for this. Also in IpConnection.cpp
			bool bIsValid = false;
			const FString OldAddress = RemoteAddrRef->ToString(true);
			RemoteAddrRef->SetIp(*Address->ToString(false), bIsValid);
			RemoteAddrRef->SetPort(Address->GetPort());

			MappedClientConnections.Add(RemoteAddrRef, FoundConn);
			ReturnVal = FoundConn;

			// We shouldn't need to log IncomingAddress, as the UNetConnection should dump it with it's description.
			UE_LOG(LogNet, Log, TEXT("Updated IP address for connection. Connection = %s, Old Address = %s"), *FoundConn->Describe(), *OldAddress);
		}
		else
		{
			UE_LOG(LogUTcp, Log, TEXT("Failed to find an existing connection with a matching cookie. Restarted Handshake failed."));
		}
	}
	else
	{
		UE_LOG(LogNet, Log, TEXT("Server accepting post-challenge connection from: %s"), *Address->ToString(true));

		ReturnVal = NewObject<UIpConnection>(GetTransientPackage(), NetConnectionClass);
		check(ReturnVal != nullptr);
		ReturnVal->InitRemoteConnection(this, GetSocket(), World ? World->URL : FURL(), *Address, USOCK_Open);
		AddClientConnection(ReturnVal);
	}

	if (ReturnVal)
	{
		auto UTcpConn = Cast<UUTcpConnection>(ReturnVal);
		UTcpConn->set_debug_name(TCHAR_TO_UTF8(*UTcpConn->GetName()));
		accept(UTcpConn, reconnect);
	}
	else
	{
		// UE_LOG(LogUTcp, Log, TEXT(""));
	}
}

void UUTcpNetDriver::on_outgoing(const void* data, int len)
{
	int32 BytesSent = 0;
	GetSocket()->SendTo((const uint8*)data, len, BytesSent, *LastRecvAddress);
}
