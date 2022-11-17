#include "rudp.h"
#include "bit_buffer.h"
#include "rudp_bunch_data.h"
#include "rudp_config.h"
#include "rudp_handshake.h"
#include "rudp_packet.h"
#include <assert.h>
#include <string.h>

#define KeepAliveTime (int)(0.2 * 1000)

static struct rudp_config rudp_config = {0};

void rudp_add_time(int64_t delta_time_ns)
{
	rudp_config.ElapsedTime += (delta_time_ns / 1000);
}

struct rudp_config* rudp_get_config()
{
	return &rudp_config;
}

void rudp_init(struct rudp_fd* fd, void* userdata, int is_client)
{
	memset(fd, 0, sizeof(*fd));
	fd->userdata = userdata;
	fd->mode = is_client ? Client : Server;
	fd->ActiveSecret = 255;
	init_rudp_bunch_data(&fd->rudp_bunch_data);

	if (is_client)
	{
		fd->bBeganHandshaking = true;
		NotifyHandshakeBegin(fd);
	}
}

// UIpNetDriver::ProcessConnectionlessPacket
int rudp_connectionless_incoming(struct rudp_fd* fd, const char* address, const uint8_t* buffer, int len)
{
	struct bitbuf bitbuf;
	if (!bitbuf_read_init(&bitbuf, buffer, len))
	{
		return -1;
	}

	int ret = IncomingConnectionless(fd, address, &bitbuf);
	if (ret)
		return ret;

	assert(bitbuf.num == bitbuf.size);

	bool bPassedChallenge = false;
	bool bRestartedHandshake = false;
	// bool bIgnorePacket = true;

	bPassedChallenge = HasPassedChallenge(fd, address, &bRestartedHandshake);
	if (bPassedChallenge)
	{
		if (bRestartedHandshake)
		{
			rudp_accept(fd, false);
		}

		// bIgnorePacket = false
	}
	if (bPassedChallenge)
	{
		if (!bRestartedHandshake)
		{
			rudp_accept(fd, true);
		}
		ResetChallengeData(fd);
	}
	return 0;
}

void rudp_sequence_init(struct rudp_fd* fd, int32_t IncomingSequence, int32_t OutgoingSequence)
{
	fd->InPacketId = IncomingSequence - 1;
	fd->OutPacketId = OutgoingSequence;
	fd->OutAckPacketId = OutgoingSequence - 1;
	fd->LastNotifiedPacketId = fd->OutAckPacketId;

	// Initialize the reliable packet sequence (more useful/effective at preventing attacks)
	fd->InitInReliable = IncomingSequence & (MAX_CHSEQUENCE - 1);
	fd->InitOutReliable = OutgoingSequence & (MAX_CHSEQUENCE - 1);

	for (int i = 0; i < DEFAULT_MAX_CHANNEL_SIZE; ++i)
	{
		fd->InReliable[i] = fd->InitInReliable;
		fd->OutReliable[i] = fd->InitOutReliable;
	}
	packet_notify_Init(&fd->packet_notify, fd->InPacketId, fd->OutPacketId);
}

// ReceivedRawPacket
// PacketHandler
// StatelessConnectHandlerComponent::Incoming
int rudp_incoming(struct rudp_fd* fd, uint8_t* buffer, int len)
{
	struct bitbuf bitbuf;
	if (!bitbuf_read_init(&bitbuf, buffer, len))
	{
		return -1;
	}

	int ret = Incoming(fd, &bitbuf);
	if (ret != 0)
	{
		return ret;
	}

	size_t left_bits = bitbuf_left_bits(&bitbuf);
	if (left_bits == 0)
	{
		return 0;
	}

	// TODO PacketHandler::ReplaceIncomingPacket
	// 这儿应当直接size-1就可以, 但先参考unreal的做法
	size_t left_bytes = bitbuf_left_bytes(&bitbuf);
	char replace_buffer[1600]; // tmp code

	bitbuf_read_bits(&bitbuf, replace_buffer, left_bits);
	if (!bitbuf_read_init(&bitbuf, replace_buffer, left_bytes))
	{
		return -3;
	}

	ret = ReceivedPacket(fd, &bitbuf);
	if (ret != 0)
	{
		return ret;
	}

	left_bits = bitbuf_left_bits(&bitbuf);
	assert(left_bits == 0);
	return left_bits != 0;
}

int rudp_update(struct rudp_fd* fd)
{
	if (fd->mode == Client)
	{
		int64_t now = rudp_gettime_ms();
		if (fd->state != Initialized && fd->LastClientSendTimestamp != 0)
		{
			int64_t LastSendTimeDiff = -fd->LastClientSendTimestamp;
			if (LastSendTimeDiff > 1000)
			{
				const bool bRestartChallenge = now - fd->LastChallengeTimestamp > MIN_COOKIE_LIFETIME * 1000;

				if (bRestartChallenge)
				{
					rudp_set_state(fd, UnInitialized);
				}

				if (fd->state == UnInitialized)
				{
					NotifyHandshakeBegin(fd);
				}
				else if (fd->state == InitializedOnLocal && fd->LastTimestamp != 0.0)
				{
					SendChallengeResponse(fd, fd->LastSecretId, fd->LastTimestamp, fd->LastCookie);
				}
			}
		}
	}
	else
	{
		double now = rudp_gettime();
		if (now - fd->LastSecretUpdateTimestamp > SECRET_UPDATE_TIME || fd->LastSecretUpdateTimestamp == 0)
		{
			UpdateSecret(fd);
		}
	}
	return 0;
}

int32_t rudp_packet_peep_id(struct rudp_fd* fd, uint8_t* buffer, int len)
{
	struct bitbuf bitbuf;
	if (!bitbuf_read_init(&bitbuf, buffer, len))
		return -1;

	read_magic_header(&bitbuf);
	uint8_t bHandshakePacket;
	if (!bitbuf_read_bit(&bitbuf, &bHandshakePacket))
		return -1;

	if (!bHandshakePacket)
		return 0;

	// Read packed header
	uint32_t PackedHeader = 0;
	if (!bitbuf_read_bytes(&bitbuf, &PackedHeader, sizeof(PackedHeader)))
	{
		return -2;
	}

	// unpack
	// notification_header->Seq = PackedHeader_GetSeq(PackedHeader);
}

struct packet_id_range rudp_send(struct rudp_fd* fd, struct rudp_bunch* bunches[], int bunches_count)
{
	struct packet_id_range PacketIdRange = {PACKET_ID_INDEX_NONE, PACKET_ID_INDEX_NONE};
	if (!check_can_send(fd, bunches, bunches_count))
		return PacketIdRange;

	for (int i = 0; i < bunches_count; ++i)
	{
		int32_t PacketId = SendRawBunch(fd, bunches[i]);
		assert(PacketId >= 0);
		if (i == 0)
			PacketIdRange.First = PacketId;
		else
			PacketIdRange.Last = PacketId;
	}

	return PacketIdRange;
}

int rudp_flush(struct rudp_fd* fd)
{
	int64_t now = rudp_gettime_ms();
	if (fd->SendBufferBitsNum == 0 && !fd->HasDirtyAcks && (now - fd->LastSendTime) < KeepAliveTime)
		return 0;

	if (fd->SendBufferBitsNum == 0)
	{
		WriteBitsToSendBuffer(fd, NULL, 0);
	}

	struct bitbuf bitbuf;
	bitbuf_write_reuse(&bitbuf, fd->SendBuffer, fd->SendBufferBitsNum, sizeof(fd->SendBuffer));

	// Write the UNetConnection-level termination bit
	bitbuf_write_end(&bitbuf);

	// if we update ack, we also update received ack associated with outgoing seq
	// so we know how many ack bits we need to write (which is updated in received packet)
	WritePacketHeader(fd, &bitbuf);
	WriteFinalPacketInfo(fd, &bitbuf);

	CapHandshakePacket(fd, &bitbuf);
	rudp_raw_send(fd, bitbuf.buffer, bitbuf_num_bytes(&bitbuf));

	memset(fd->SendBuffer, 0, sizeof(fd->SendBuffer));
	fd->SendBufferBitsNum = 0;
	return 0;
}
