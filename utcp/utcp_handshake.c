#include "utcp_handshake.h"
#include "bit_buffer.h"
#include "utcp.h"
#include "utcp_packet.h"
#include "utcp_sequence_number.h"
#include "utcp_utils.h"
#include <assert.h>
#include <string.h>

enum
{
	MAX_PACKETID = SeqNumberCount,
};

static inline int32_t GetAdjustedSizeBits(int32_t InSizeBits)
{
	struct utcp_config* utcp_config = utcp_get_config();
	return utcp_config->MagicHeaderBits + InSizeBits;
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
	assert(NumBits == HANDSHAKE_PACKET_SIZE_BITS || NumBits == RESTART_HANDSHAKE_PACKET_SIZE_BITS || NumBits == RESTART_RESPONSE_SIZE_BITS);
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
	uint8_t bRestartHandshake = 0; // Ignored clientside
	double Timestamp = utcp_gettime();
	uint8_t Cookie[COOKIE_BYTE_SIZE];

	GenerateCookie(fd, address, fd->ActiveSecret, Timestamp, Cookie);

	write_magic_header(&bitbuf);

	bitbuf_write_bit(&bitbuf, bHandshakePacket);
	bitbuf_write_bit(&bitbuf, bRestartHandshake);
	bitbuf_write_bit(&bitbuf, fd->ActiveSecret);
	bitbuf_write_bytes(&bitbuf, &Timestamp, sizeof(Timestamp));
	bitbuf_write_bytes(&bitbuf, Cookie, sizeof(Cookie));

	CapHandshakePacket(&bitbuf);

	utcp_listener_outgoing(fd, bitbuf.buffer, bitbuf_num_bytes(&bitbuf));
}

// StatelessConnectHandlerComponent::SendRestartHandshakeRequest
static void SendRestartHandshakeRequest(struct utcp_listener* fd)
{
	// GetAdjustedSizeBits(RESTART_HANDSHAKE_PACKET_SIZE_BITS) + 1 /* Termination bit */
	uint8_t buffer[UTCP_MAX_PACKET];
	struct bitbuf bitbuf;
	if (!bitbuf_write_init(&bitbuf, buffer, sizeof(buffer)))
		return;

	uint8_t bHandshakePacket = 1;
	uint8_t bRestartHandshake = 1;

	write_magic_header(&bitbuf);

	bitbuf_write_bit(&bitbuf, bHandshakePacket);
	bitbuf_write_bit(&bitbuf, bRestartHandshake);

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
	uint8_t bRestartHandshake = 0; // Ignored clientside
	double Timestamp = -1.0;

	write_magic_header(&bitbuf);

	bitbuf_write_bit(&bitbuf, bHandshakePacket);
	bitbuf_write_bit(&bitbuf, bRestartHandshake);
	bitbuf_write_bit(&bitbuf, bHandshakePacket); // ActiveSecret

	bitbuf_write_bytes(&bitbuf, &Timestamp, sizeof(Timestamp));
	bitbuf_write_bytes(&bitbuf, InCookie, COOKIE_BYTE_SIZE);

	CapHandshakePacket(&bitbuf);

	if (listener_fd)
		utcp_listener_outgoing(listener_fd, bitbuf.buffer, bitbuf_num_bytes(&bitbuf));
	else
		utcp_connection_outgoing(fd, bitbuf.buffer, bitbuf_num_bytes(&bitbuf));
}

static bool ParseHandshakePacket(struct bitbuf* bitbuf, uint8_t* bOutRestartHandshake, uint8_t* OutSecretId, double* OutTimestamp, uint8_t* OutCookie, uint8_t* OutOrigCookie,
								 bool bIsClient)
{
	bool bValidPacket = false;
	size_t BitsLeft = bitbuf->size - bitbuf->num;
	bool bHandshakePacketSize = BitsLeft == (HANDSHAKE_PACKET_SIZE_BITS - 1);
	bool bRestartResponsePacketSize = BitsLeft == (RESTART_RESPONSE_SIZE_BITS - 1);
	bool bRestartResponseDiagnosticsPacketSize = false;

	// Only accept handshake packets of precisely the right size
	if (bHandshakePacketSize || bRestartResponsePacketSize || bRestartResponseDiagnosticsPacketSize)
	{
		if (!bitbuf_read_bit(bitbuf, bOutRestartHandshake))
			return false;

		if (!bitbuf_read_bit(bitbuf, OutSecretId))
			return false;

		if (!bitbuf_read_bytes(bitbuf, OutTimestamp, sizeof(*OutTimestamp)))
			return false;

		if (!bitbuf_read_bytes(bitbuf, OutCookie, COOKIE_BYTE_SIZE))
			return false;

		if (bRestartResponsePacketSize || bRestartResponseDiagnosticsPacketSize)
		{
			if (!bitbuf_read_bytes(bitbuf, OutOrigCookie, COOKIE_BYTE_SIZE))
				return false;
		}
		bValidPacket = true;
	}
	else if (BitsLeft == (RESTART_HANDSHAKE_PACKET_SIZE_BITS - 1))
	{
		if (!bitbuf_read_bit(bitbuf, bOutRestartHandshake))
			return false;
		bValidPacket = bOutRestartHandshake && bIsClient;
	}

	return bValidPacket;
}

// StatelessConnectHandlerComponent::IncomingConnectionless
static int IncomingConnectionless(struct utcp_listener* fd, const char* address, struct bitbuf* bitbuf)
{
	read_magic_header(bitbuf);

	uint8_t bHandshakePacket;
	if (!bitbuf_read_bit(bitbuf, &bHandshakePacket))
		return -2;

	if (!bHandshakePacket)
	{
		SendRestartHandshakeRequest(fd);
		return -3;
	}

	uint8_t bRestartHandshake = false;
	uint8_t SecretId = 0;
	double Timestamp = 1.0;
	uint8_t Cookie[COOKIE_BYTE_SIZE];
	uint8_t OrigCookie[COOKIE_BYTE_SIZE];

	bHandshakePacket = ParseHandshakePacket(bitbuf, &bRestartHandshake, &SecretId, &Timestamp, Cookie, OrigCookie, false);
	if (!bHandshakePacket)
	{
		return -4;
	}

	bool bInitialConnect = Timestamp == 0.0;
	if (bInitialConnect)
	{
		SendConnectChallenge(fd, address);
		return 0;
	}
	// NOTE: Allow CookieDelta to be 0.0, as it is possible for a server to send a challenge and receive a response,
	//			during the same tick
	bool bChallengeSuccess = false;
	const double CookieDelta = utcp_gettime() - Timestamp;
	const double SecretDelta = Timestamp - fd->LastSecretUpdateTimestamp;
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
			if (bRestartHandshake)
			{
				memcpy(fd->AuthorisedCookie, OrigCookie, sizeof(fd->AuthorisedCookie));
			}
			else
			{
				int16_t* CurSequence = (int16_t*)Cookie;

				fd->LastServerSequence = *CurSequence & (MAX_PACKETID - 1);
				fd->LastClientSequence = *(CurSequence + 1) & (MAX_PACKETID - 1);

				memcpy(fd->AuthorisedCookie, Cookie, sizeof(fd->AuthorisedCookie));
			}

			fd->bRestartedHandshake = bRestartHandshake;
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

	write_magic_header(&bitbuf);

	uint8_t bHandshakePacket = 1;
	// In order to prevent DRDoS reflection amplification attacks, clients must pad the packet to match server packet size
	uint8_t bRestartHandshake = fd->challenge_data->bRestartedHandshake ? 1 : 0;
	uint8_t SecretIdPad = 0;
	uint8_t PacketSizeFiller[28];

	bitbuf_write_bit(&bitbuf, bHandshakePacket);
	bitbuf_write_bit(&bitbuf, bRestartHandshake);
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
static void SendChallengeResponse(struct utcp_connection* fd, uint8_t InSecretId, double InTimestamp, uint8_t InCookie[COOKIE_BYTE_SIZE])
{
	// int32_t RestartHandshakeResponseSize = RESTART_RESPONSE_SIZE_BITS;
	// const int32 BaseSize = GetAdjustedSizeBits(fd->bRestartedHandshake ? RestartHandshakeResponseSize : HANDSHAKE_PACKET_SIZE_BITS);
	// BaseSize + 1 /* Termination bit */
	uint8_t buffer[UTCP_MAX_PACKET];
	struct bitbuf bitbuf;
	if (!bitbuf_write_init(&bitbuf, buffer, sizeof(buffer)))
		return;

	uint8_t bHandshakePacket = 1;
	uint8_t bRestartHandshake = (fd->challenge_data->bRestartedHandshake ? 1 : 0);

	write_magic_header(&bitbuf);

	bitbuf_write_bit(&bitbuf, bHandshakePacket);
	bitbuf_write_bit(&bitbuf, bRestartHandshake);
	bitbuf_write_bit(&bitbuf, InSecretId);

	bitbuf_write_bytes(&bitbuf, &InTimestamp, sizeof(InTimestamp));
	bitbuf_write_bytes(&bitbuf, InCookie, COOKIE_BYTE_SIZE);

	if (fd->challenge_data->bRestartedHandshake)
	{
		bitbuf_write_bytes(&bitbuf, fd->AuthorisedCookie, COOKIE_BYTE_SIZE);
	}

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
	read_magic_header(bitbuf);

	uint8_t bHandshakePacket;
	if (!bitbuf_read_bit(bitbuf, &bHandshakePacket))
	{
		return -1;
	}

	if (!bHandshakePacket)
	{
		return 0;
	}

	uint8_t bRestartHandshake = false;
	uint8_t SecretId = 0;
	double Timestamp = 1.0;
	uint8_t Cookie[COOKIE_BYTE_SIZE];
	uint8_t OrigCookie[COOKIE_BYTE_SIZE];

	bHandshakePacket = ParseHandshakePacket(bitbuf, &bRestartHandshake, &SecretId, &Timestamp, Cookie, OrigCookie, is_client(fd));
	if (!bHandshakePacket)
	{
		return -2;
	}

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
		if (bRestartHandshake)
		{
			utcp_log(Log, "Ignoring restart handshake request, while already restarted.");
		}
		// Receiving challenge, verify the timestamp is > 0.0f
		else if (Timestamp > 0.0)
		{
			fd->challenge_data->LastChallengeTimestamp = utcp_gettime_ms();

			SendChallengeResponse(fd, SecretId, Timestamp, Cookie);

			// Utilize this state as an intermediary, indicating that the challenge response has been sent
			SetState(fd, InitializedOnLocal);
		}
		// Receiving challenge ack, verify the timestamp is < 0.0f
		else if (Timestamp < 0.0)
		{
			if (!fd->challenge_data->bRestartedHandshake)
			{
				// Extract the initial packet sequence from the random Cookie data
				int16_t* CurSequence = (int16_t*)Cookie;
				int32_t LastServerSequence = *CurSequence & (MAX_PACKETID - 1);
				int32_t LastClientSequence = *(CurSequence + 1) & (MAX_PACKETID - 1);

				utcp_sequence_init(fd, LastServerSequence, LastClientSequence);
				// Save the final authorized cookie
				memcpy(fd->AuthorisedCookie, Cookie, sizeof(fd->AuthorisedCookie));
			}

			// Now finish initializing the handler - flushing the queued packet buffer in the process.
			SetState(fd, Initialized);
			utcp_on_connect(fd, fd->challenge_data->bRestartedHandshake);
			fd->challenge_data->bRestartedHandshake = false;
		}
	}
	else if (bRestartHandshake)
	{
		uint8_t ZeroCookie[COOKIE_BYTE_SIZE] = {0};
		bool bValidAuthCookie = memcmp(fd->AuthorisedCookie, ZeroCookie, COOKIE_BYTE_SIZE) != 0;

		// The server has requested us to restart the handshake process - this is because
		// it has received traffic from us on a different address than before.
		if (bValidAuthCookie)
		{
			bool bPassedDelayCheck = false;
			bool bPassedDualIPCheck = false;
			int64_t CurrentTime = utcp_gettime_ms();

			if (!fd->challenge_data->bRestartedHandshake)
			{
				// The server may send multiple restart handshake packets, so have a 10 second delay between accepting them
				bPassedDelayCheck = (CurrentTime - fd->challenge_data->LastClientSendTimestamp) > 10 * 1000;

				// Some clients end up sending packets duplicated over multiple IP's, triggering the restart handshake.
				// Detect this by checking if any restart handshake requests have been received in roughly the last second
				// (Dual IP situations will make the server send them constantly) - and override the checks as a failsafe,
				// if no NetConnection packets have been received in the last second.
				int64_t LastRestartPacketTimeDiff = CurrentTime - fd->challenge_data->LastRestartPacketTimestamp;
				int64_t LastNetConnPacketTimeDiff = CurrentTime - fd->LastReceiveRealtime;

				bPassedDualIPCheck = fd->challenge_data->LastRestartPacketTimestamp == 0 || LastRestartPacketTimeDiff > 1100 || LastNetConnPacketTimeDiff > 1000;
			}

			fd->challenge_data->LastRestartPacketTimestamp = CurrentTime;
			if (!fd->challenge_data->bRestartedHandshake && bPassedDelayCheck && bPassedDualIPCheck)
			{
				// UE_LOG(LogHandshake, Log, TEXT("Beginning restart handshake process."));

				fd->challenge_data->bRestartedHandshake = true;
				SetState(fd, UnInitialized);
				NotifyHandshakeBegin(fd);
			}
			else
			{
				if (fd->challenge_data->bRestartedHandshake)
				{
					utcp_log(Log, "Ignoring restart handshake request, while already restarted (this is normal).");
				}
			}
		}
		else
		{
			utcp_log(Log, "Server sent restart handshake request, when we don't have an authorised cookie.");
			return -3;
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
	if (LastSendTimeDiff < 1000)
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
