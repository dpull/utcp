#pragma once
#include "Net/DataBunch.h"
#include "utcp/utcp_def.h"

struct FConvert
{
	static void To(const FOutBunch* InBunch, utcp_bunch* OutBunch)
	{
		memset(OutBunch, 0, sizeof(*OutBunch));
		OutBunch->NameIndex = (uint32)(*(InBunch->ChName.ToEName()));
		OutBunch->ChIndex = InBunch->ChIndex;
		OutBunch->bOpen = InBunch->bOpen;
		OutBunch->bClose = InBunch->bClose;
		OutBunch->CloseReason = (uint8_t)InBunch->CloseReason;
		OutBunch->bIsReplicationPaused = InBunch->bIsReplicationPaused;
		OutBunch->bReliable = InBunch->bReliable;
		OutBunch->bHasPackageMapExports = InBunch->bHasPackageMapExports;
		OutBunch->bHasMustBeMappedGUIDs = InBunch->bHasMustBeMappedGUIDs;
		OutBunch->bPartial = InBunch->bPartial;
		OutBunch->bPartialInitial = InBunch->bPartialInitial;
		OutBunch->bPartialFinal = InBunch->bPartialFinal;
		OutBunch->DataBitsLen = InBunch->GetNumBits();
		if (OutBunch->DataBitsLen > 0)
		{
			memcpy(OutBunch->Data, InBunch->GetData(), InBunch->GetNumBytes());
		}
	}

	static void To(const utcp_bunch* InBunch, FInBunch* OutBunch)
	{
		OutBunch->ChName = EName(InBunch->NameIndex);
		OutBunch->PacketId = InBunch->PacketId;
		OutBunch->ChIndex = InBunch->ChIndex;
		OutBunch->ChSequence = InBunch->ChSequence;
		OutBunch->bOpen = InBunch->bOpen;
		OutBunch->bClose = InBunch->bClose;
		OutBunch->CloseReason = (EChannelCloseReason)InBunch->CloseReason;
		OutBunch->bIsReplicationPaused = InBunch->bIsReplicationPaused;
		OutBunch->bReliable = InBunch->bReliable;
		OutBunch->bHasPackageMapExports = InBunch->bHasPackageMapExports;
		OutBunch->bHasMustBeMappedGUIDs = InBunch->bHasMustBeMappedGUIDs;
		OutBunch->bPartial = InBunch->bPartial;
		OutBunch->bPartialInitial = InBunch->bPartialInitial;
		OutBunch->bPartialFinal = InBunch->bPartialFinal;
		OutBunch->SetData((uint8*)InBunch->Data, InBunch->DataBitsLen);
	}
};
