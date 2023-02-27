#include "utcp.h"
#include "bit_buffer.h"
#include "utcp_channel.h"
#include "utcp_handshake.h"
#include "utcp_packet.h"
#include "utcp_utils.h"
#include <assert.h>
#include <string.h>

static struct utcp_config utcp_config = {0};

struct utcp_config* utcp_get_config()
{
	return &utcp_config;
}

void utcp_add_elapsed_time(int64_t delta_time_ns)
{
	utcp_config.ElapsedTime += (delta_time_ns / 1000);
}

struct utcp_listener* utcp_listener_create()
{
	return (struct utcp_listener*)utcp_realloc(NULL, sizeof(struct utcp_listener));
}

void utcp_listener_destroy(struct utcp_listener* fd)
{
	if (fd)
	{
		utcp_realloc(fd, 0);
	}
}

void utcp_listener_init(struct utcp_listener* fd, void* userdata)
{
	memset(fd, 0, sizeof(*fd));
	fd->userdata = userdata;
	fd->ActiveSecret = 255;

	utcp_listener_update_secret(fd, NULL);
}

// StatelessConnectHandlerComponent::UpdateSecret
void utcp_listener_update_secret(struct utcp_listener* fd, uint8_t special_secret[64] /* = NULL*/)
{
	static_assert(SECRET_BYTE_SIZE == 64, "SECRET_BYTE_SIZE == 64");

	fd->LastSecretUpdateTimestamp = utcp_gettime();

	// On first update, update both secrets
	if (fd->ActiveSecret == 255)
	{
		uint8_t* CurArray = fd->HandshakeSecret[1];
		for (int i = 0; i < SECRET_BYTE_SIZE; i++)
		{
			CurArray[i] = rand() % 255;
		}

		fd->ActiveSecret = 0;
	}
	else
	{
		fd->ActiveSecret = (uint8_t)!fd->ActiveSecret;
	}

	uint8_t* CurArray = fd->HandshakeSecret[fd->ActiveSecret];
	if (special_secret)
	{
		memcpy(CurArray, special_secret, SECRET_BYTE_SIZE);
	}
	else
	{
		for (int i = 0; i < SECRET_BYTE_SIZE; i++)
		{
			CurArray[i] = rand() % 255;
		}
	}
}

int utcp_listener_incoming(struct utcp_listener* fd, const char* address, const uint8_t* buffer, int len)
{
	utcp_dump("listener", "incoming", buffer, len);
	return process_connectionless_packet(fd, address, buffer, len);
}

void utcp_listener_accept(struct utcp_listener* listener, struct utcp_connection* conn, bool reconnect)
{
	if (!reconnect)
	{
		assert(conn->challenge_data == NULL);
		memcpy(conn->AuthorisedCookie, listener->AuthorisedCookie, sizeof(conn->AuthorisedCookie));
		utcp_sequence_init(conn, listener->LastClientSequence, listener->LastServerSequence);
	}
}

struct utcp_connection* utcp_connection_create()
{
	return (struct utcp_connection*)utcp_realloc(NULL, sizeof(struct utcp_connection));
}

void utcp_connection_destroy(struct utcp_connection* fd)
{
	if (fd)
	{
		utcp_realloc(fd, 0);
	}
}

void utcp_init(struct utcp_connection* fd, void* userdata)
{
	memset(fd, 0, sizeof(*fd));
	fd->userdata = userdata;
	fd->LastReceiveRealtime = utcp_gettime_ms();
}

void utcp_uninit(struct utcp_connection* fd)
{
	utcp_mark_close(fd, Cleanup);
	utcp_channels_uninit(&fd->channels);
	if (fd->challenge_data)
	{
		utcp_realloc(fd->challenge_data, 0);
		fd->challenge_data = NULL;
	}
}

void utcp_connect(struct utcp_connection* fd)
{
	assert(!fd->challenge_data);
	fd->challenge_data = (struct utcp_challenge_data*)utcp_realloc(NULL, sizeof(*fd->challenge_data));
	memset(fd->challenge_data, 0, sizeof(sizeof(*fd->challenge_data)));
	fd->challenge_data->bBeganHandshaking = true;

	handshake_begin(fd);
}

// UNetConnection::ReceivedRawPacket
bool utcp_incoming(struct utcp_connection* fd, uint8_t* buffer, int len)
{
	utcp_dump(fd->debug_name, "incoming", buffer, len);

	fd->LastReceiveRealtime = utcp_gettime_ms();

	struct bitbuf bitbuf;
	if (!bitbuf_read_init(&bitbuf, buffer, len))
	{
		utcp_log(Warning, "[%s]Received packet with 0's in last byte of packet", fd->debug_name);
		utcp_mark_close(fd, ZeroLastByte);
		return false;
	}

	int ret = handshake_incoming(fd, &bitbuf);
	if (ret != 0)
	{
		if (!is_client(fd))
			utcp_log(Warning, "[%s]handshake_incoming failed, ret=%d", fd->debug_name, ret);
		utcp_mark_close(fd, PacketHandlerIncomingError);
		return false;
	}

	size_t left_bits = bitbuf_left_bits(&bitbuf);
	if (left_bits == 0)
		return true;


	bitbuf.size--;
	ret = ReceivedPacket(fd, &bitbuf);
	if (!ret)
		return false;

	left_bits = bitbuf_left_bits(&bitbuf);
	return left_bits == 0;
}

int utcp_update(struct utcp_connection* fd)
{
	if (is_connected(fd))
	{
		int64_t now = utcp_gettime_ms();
		if (now - fd->LastReceiveRealtime > UTCP_CONNECT_TIMEOUT)
			utcp_mark_close(fd, ConnectionTimeout);
	}
	else
	{
		handshake_update(fd);
	}

	utcp_delay_close_channel(&fd->channels);

	if (!fd->bClose)
		return 0;

	utcp_on_disconnect(fd, fd->CloseReason);
	return -1;
}

int32_t utcp_peep_packet_id(struct utcp_connection* fd, uint8_t* buffer, int len)
{
	struct bitbuf bitbuf;
	if (!bitbuf_read_init(&bitbuf, buffer, len))
		return -1;

	uint8_t bHandshakePacket;
	if (!bitbuf_read_bit(&bitbuf, &bHandshakePacket))
		return -1;

	if (bHandshakePacket)
		return 0;

	return PeekPacketId(fd, &bitbuf);
}

int32_t utcp_expect_packet_id(struct utcp_connection* fd)
{
	return fd->InPacketId + 1;
}

int32_t utcp_send_bunch(struct utcp_connection* fd, struct utcp_bunch* bunch)
{
	int32_t packet_id = SendRawBunch(fd, bunch);
	if (packet_id >= 0)
	{
		utcp_log(Verbose, "[%s]send bunch, bOpen=%d, bClose=%d, NameIndex=%d, ChIndex=%d, NumBits=%d, PacketId=%d",
				 fd->debug_name, bunch->bOpen, bunch->bClose, bunch->ChType, bunch->ChIndex, bunch->DataBitsLen, packet_id);
		return packet_id;
	}

	utcp_log(Warning, "[%s]send bunch failed:%d", fd->debug_name, packet_id);
	return PACKET_ID_INDEX_NONE;
}

// UNetConnection::FlushNet
int utcp_send_flush(struct utcp_connection* fd)
{
	if (!is_connected(fd))
		return 0;

	int64_t now = utcp_gettime_ms();

	// If there is any pending data to send, send it.
	if (fd->SendBufferBitsNum == 0 && (now - fd->LastSendTime) < UTCP_KEEP_ALIVE_TIME)
		return 0;

	// If sending keepalive packet, still write the packet id
	if (fd->SendBufferBitsNum == 0)
		WriteBitsToSendBuffer(fd, NULL, 0, NULL, 0);

	struct bitbuf bitbuf;
	bitbuf_write_reuse(&bitbuf, fd->SendBuffer, fd->SendBufferBitsNum, sizeof(fd->SendBuffer));

	// Write the UNetConnection-level termination bit
	bitbuf_write_end(&bitbuf); // Packet

	bitbuf_write_end(&bitbuf); // Handshake
	utcp_connection_outgoing(fd, bitbuf.buffer, bitbuf_num_bytes(&bitbuf));

	memset(fd->SendBuffer, 0, sizeof(fd->SendBuffer));
	fd->SendBufferBitsNum = 0;

	fd->LastSendTime = now;
	fd->OutPacketId++;

	// Move acks around.
	for (int32_t i = 0; i < fd->QueuedAcksCount; i++)
	{
		fd->ResendAcks[fd->ResendAcksCount] = fd->QueuedAcks[i];
		fd->ResendAcksCount++;
	}
	fd->QueuedAcksCount = 0;

	return 0;
}

bool utcp_send_would_block(struct utcp_connection* fd, int count)
{
	return fd->OutPacketId - fd->OutAckPacketId + count >= (UTCP_MAX_PARTIAL_BUNCH_COUNT - 2);
}

bool utcp_is_handshake_packet(uint8_t* buffer, int len)
{
	struct bitbuf bitbuf;
	if (!bitbuf_read_init(&bitbuf, buffer, len))
	{
		return false;
	}

	uint8_t bHandshakePacket;
	if (!bitbuf_read_bit(&bitbuf, &bHandshakePacket))
		return false;
	
	return bHandshakePacket;
}

void utcp_mark_close(struct utcp_connection* fd, uint8_t close_reason)
{
	if (fd->bClose)
		return;
	fd->bClose = true;
	fd->CloseReason = close_reason;
	utcp_log(Warning, "[%s]utcp_mark_close, type=%hhu", fd->debug_name, close_reason);
}
