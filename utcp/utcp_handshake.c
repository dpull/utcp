#include "utcp_handshake.h"
#include "bit_buffer.h"
#include "utcp.h"
#include "utcp_packet.h"
#include "utcp_utils.h"
#include <assert.h>
#include <string.h>

static inline int32_t GetAdjustedSizeBits(int32_t InSizeBits)
{
	return InSizeBits;
}

// StatelessConnectHandlerComponent::GenerateCookie
extern void sha1_hmac_buffer(const void* Key, uint32_t KeySize, const void* Data, uint64_t DataSize, uint8_t* OutHash);
static void GenerateCookie(struct utcp_listener* fd, const char* ClientAddress, uint8_t SecretId, double Timestamp, uint8_t* OutCookie)
{
	size_t ClientAddressLen = strlen(ClientAddress);
	uint8_t CookieData[sizeof(double) + sizeof(int32_t) + ADDRSTR_PORT_SIZE];
	size_t Offset = 0;

	memcpy(CookieData + Offset, &Timestamp, sizeof(Timestamp));
	Offset += sizeof(Timestamp);
	memcpy(CookieData + Offset, &ClientAddressLen, sizeof(ClientAddressLen));
	Offset += sizeof(ClientAddressLen);
	memcpy(CookieData + Offset, ClientAddress, ClientAddressLen);
	Offset += ClientAddressLen;

	sha1_hmac_buffer(fd->HandshakeSecret[!!SecretId], SECRET_BYTE_SIZE, CookieData, Offset, OutCookie);
}

// StatelessConnectHandlerComponent::CapHandshakePacket
void CapHandshakePacket(struct bitbuf* bitbuf)
{
	size_t NumBits = bitbuf->num - GetAdjustedSizeBits(0);
	assert(NumBits == HANDSHAKE_PACKET_SIZE_BITS);
	// Add a termination bit, the same as the UNetConnection code does
	bitbuf_write_end(bitbuf);
}

// StatelessConnectHandlerComponent::SendConnectChallenge
static void SendConnectChallenge(struct utcp_listener* fd, const char* address)
{
	// GetAdjustedSizeBits(HANDSHAKE_PACKET_SIZE_BITS) + 1 /* Termination bit */
	uint8_t buffer[UTCP_MAX_PACKET];
	struct bitbuf bitbuf;
	if (!bitbuf_write_init(&bitbuf, buffer, sizeof(buffer)))
		return;

	uint8_t bHandshakePacket = 1;
	float Timestamp = utcp_gettime();
	uint8_t Cookie[COOKIE_BYTE_SIZE];

	GenerateCookie(fd, address, fd->ActiveSecret, Timestamp, Cookie);

	bitbuf_write_bit(&bitbuf, bHandshakePacket);
	bitbuf_write_bit(&bitbuf, fd->ActiveSecret);
	bitbuf_write_bytes(&bitbuf, &Timestamp, sizeof(Timestamp));
	bitbuf_write_bytes(&bitbuf, Cookie, sizeof(Cookie));

	CapHandshakePacket(&bitbuf);

	utcp_listener_outgoing(fd, bitbuf.buffer, bitbuf_num_bytes(&bitbuf));
}

// StatelessConnectHandlerComponent::SendChallengeAck
static void SendChallengeAck(struct utcp_listener* listener_fd, struct utcp_connection* fd, uint8_t InCookie[COOKIE_BYTE_SIZE])
{
	// GetAdjustedSizeBits(HANDSHAKE_PACKET_SIZE_BITS) + 1 /* Termination bit */
	uint8_t buffer[UTCP_MAX_PACKET];
	struct bitbuf bitbuf;
	if (!bitbuf_write_init(&bitbuf, buffer, sizeof(buffer)))
		return;

	uint8_t bHandshakePacket = 1;
	float Timestamp = -1.0;

	bitbuf_write_bit(&bitbuf, bHandshakePacket);
	bitbuf_write_bit(&bitbuf, bHandshakePacket); // ActiveSecret

	bitbuf_write_bytes(&bitbuf, &Timestamp, sizeof(Timestamp));
	bitbuf_write_bytes(&bitbuf, InCookie, COOKIE_BYTE_SIZE);

	CapHandshakePacket(&bitbuf);

	if (listener_fd)
		utcp_listener_outgoing(listener_fd, bitbuf.buffer, bitbuf_num_bytes(&bitbuf));
	else
		utcp_connection_outgoing(fd, bitbuf.buffer, bitbuf_num_bytes(&bitbuf));
}

// StatelessConnectHandlerComponent::ParseHandshakePacket
static bool ParseHandshakePacket(struct bitbuf* bitbuf, uint8_t* OutSecretId, float* OutTimestamp, uint8_t* OutCookie)
{
	// Only accept handshake packets of precisely the right size
	if (bitbuf_left_bits(bitbuf) != (HANDSHAKE_PACKET_SIZE_BITS - 1))
		return false;

	if (!bitbuf_read_bit(bitbuf, OutSecretId))
		return false;

	if (!bitbuf_read_bytes(bitbuf, OutTimestamp, sizeof(*OutTimestamp)))
		return false;

	if (!bitbuf_read_bytes(bitbuf, OutCookie, COOKIE_BYTE_SIZE))
		return false;

	return true;
}

// StatelessConnectHandlerComponent::IncomingConnectionless
static int IncomingConnectionless(struct utcp_listener* fd, const char* address, struct bitbuf* bitbuf)
{
	uint8_t bHandshakePacket;
	if (!bitbuf_read_bit(bitbuf, &bHandshakePacket))
		return -2;

	if (!bHandshakePacket)
		return -3;

	uint8_t SecretId = 0;
	float Timestamp = 1.0;
	uint8_t Cookie[COOKIE_BYTE_SIZE];

	bHandshakePacket = ParseHandshakePacket(bitbuf, &SecretId, &Timestamp, Cookie);
	if (!bHandshakePacket)
		return -4;

	bool bInitialConnect = Timestamp == 0.f;
	if (bInitialConnect)
	{
		SendConnectChallenge(fd, address);
		return 0;
	}

	// NOTE: Allow CookieDelta to be 0.0, as it is possible for a server to send a challenge and receive a response,
	//			during the same tick
	bool bChallengeSuccess = false;
	const float CookieDelta = utcp_gettime() - Timestamp;
	const float SecretDelta = Timestamp - fd->LastSecretUpdateTimestamp;
	const bool bValidCookieLifetime = CookieDelta >= 0.0 && (MAX_COOKIE_LIFETIME - CookieDelta) > 0.0;
	const bool bValidSecretIdTimestamp = (SecretId == fd->ActiveSecret) ? (SecretDelta >= 0.0) : (SecretDelta <= 0.0);

	if (bValidCookieLifetime && bValidSecretIdTimestamp)
	{
		// Regenerate the cookie from the packet info, and see if the received cookie matches the regenerated one
		uint8_t RegenCookie[COOKIE_BYTE_SIZE];

		GenerateCookie(fd, address, SecretId, Timestamp, RegenCookie);

		bChallengeSuccess = memcmp(Cookie, RegenCookie, COOKIE_BYTE_SIZE) == 0;

		if (bChallengeSuccess)
		{
			int16_t* CurSequence = (int16_t*)Cookie;

			fd->LastServerSequence = *CurSequence & (UTCP_MAX_PACKETID - 1);
			fd->LastClientSequence = *(CurSequence + 1) & (UTCP_MAX_PACKETID - 1);

			memcpy(fd->AuthorisedCookie, Cookie, sizeof(fd->AuthorisedCookie));
			strncpy(fd->LastChallengeSuccessAddress, address, sizeof(fd->LastChallengeSuccessAddress));

			// Now ack the challenge response - the cookie is stored in AuthorisedCookie, to enable retries
			SendChallengeAck(fd, NULL, fd->AuthorisedCookie);
		}
		return 0;
	}
	return -6;
}

// StatelessConnectHandlerComponent::HasPassedChallenge
static bool HasPassedChallenge(struct utcp_listener* fd, const char* address, bool* bOutRestartedHandshake)
{
	*bOutRestartedHandshake = fd->bRestartedHandshake;
	return strncmp(fd->LastChallengeSuccessAddress, address, sizeof(fd->LastChallengeSuccessAddress)) == 0;
}

// StatelessConnectHandlerComponent::ResetChallengeData
static void ResetChallengeData(struct utcp_listener* fd)
{
	fd->LastChallengeSuccessAddress[0] = '\0';
	fd->bRestartedHandshake = false;
	fd->LastServerSequence = 0;
	fd->LastClientSequence = 0;
	memset(fd->AuthorisedCookie, 0, COOKIE_BYTE_SIZE);
}

// UIpNetDriver::ProcessConnectionlessPacket
int process_connectionless_packet(struct utcp_listener* fd, const char* address, const uint8_t* buffer, int len)
{
	struct bitbuf bitbuf;
	if (!bitbuf_read_init(&bitbuf, buffer, len))
	{
		return -1;
	}

	int ret = IncomingConnectionless(fd, address, &bitbuf);
	if (ret)
	{
		return ret;
	}

	assert(bitbuf.num == bitbuf.size);

	bool bPassedChallenge = false;
	bool bRestartedHandshake = false;
	// bool bIgnorePacket = true;

	bPassedChallenge = HasPassedChallenge(fd, address, &bRestartedHandshake);
	if (bPassedChallenge)
	{
		if (bRestartedHandshake)
		{
			utcp_on_accept(fd, true);
		}

		// bIgnorePacket = false
	}
	if (bPassedChallenge)
	{
		if (!bRestartedHandshake)
		{
			utcp_on_accept(fd, false);
		}
		ResetChallengeData(fd);
	}
	return 0;
}

// StatelessConnectHandlerComponent::NotifyHandshakeBegin
static void NotifyHandshakeBegin(struct utcp_connection* fd)
{
	if (!is_client(fd))
		return;

	// GetAdjustedSizeBits(fd, HANDSHAKE_PACKET_SIZE_BITS) + 1
	uint8_t buffer[UTCP_MAX_PACKET];
	struct bitbuf bitbuf;
	if (!bitbuf_write_init(&bitbuf, buffer, sizeof(buffer)))
		return;

	uint8_t bHandshakePacket = 1;
	// In order to prevent DRDoS reflection amplification attacks, clients must pad the packet to match server packet size
	uint8_t SecretIdPad = 0;
	uint8_t PacketSizeFiller[24];

	bitbuf_write_bit(&bitbuf, bHandshakePacket);
	bitbuf_write_bit(&bitbuf, SecretIdPad);

	memset(PacketSizeFiller, 0, sizeof(PacketSizeFiller));
	bitbuf_write_bytes(&bitbuf, PacketSizeFiller, sizeof(PacketSizeFiller));

	CapHandshakePacket(&bitbuf);

	utcp_connection_outgoing(fd, bitbuf.buffer, bitbuf_num_bytes(&bitbuf));
	fd->challenge_data->LastClientSendTimestamp = utcp_gettime_ms();
}

void handshake_begin(struct utcp_connection* fd)
{
	NotifyHandshakeBegin(fd);
}

// StatelessConnectHandlerComponent::SendChallengeResponse
static void SendChallengeResponse(struct utcp_connection* fd, uint8_t InSecretId, float InTimestamp, uint8_t InCookie[COOKIE_BYTE_SIZE])
{
	// int32_t RestartHandshakeResponseSize = RESTART_RESPONSE_SIZE_BITS;
	// const int32 BaseSize = GetAdjustedSizeBits(fd->bRestartedHandshake ? RestartHandshakeResponseSize : HANDSHAKE_PACKET_SIZE_BITS);
	// BaseSize + 1 /* Termination bit */
	uint8_t buffer[UTCP_MAX_PACKET];
	struct bitbuf bitbuf;
	if (!bitbuf_write_init(&bitbuf, buffer, sizeof(buffer)))
		return;

	uint8_t bHandshakePacket = 1;

	bitbuf_write_bit(&bitbuf, bHandshakePacket);
	bitbuf_write_bit(&bitbuf, InSecretId);

	bitbuf_write_bytes(&bitbuf, &InTimestamp, sizeof(InTimestamp));
	bitbuf_write_bytes(&bitbuf, InCookie, COOKIE_BYTE_SIZE);

	CapHandshakePacket(&bitbuf);
	utcp_connection_outgoing(fd, bitbuf.buffer, bitbuf_num_bytes(&bitbuf));

	int16_t* CurSequence = (int16_t*)InCookie;

	fd->challenge_data->LastClientSendTimestamp = utcp_gettime_ms();
	fd->challenge_data->LastSecretId = InSecretId;
	fd->challenge_data->LastTimestamp = InTimestamp;

	memcpy(fd->challenge_data->LastCookie, InCookie, sizeof(fd->AuthorisedCookie));
}

// HandlerComponent::SetState
static void SetState(struct utcp_connection* fd, enum utcp_challenge_state state)
{
	fd->challenge_data->state = state;
}

// void StatelessConnectHandlerComponent::Incoming(FBitReader& Packet)
int handshake_incoming(struct utcp_connection* fd, struct bitbuf* bitbuf)
{
	uint8_t bHandshakePacket;
	if (!bitbuf_read_bit(bitbuf, &bHandshakePacket))
		return -1;

	if (!bHandshakePacket)
		return 0;

	uint8_t SecretId = 0;
	float Timestamp = 1.0;
	uint8_t Cookie[COOKIE_BYTE_SIZE];

	bHandshakePacket = ParseHandshakePacket(bitbuf, &SecretId, &Timestamp, Cookie);
	if (!bHandshakePacket)
		return -2;

	if (!is_client(fd))
	{
		// TODO
		// Servers should wipe LastChallengeSuccessAddress shortly after the first non-handshake packet is received by the client,
		// in order to disable challenge ack resending

		// The server should not be receiving handshake packets at this stage - resend the ack in case it was lost.
		// In this codepath, this component is linked to a UNetConnection, and the Last* values below, cache the handshake info.
		SendChallengeAck(NULL, fd, fd->AuthorisedCookie);

		return 0;
	}

	if (fd->challenge_data->state == UnInitialized || fd->challenge_data->state == InitializedOnLocal)
	{
		// Receiving challenge, verify the timestamp is > 0.0f
		if (Timestamp > 0.0f)
		{
			fd->challenge_data->LastChallengeTimestamp = utcp_gettime_ms();

			SendChallengeResponse(fd, SecretId, Timestamp, Cookie);

			// Utilize this state as an intermediary, indicating that the challenge response has been sent
			SetState(fd, InitializedOnLocal);
		}
		// Receiving challenge ack, verify the timestamp is < 0.0f
		else if (Timestamp < 0.0f)
		{
			// Extract the initial packet sequence from the random Cookie data
			int16_t* CurSequence = (int16_t*)Cookie;
			int32_t LastServerSequence = *CurSequence & (UTCP_MAX_PACKETID - 1);
			int32_t LastClientSequence = *(CurSequence + 1) & (UTCP_MAX_PACKETID - 1);

			utcp_sequence_init(fd, LastServerSequence, LastClientSequence);
			// Save the final authorized cookie
			memcpy(fd->AuthorisedCookie, Cookie, sizeof(fd->AuthorisedCookie));

			// Now finish initializing the handler - flushing the queued packet buffer in the process.
			SetState(fd, Initialized);
			utcp_on_connect(fd, false);
		}
	}
	else
	{
		// Ignore, could be a dupe/out-of-order challenge packet
	}

	return 0;
}

// StatelessConnectHandlerComponent::Tick
void handshake_update(struct utcp_connection* fd)
{
	if (fd->challenge_data == NULL || fd->challenge_data->LastClientSendTimestamp == 0)
	{
		return;
	}

	int64_t now = utcp_gettime_ms();
	int64_t LastSendTimeDiff = now - fd->challenge_data->LastClientSendTimestamp;
	if (LastSendTimeDiff < 500)
	{
		return;
	}

	const bool bRestartChallenge = now - fd->challenge_data->LastChallengeTimestamp > MIN_COOKIE_LIFETIME * 1000;
	if (bRestartChallenge)
	{
		SetState(fd, UnInitialized);
	}

	if (fd->challenge_data->state == UnInitialized)
	{
		NotifyHandshakeBegin(fd);
	}
	else if (fd->challenge_data->state == InitializedOnLocal && fd->challenge_data->LastTimestamp != 0.0)
	{
		SendChallengeResponse(fd, fd->challenge_data->LastSecretId, fd->challenge_data->LastTimestamp, fd->challenge_data->LastCookie);
	}
}
