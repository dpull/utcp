#include "rudp.h"
#include "bit_buffer.h"
#include "rudp_handshake.h"
#include "rudp_packet.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define KeepAliveTime (0.2 * 1000)

struct rudp_env
{
	callback_fn callback;
	int64_t ElapsedTime;
	uint32_t MagicHeader;
	uint8_t MagicHeaderBits;
	uint8_t enable_debug_cookie;
	uint8_t debug_cookie[COOKIE_BYTE_SIZE];
};

static struct rudp_env rudp_env = {0};
void rudp_env_init(void)
{
	memset(&rudp_env, 0, sizeof(rudp_env));
}

void rudp_env_add_time(int64_t delta_time_ns)
{
	rudp_env.ElapsedTime += (delta_time_ns / 1000);
}
int64_t rudp_gettime_ms(void)
{
	return rudp_env.ElapsedTime / 1000;
}
double rudp_gettime(void)
{
	return ((double)rudp_env.ElapsedTime) / 1000 / 1000 / 1000;
}

void rudp_env_setcallback(callback_fn callback)
{
	rudp_env.callback = callback;
}

void rudp_env_set_debug_cookie(const char debug_cookie[20])
{
	rudp_env.enable_debug_cookie = true;
	memcpy(rudp_env.debug_cookie, debug_cookie, sizeof(rudp_env.debug_cookie));
}

bool try_use_debug_cookie(uint8_t* out_cookie)
{
	if (rudp_env.enable_debug_cookie)
		memcpy(out_cookie, rudp_env.debug_cookie, sizeof(rudp_env.debug_cookie));
	return rudp_env.enable_debug_cookie;
}

bool write_magic_header(struct bitbuf* bitbuf)
{
	return true;
}

bool read_magic_header(struct bitbuf* bitbuf)
{
	return true;
}

struct rudp_fd* rudp_create()
{
	return malloc(sizeof(struct rudp_fd));
}

void rudp_destory(struct rudp_fd* fd)
{
	free(fd);
}

void rudp_raw_send(struct rudp_fd* fd, char* buffer, size_t len)
{
	rudp_env.callback(fd, fd->userdata, callback_send, buffer, (int)len);
}

void rudp_raw_accept(struct rudp_fd* fd, bool new_conn, char* buffer, size_t len)
{
	if (new_conn)
	{
		rudp_env.callback(fd, fd->userdata, callback_newconn, buffer, (int)len);
	}
	else
	{
		rudp_env.callback(fd, fd->userdata, callback_reconn, buffer, (int)len);
	}
}

void rudp_init(struct rudp_fd* fd, void* userdata, int is_client)
{
	memset(fd, 0, sizeof(*fd));
	fd->userdata = userdata;
	fd->mode = is_client ? Client : Server;
	fd->ActiveSecret = 255;

	if (is_client)
	{
		fd->bBeganHandshaking = true;
		NotifyHandshakeBegin(fd);
	}
}

// ReceivedRawPacket
// PacketHandler
// StatelessConnectHandlerComponent::Incoming
int rudp_incoming(struct rudp_fd* fd, char* buffer, int len)
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

// UIpNetDriver::ProcessConnectionlessPacket
int rudp_accept_incoming(struct rudp_fd* fd, const char* address, const char* buffer, int len)
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
			rudp_raw_accept(fd, false, NULL, 0);
		}

		// bIgnorePacket = false
	}
	if (bPassedChallenge)
	{
		if (!bRestartedHandshake)
		{
			rudp_raw_accept(fd, true, NULL, 0);
		}
		ResetChallengeData(fd);
	}
	return 0;
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
	bitbuf_write_bit(&bitbuf, 1);

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
