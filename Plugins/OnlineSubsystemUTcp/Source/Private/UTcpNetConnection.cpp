#include "UTcpNetConnection.h"

#include "BunchConvert.h"
#include "Containers/Array.h"
#include "Net/Core/Misc/PacketAudit.h"
#include "UTcpNetDriver.h"
#include "Engine/PackageMapClient.h"
#include "Sockets.h"
#include "Net/DataChannel.h"

void UUTcpConnection::InitSequence(int32 IncomingSequence, int32 OutgoingSequence)
{
	if (Driver->IsServer())
		assert(false);
	Super::InitSequence(IncomingSequence, OutgoingSequence);
}

void UUTcpConnection::InitRemoteConnection(UNetDriver* InDriver, FSocket* InSocket, const FURL& InURL,
	const FInternetAddr& InRemoteAddr, EConnectionState InState, int32 InMaxPacket, int32 InPacketOverhead)
{
	Super::InitRemoteConnection(InDriver, InSocket, InURL, InRemoteAddr, InState, InMaxPacket, InPacketOverhead);
}

void UUTcpConnection::ReceivedRawPacket(void* Data, int32 Count)
{
	if (Driver->IsServer())
	{
		incoming((uint8_t*)Data, Count);
		return;
	}
	DumpBin(*(GetName() + TEXT("\tRecv")), Data, Count);
	Super::ReceivedRawPacket(Data, Count);
}

void UUTcpConnection::Tick(float DeltaSeconds)
{
	if (Driver->IsServer())
		update();
	Super::Tick(DeltaSeconds);
}

void UUTcpConnection::FlushNet(bool bIgnoreSimulation)
{
	if (Driver->IsServer())
	{
		send_flush();
		return;
	}
	Super::FlushNet(bIgnoreSimulation);
	UE_LOG(LogUTcp, Display, TEXT("%s\tFlushNet OutPacketId=%d"), *GetName(), OutPacketId - 1);
}

void UUTcpConnection::LowLevelSend(void* Data, int32 CountBits, FOutPacketTraits& Traits)
{
	if (Driver->IsServer())
		assert(false);
	Super::LowLevelSend(Data, CountBits, Traits);
	UE_LOG(LogUTcp, Display, TEXT("%s\tLowLevelSend OutPacketId=%d"), *GetName(), OutPacketId);
	DumpBin(*(GetName() + TEXT("\tSend")), Data, FMath::DivideAndRoundUp(CountBits, 8));
}

int32 UUTcpConnection::InternalSendRawBunch(FOutBunch* InBunch, bool Merge)
{
	int32 PacketId;
	if (Driver->IsServer())
	{
		check(SendBuffer.GetNumBits() == 0);
		TimeSensitive = true;

		utcp_bunch Bunch;
		FConvert::To(InBunch, &Bunch);
		PacketId = utcp_send_bunch(get_fd(), &Bunch);
	}
	else
	{
		PacketId = SendRawBunch(*InBunch, Merge);
	}

	CastChecked<UUTcpNetDriver>(Driver)->GetNetConnectionDebug()->OnSend(this, InBunch, PacketId);
	return PacketId;
}

void UUTcpConnection::InternalPostTickDispatch()
{
	if (!Driver->IsServer())
		assert(false);
	flush_incoming_cache();
}

void UUTcpConnection::on_disconnect(int close_reason)
{
	conn::on_disconnect(close_reason);
}

void UUTcpConnection::on_outgoing(const void* data, int len)
{
	int32 BytesSent = 0;
	GetSocket()->SendTo((const uint8*)data, len, BytesSent, *RemoteAddr);
	TimeSensitive = false;
}

void UUTcpConnection::on_recv_bunch(utcp_bunch* const bunches[], int count)
{
	auto Bunch = bunches[0];
	auto Channel = Channels[Bunch->ChIndex];
	if (!Channel)
	{
		ensure(Bunch->bOpen);
		auto ChName = EName(Bunch->NameIndex);
		Channel = CreateChannelByName(ChName, EChannelCreateFlags::None, Bunch->ChIndex);
	}

	for (auto i = 0; i < count; ++i)
	{
		FInBunch InBunch(this);
		FConvert::To(bunches[i], &InBunch);
		Channel->ReceivedBunch(InBunch);

		// 保证 HasReceivedClientPacket 返回值正确
		if (InBunch.bReliable)
			InReliable[InBunch.ChIndex] = InBunch.ChSequence;
	}
}

void UUTcpConnection::on_delivery_status(int32_t packet_id, bool ack)
{
	UE_LOG(LogUTcp, Display, TEXT("[%s]OnDeliveryStatus:(%d, %s)"), *GetName(), packet_id, ack ? TEXT("ACK") : TEXT("NAK"));
	if (ack)
	{
		FChannelsToClose ChannelsToClose;
		InternalAck(packet_id, ChannelsToClose);
	}
	else
	{
		InternalNak(packet_id);
	}
}

void UUTcpConnection::InternalAck(int32 AckPacketId, FChannelsToClose& OutChannelsToClose)
{
	UE_LOG(LogNetTraffic, Verbose, TEXT("   Received ack %i"), AckPacketId);

	// Advance OutAckPacketId
	OutAckPacketId = AckPacketId;

	if (PackageMap != NULL)
	{
		PackageMap->ReceivedAck(AckPacketId);
	}

	auto AckChannelFunc = [this, &OutChannelsToClose](int32 AckedPacketId, uint32 ChannelIndex) {
		UChannel* const Channel = Channels[ChannelIndex];

		if (Channel)
		{
			if (Channel->OpenPacketId.Last == AckedPacketId) // Necessary for unreliable "bNetTemporary" channels.
			{
				Channel->OpenAcked = 1;
			}

			for (FOutBunch* OutBunch = Channel->OutRec; OutBunch; OutBunch = OutBunch->Next)
			{
				ensure(false);
			}
			Channel->ReceivedAck(AckedPacketId);
			EChannelCloseReason CloseReason;
			if (Channel->ReceivedAcks(CloseReason))
			{
				const FChannelCloseInfo Info = { ChannelIndex, CloseReason };
				OutChannelsToClose.Emplace(Info);
			}
		}
	};

	// TODO 可以优化数量
	for (auto It = ActorChannelConstIterator(); It; ++It)
	{
		AckChannelFunc(AckPacketId, It->Value->ChIndex);
	}
}

void UUTcpConnection::InternalNak(int32 NakPacketId)
{
	UE_LOG(LogNetTraffic, Verbose, TEXT("   Received nak %i"), NakPacketId);

	// Update pending NetGUIDs
	PackageMap->ReceivedNak(NakPacketId);

	auto NakChannelFunc = [this](int32 NackedPacketId, uint32 ChannelIndex) {
		UChannel* const Channel = Channels[ChannelIndex];
		if (Channel)
		{
			Channel->ReceivedNak(NackedPacketId);
			if (Channel->OpenPacketId.InRange(NackedPacketId))
			{
				Channel->ReceivedAcks(); // warning: May destroy Channel.
			}
		}
	};

	// TODO 可以优化数量
	for (auto It = ActorChannelConstIterator(); It; ++It)
	{
		NakChannelFunc(NakPacketId, It->Value->ChIndex);
	}
}

void UUTcpChannel::UTcpTick()
{
	UE_LOG(LogUTcp, Log, TEXT("[%s]RAW Channel %d InRec:%d, OutRec:%d"), *GetName(), ChIndex, NumInRec, NumOutRec);
}

static int32 NetMaxConstructedPartialBunchSizeBytes = 1024 * 64;
template <typename T>
static const bool IsBunchTooLarge(UNetConnection* Connection, T* Bunch)
{
	return !Connection->IsUnlimitedBunchSizeAllowed() && Bunch != nullptr && Bunch->GetNumBytes() > NetMaxConstructedPartialBunchSizeBytes;
}

FOutBunch* UUTcpChannel::UTcpPrepBunch(FOutBunch* Bunch, FOutBunch* OutBunch, bool Merge)
{
	if (Connection->ResendAllDataState != EResendAllDataState::None)
	{
		return Bunch;
	}

	// Find outgoing bunch index.
	if (Bunch->bReliable)
	{
		// Find spot, which was guaranteed available by FOutBunch constructor.
		if (OutBunch == NULL)
		{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if (!(NumOutRec < RELIABLE_BUFFER - 1 + Bunch->bClose))
			{
				UE_LOG(LogNetTraffic, Warning, TEXT("PrepBunch: Reliable buffer overflow! %s"), *Describe());
				PrintReliableBunchBuffer();
			}
#else
			check(NumOutRec < RELIABLE_BUFFER - 1 + Bunch->bClose);
#endif

			Bunch->Next = NULL;
			Bunch->ChSequence = ++Connection->OutReliable[ChIndex];

			if (!Connection->Driver->IsServer())
			{
				NumOutRec++;
				OutBunch = new FOutBunch(*Bunch);
				FOutBunch** OutLink = &OutRec;
				while (*OutLink) // This was rewritten from a single-line for loop due to compiler complaining about empty body for loops (-Wempty-body)
				{
					OutLink = &(*OutLink)->Next;
				}
				*OutLink = OutBunch;
			}
			else
			{
				OutBunch = Bunch;
			}
		}
		else
		{
			Bunch->Next = OutBunch->Next;
			*OutBunch = *Bunch;
		}
		Connection->LastOutBunch = OutBunch;
	}
	else
	{
		OutBunch = Bunch;
		Connection->LastOutBunch = NULL; // warning: Complex code, don't mess with this!
	}

	return OutBunch;
}

FPacketIdRange UUTcpChannel::UTcpSendBunch(FOutBunch* Bunch, bool Merge)
{
	LLM_SCOPE_BYTAG(NetChannel);

	if (!ensure(ChIndex != -1))
	{
		// Client "closing" but still processing bunches. Client->Server RPCs should avoid calling this, but perhaps more code needs to check this condition.
		return FPacketIdRange(INDEX_NONE);
	}

	if (!ensureMsgf(!IsBunchTooLarge(Connection, Bunch), TEXT("Attempted to send bunch exceeding max allowed size. BunchSize=%d, MaximumSize=%d Channel: %s"), Bunch->GetNumBytes(), NetMaxConstructedPartialBunchSizeBytes, *Describe()))
	{
		UE_LOG(LogUTcp, Error, TEXT("Attempted to send bunch exceeding max allowed size. BunchSize=%d, MaximumSize=%d Channel: %s"), Bunch->GetNumBytes(), NetMaxConstructedPartialBunchSizeBytes, *Describe());
		Bunch->SetError();
		return FPacketIdRange(INDEX_NONE);
	}

	check(!Closing);
	checkf(Connection->Channels[ChIndex] == this, TEXT("This: %s, Connection->Channels[ChIndex]: %s"), *Describe(), Connection->Channels[ChIndex] ? *Connection->Channels[ChIndex]->Describe() : TEXT("Null"));
	check(!Bunch->IsError());
	check(!Bunch->bHasPackageMapExports);

	// Set bunch flags.

	const bool bDormancyClose = Bunch->bClose && (Bunch->CloseReason == EChannelCloseReason::Dormancy);

	if (OpenedLocally && ((OpenPacketId.First == INDEX_NONE) || ((Connection->ResendAllDataState != EResendAllDataState::None) && !bDormancyClose)))
	{
		bool bOpenBunch = true;

		if (Connection->ResendAllDataState == EResendAllDataState::SinceCheckpoint)
		{
			bOpenBunch = !bOpenedForCheckpoint;
			bOpenedForCheckpoint = true;
		}

		if (bOpenBunch)
		{
			Bunch->bOpen = 1;
			OpenTemporary = !Bunch->bReliable;
		}
	}

	// If channel was opened temporarily, we are never allowed to send reliable packets on it.
	check(!OpenTemporary || !Bunch->bReliable);

	// This is the max number of bits we can have in a single bunch
	const int64 MAX_SINGLE_BUNCH_SIZE_BITS = Connection->GetMaxSingleBunchSizeBits();

	// Max bytes we'll put in a partial bunch
	const int64 MAX_SINGLE_BUNCH_SIZE_BYTES = MAX_SINGLE_BUNCH_SIZE_BITS / 8;

	// Max bits will put in a partial bunch (byte aligned, we dont want to deal with partial bytes in the partial bunches)
	const int64 MAX_PARTIAL_BUNCH_SIZE_BITS = MAX_SINGLE_BUNCH_SIZE_BYTES * 8;

	TArray<FOutBunch*>& OutgoingBunches = Connection->GetOutgoingBunches();
	OutgoingBunches.Reset();

	// Add any export bunches
	// Replay connections will manage export bunches separately.
	if (!Connection->IsInternalAck())
	{
		AppendExportBunches(OutgoingBunches);
	}

	if (OutgoingBunches.Num())
	{
		// Don't merge if we are exporting guid's
		// We can't be for sure if the last bunch has exported guids as well, so this just simplifies things
		Merge = false;
	}

	if (Connection->Driver->IsServer())
	{
		// This is a bit special, currently we report this is at the end of bunch event though AppendMustBeMappedGuids rewrites the entire bunch
		// UE_NET_TRACE_SCOPE(MustBeMappedGuids_IsAtStartOfBunch, *Bunch, GetTraceCollector(*Bunch), ENetTraceVerbosity::Trace);

		// Append any "must be mapped" guids to front of bunch from the packagemap
		AppendMustBeMappedGuids(Bunch);

		if (Bunch->bHasMustBeMappedGUIDs)
		{
			// We can't merge with this, since we need all the unique static guids in the front
			Merge = false;
		}
	}

	//-----------------------------------------------------
	// Contemplate merging.
	//-----------------------------------------------------
	int32 PreExistingBits = 0;
	FOutBunch* OutBunch = NULL;
	//-----------------------------------------------------
	// Possibly split large bunch into list of smaller partial bunches
	//-----------------------------------------------------
	if (Bunch->GetNumBits() > MAX_SINGLE_BUNCH_SIZE_BITS)
	{
		uint8* data = Bunch->GetData();
		int64 bitsLeft = Bunch->GetNumBits();
		Merge = false;

		while (bitsLeft > 0)
		{
			FOutBunch* PartialBunch = new FOutBunch(this, false);
			int64 bitsThisBunch = FMath::Min<int64>(bitsLeft, MAX_PARTIAL_BUNCH_SIZE_BITS);
			PartialBunch->SerializeBits(data, bitsThisBunch);

#if UE_NET_TRACE_ENABLED
			// Attach tracecollector of split bunch to first partial bunch
			SetTraceCollector(*PartialBunch, GetTraceCollector(*Bunch));
			SetTraceCollector(*Bunch, nullptr);
#endif

			OutgoingBunches.Add(PartialBunch);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			PartialBunch->DebugString = FString::Printf(TEXT("Partial[%d]: %s"), OutgoingBunches.Num(), *Bunch->DebugString);
#endif

			bitsLeft -= bitsThisBunch;
			data += (bitsThisBunch >> 3);

			UE_LOG(LogUTcp, Log, TEXT("	Making partial bunch from content bunch. bitsThisBunch: %d bitsLeft: %d"), bitsThisBunch, bitsLeft);

			ensure(bitsLeft == 0 || bitsThisBunch % 8 == 0); // Byte aligned or it was the last bunch
		}
	}
	else
	{
		OutgoingBunches.Add(Bunch);
	}

	//-----------------------------------------------------
	// Send all the bunches we need to
	//	Note: this is done all at once. We could queue this up somewhere else before sending to Out.
	//-----------------------------------------------------
	FPacketIdRange PacketIdRange;

	const bool bOverflowsReliable = (NumOutRec + OutgoingBunches.Num() >= RELIABLE_BUFFER + Bunch->bClose);
	if (Bunch->bReliable && bOverflowsReliable)
	{
		UE_LOG(LogUTcp, Warning, TEXT("SendBunch: Reliable partial bunch overflows reliable buffer! %s"), *Describe());
		UE_LOG(LogUTcp, Warning, TEXT("   Num OutgoingBunches: %d. NumOutRec: %d"), OutgoingBunches.Num(), NumOutRec);
		PrintReliableBunchBuffer();

		// Bail out, we can't recover from this (without increasing RELIABLE_BUFFER)
		FString ErrorMsg = NSLOCTEXT("NetworkErrors", "ClientReliableBufferOverflow", "Outgoing reliable buffer overflow").ToString();

		Connection->SendCloseReason(ENetCloseResult::ReliableBufferOverflow);
		FNetControlMessage<NMT_Failure>::Send(Connection, ErrorMsg);
		Connection->FlushNet(true);
		Connection->Close(ENetCloseResult::ReliableBufferOverflow);

		return PacketIdRange;
	}

	UE_CLOG((OutgoingBunches.Num() > 1), LogUTcp, Log, TEXT("Sending %d Bunches. Channel: %d %s"), OutgoingBunches.Num(), Bunch->ChIndex, *Describe());
	for (int32 PartialNum = 0; PartialNum < OutgoingBunches.Num(); ++PartialNum)
	{
		FOutBunch* NextBunch = OutgoingBunches[PartialNum];

		NextBunch->bReliable = Bunch->bReliable;
		NextBunch->bOpen = Bunch->bOpen;
		NextBunch->bClose = Bunch->bClose;
		NextBunch->CloseReason = Bunch->CloseReason;
		NextBunch->bIsReplicationPaused = Bunch->bIsReplicationPaused;
		NextBunch->ChIndex = Bunch->ChIndex;
		NextBunch->ChName = Bunch->ChName;

		if (!NextBunch->bHasPackageMapExports)
		{
			NextBunch->bHasMustBeMappedGUIDs |= Bunch->bHasMustBeMappedGUIDs;
		}

		if (OutgoingBunches.Num() > 1)
		{
			NextBunch->bPartial = 1;
			NextBunch->bPartialInitial = (PartialNum == 0 ? 1 : 0);
			NextBunch->bPartialFinal = (PartialNum == OutgoingBunches.Num() - 1 ? 1 : 0);
			NextBunch->bOpen &= (PartialNum == 0);											  // Only the first bunch should have the bOpen bit set
			NextBunch->bClose = (Bunch->bClose && (OutgoingBunches.Num() - 1 == PartialNum)); // Only last bunch should have bClose bit set
		}

		FOutBunch* ThisOutBunch = UTcpPrepBunch(NextBunch, OutBunch, Merge); // This handles queuing reliable bunches into the ack list

		if (UE_LOG_ACTIVE(LogUTcp, Verbose) && (OutgoingBunches.Num() > 1)) // Don't want to call appMemcrc unless we need to
		{
			UE_LOG(LogUTcp, Verbose, TEXT("	Bunch[%d]: Bytes: %d Bits: %d ChSequence: %d 0x%X"), PartialNum, ThisOutBunch->GetNumBytes(), ThisOutBunch->GetNumBits(), ThisOutBunch->ChSequence, FCrc::MemCrc_DEPRECATED(ThisOutBunch->GetData(), ThisOutBunch->GetNumBytes()));
		}

		// Update Packet Range
		int32 PacketId = UTcpSendRawBunch(ThisOutBunch, Merge);
		if (PartialNum == 0)
		{
			PacketIdRange = FPacketIdRange(PacketId);
		}
		else
		{
			PacketIdRange.Last = PacketId;
		}

		// Update channel sequence count.
		Connection->LastOut = *ThisOutBunch;
		Connection->LastEnd = FBitWriterMark(Connection->SendBuffer);
	}

	// Update open range if necessary
	if (Bunch->bOpen && (Connection->ResendAllDataState == EResendAllDataState::None))
	{
		OpenPacketId = PacketIdRange;
	}

	// Destroy outgoing bunches now that they are sent, except the one that was passed into ::SendBunch
	//	This is because the one passed in ::SendBunch is the responsibility of the caller, the other bunches in OutgoingBunches
	//	were either allocated in this function for partial bunches, or taken from the package map, which expects us to destroy them.
	for (auto It = OutgoingBunches.CreateIterator(); It; ++It)
	{
		FOutBunch* DeleteBunch = *It;
		if (DeleteBunch != Bunch)
			delete DeleteBunch;
	}

	return PacketIdRange;
}

void UUTcpChannel::UTcpOnRecv(FInBunch& Bunch)
{
	CastChecked<UUTcpNetDriver>(Connection->Driver)->GetNetConnectionDebug()->OnRecv(Connection, &Bunch);
}

int32 UUTcpChannel::UTcpSendRawBunch(FOutBunch* OutBunch, bool Merge)
{
	// Sending for checkpoints may need to send an open bunch if the actor went dormant, so allow the OpenPacketId to be set

	// Send the raw bunch.
	OutBunch->ReceivedAck = 0;
	int32 PacketId = Cast<UUTcpConnection>(Connection)->InternalSendRawBunch(OutBunch, Merge);
	if (OpenPacketId.First == INDEX_NONE && OpenedLocally)
	{
		OpenPacketId = FPacketIdRange(PacketId);
	}

	if (OutBunch->bClose)
	{
		SetClosingFlag();
	}

	// return PacketId;
	return Connection->OutPacketId;
}
