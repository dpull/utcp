#include "utcp.h"
#include "bit_buffer.h"
#include "utcp_bunch_data.h"
#include "utcp_handshake.h"
#include "utcp_packet.h"
#include "utcp_packet_notify.h"
#include "utcp_sequence_number.h"
#include "utcp_utils.h"
#include <assert.h>
#include <string.h>

#define KeepAliveTime (int)(0.2 * 1000)

static struct utcp_config utcp_config = {0};

void utcp_add_time(int64_t delta_time_ns)
{
	utcp_config.ElapsedTime += (delta_time_ns / 1000);
}

struct utcp_config* utcp_get_config()
{
	return &utcp_config;
}

void utcp_init(struct utcp_fd* fd, void* userdata, int is_client)
{
	memset(fd, 0, sizeof(*fd));
	fd->userdata = userdata;
	fd->mode = is_client ? Client : Server;
	fd->ActiveSecret = 255;
	init_utcp_bunch_data(&fd->utcp_bunch_data);

	if (is_client)
	{
		fd->bBeganHandshaking = true;
		NotifyHandshakeBegin(fd);
	}
}

// UIpNetDriver::ProcessConnectionlessPacket
int utcp_connectionless_incoming(struct utcp_fd* fd, const char* address, const uint8_t* buffer, int len)
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
			utcp_accept(fd, true);
		}

		// bIgnorePacket = false
	}
	if (bPassedChallenge)
	{
		if (!bRestartedHandshake)
		{
			utcp_accept(fd, false);
		}
		ResetChallengeData(fd);
	}
	return 0;
}

// UNetConnection::InitSequence
void utcp_sequence_init(struct utcp_fd* fd, int32_t IncomingSequence, int32_t OutgoingSequence)
{
	fd->InPacketId = IncomingSequence - 1;
	fd->OutPacketId = OutgoingSequence;
	fd->OutAckPacketId = OutgoingSequence - 1;
	fd->LastNotifiedPacketId = fd->OutAckPacketId;

	// Initialize the reliable packet sequence (more useful/effective at preventing attacks)
	fd->InitInReliable = IncomingSequence & (UTCP_MAX_CHSEQUENCE - 1);
	fd->InitOutReliable = OutgoingSequence & (UTCP_MAX_CHSEQUENCE - 1);

	for (int i = 0; i < DEFAULT_MAX_CHANNEL_SIZE; ++i)
	{
		fd->InReliable[i] = fd->InitInReliable;
		fd->OutReliable[i] = fd->InitOutReliable;
	}
	packet_notify_Init(&fd->packet_notify, seq_num_init(fd->InPacketId), seq_num_init(fd->OutPacketId));
}

// ReceivedRawPacket
// PacketHandler
// StatelessConnectHandlerComponent::Incoming
int utcp_ordered_incoming(struct utcp_fd* fd, uint8_t* buffer, int len)
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

	bitbuf.size--;
	ret = ReceivedPacket(fd, &bitbuf);
	if (ret != 0)
	{
		return ret;
	}

	left_bits = bitbuf_left_bits(&bitbuf);
	assert(left_bits == 0);
	return 0;
}

int utcp_update(struct utcp_fd* fd)
{
	if (fd->mode == Client)
	{
		int64_t now = utcp_gettime_ms();
		if (fd->state != Initialized && fd->LastClientSendTimestamp != 0)
		{
			int64_t LastSendTimeDiff = -fd->LastClientSendTimestamp;
			if (LastSendTimeDiff > 1000)
			{
				const bool bRestartChallenge = now - fd->LastChallengeTimestamp > MIN_COOKIE_LIFETIME * 1000;

				if (bRestartChallenge)
				{
					utcp_set_state(fd, UnInitialized);
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
		double now = utcp_gettime();
		if (now - fd->LastSecretUpdateTimestamp > SECRET_UPDATE_TIME || fd->LastSecretUpdateTimestamp == 0)
		{
			UpdateSecret(fd);
		}
	}
	return 0;
}

int32_t utcp_peep_packet_id(struct utcp_fd* fd, uint8_t* buffer, int len)
{
	struct bitbuf bitbuf;
	if (!bitbuf_read_init(&bitbuf, buffer, len))
		return -1;

	read_magic_header(&bitbuf);
	uint8_t bHandshakePacket;
	if (!bitbuf_read_bit(&bitbuf, &bHandshakePacket))
		return -1;

	if (bHandshakePacket)
		return 0;

	return PeekPacketId(fd, &bitbuf);
}

int32_t utcp_expect_packet_id(struct utcp_fd* fd)
{
	return fd->InPacketId + 1;
}

int32_t utcp_send_bunch(struct utcp_fd* fd, struct utcp_bunch* bunch)
{
	return SendRawBunch(fd, bunch); 
}

// UNetConnection::FlushNet
int utcp_flush(struct utcp_fd* fd)
{
	int64_t now = utcp_gettime_ms();
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

	bitbuf_write_end(&bitbuf);
	utcp_raw_send(fd, bitbuf.buffer, bitbuf_num_bytes(&bitbuf));

	memset(fd->SendBuffer, 0, sizeof(fd->SendBuffer));
	fd->SendBufferBitsNum = 0;

	packet_notify_CommitAndIncrementOutSeq(&fd->packet_notify);
	fd->LastSendTime = now;
	fd->OutPacketId++;

	return 0;
}
