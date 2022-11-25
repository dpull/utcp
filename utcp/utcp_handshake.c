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

	utcp_outgoing(fd, bitbuf.buffer, bitbuf_num_bytes(&bitbuf));
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

	utcp_outgoing(fd, bitbuf.buffer, bitbuf_num_bytes(&bitbuf));
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
		utcp_outgoing(listener_fd, bitbuf.buffer, bitbuf_num_bytes(&bitbuf));
	else
		utcp_outgoing(fd, bitbuf.buffer, bitbuf_num_bytes(&bitbuf));
}

static bool ParseHandshakePacket(struct bitbuf* bitbuf, uint8_t* bOutRestartHandshake, uint8_t* OutSecretId, double* OutTimestamp, uint8_t* OutCookie, uint8_t* OutOrigCookie, bool bIsClient)
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

// StatelessConnectHandlerComponent::CapHandshakePacket
void CapHandshakePacket(struct bitbuf* bitbuf)
{
	size_t NumBits = bitbuf->num - GetAdjustedSizeBits(0);
	assert(NumBits == HANDSHAKE_PACKET_SIZE_BITS || NumBits == RESTART_HANDSHAKE_PACKET_SIZE_BITS || NumBits == RESTART_RESPONSE_SIZE_BITS);
	// Add a termination bit, the same as the UNetConnection code does
	bitbuf_write_end(bitbuf);
}

// StatelessConnectHandlerComponent::Outgoing
int Outgoing(struct bitbuf* bitbuf)
{
	assert(bitbuf->num == 0);
	write_magic_header(bitbuf);

	uint8_t bHandshakePacket = 0;
	bitbuf_write_bit(bitbuf, bHandshakePacket);
	return 0;
}

// StatelessConnectHandlerComponent::IncomingConnectionless
int IncomingConnectionless(struct utcp_listener* fd, const char* address, struct bitbuf* bitbuf)
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

bool HasPassedChallenge(struct utcp_listener* fd, const char* address, bool* bOutRestartedHandshake)
{
	*bOutRestartedHandshake = fd->bRestartedHandshake;
	return strncmp(fd->LastChallengeSuccessAddress, address, sizeof(fd->LastChallengeSuccessAddress)) == 0;
}

void ResetChallengeData(struct utcp_listener* fd)
{
	fd->LastChallengeSuccessAddress[0] = '\0';
	fd->bRestartedHandshake = false;
	fd->LastServerSequence = 0;
	fd->LastClientSequence = 0;
	memset(fd->AuthorisedCookie, 0, COOKIE_BYTE_SIZE);
}

// StatelessConnectHandlerComponent::NotifyHandshakeBegin
void NotifyHandshakeBegin(struct utcp_connection* fd)
{
	if (fd->mode != Client)
		return;

	// GetAdjustedSizeBits(fd, HANDSHAKE_PACKET_SIZE_BITS) + 1
	uint8_t buffer[UTCP_MAX_PACKET];
	struct bitbuf bitbuf;
	if (!bitbuf_write_init(&bitbuf, buffer, sizeof(buffer)))
		return;

	write_magic_header(&bitbuf);

	uint8_t bHandshakePacket = 1;
	// In order to prevent DRDoS reflection amplification attacks, clients must pad the packet to match server packet size
	uint8_t bRestartHandshake = fd->bRestartedHandshake ? 1 : 0;
	uint8_t SecretIdPad = 0;
	uint8_t PacketSizeFiller[28];

	bitbuf_write_bit(&bitbuf, bHandshakePacket);
	bitbuf_write_bit(&bitbuf, bRestartHandshake);
	bitbuf_write_bit(&bitbuf, SecretIdPad);

	memset(PacketSizeFiller, 0, sizeof(PacketSizeFiller));
	bitbuf_write_bytes(&bitbuf, PacketSizeFiller, sizeof(PacketSizeFiller));

	CapHandshakePacket(&bitbuf);

	utcp_outgoing(fd, bitbuf.buffer, bitbuf_num_bytes(&bitbuf));
	fd->LastClientSendTimestamp = utcp_gettime_ms();
}

// StatelessConnectHandlerComponent::SendChallengeResponse
void SendChallengeResponse(struct utcp_connection* fd, uint8_t InSecretId, double InTimestamp, uint8_t InCookie[COOKIE_BYTE_SIZE])
{
	// int32_t RestartHandshakeResponseSize = RESTART_RESPONSE_SIZE_BITS;
	// const int32 BaseSize = GetAdjustedSizeBits(fd->bRestartedHandshake ? RestartHandshakeResponseSize : HANDSHAKE_PACKET_SIZE_BITS);
	// BaseSize + 1 /* Termination bit */
	uint8_t buffer[UTCP_MAX_PACKET];
	struct bitbuf bitbuf;
	if (!bitbuf_write_init(&bitbuf, buffer, sizeof(buffer)))
		return;

	uint8_t bHandshakePacket = 1;
	uint8_t bRestartHandshake = (fd->bRestartedHandshake ? 1 : 0);

	write_magic_header(&bitbuf);

	bitbuf_write_bit(&bitbuf, bHandshakePacket);
	bitbuf_write_bit(&bitbuf, bRestartHandshake);
	bitbuf_write_bit(&bitbuf, InSecretId);

	bitbuf_write_bytes(&bitbuf, &InTimestamp, sizeof(InTimestamp));
	bitbuf_write_bytes(&bitbuf, InCookie, COOKIE_BYTE_SIZE);

	if (fd->bRestartedHandshake)
	{
		bitbuf_write_bytes(&bitbuf, fd->AuthorisedCookie, COOKIE_BYTE_SIZE);
	}

	CapHandshakePacket(&bitbuf);
	utcp_outgoing(fd, bitbuf.buffer, bitbuf_num_bytes(&bitbuf));

	int16_t* CurSequence = (int16_t*)InCookie;

	fd->LastClientSendTimestamp = utcp_gettime_ms();
	fd->LastSecretId = InSecretId;
	fd->LastTimestamp = InTimestamp;
	fd->LastServerSequence = *CurSequence & (MAX_PACKETID - 1);
	fd->LastClientSequence = *(CurSequence + 1) & (MAX_PACKETID - 1);

	memcpy(fd->LastCookie, InCookie, sizeof(fd->AuthorisedCookie));
}

// void StatelessConnectHandlerComponent::Incoming(FBitReader& Packet)
int Incoming(struct utcp_connection* fd, struct bitbuf* bitbuf)
{
	read_magic_header(bitbuf);

	uint8_t bHandshakePacket;
	if (!bitbuf_read_bit(bitbuf, &bHandshakePacket))
		return -2;

	if (bHandshakePacket)
	{
		uint8_t bRestartHandshake = false;
		uint8_t SecretId = 0;
		double Timestamp = 1.0;
		uint8_t Cookie[COOKIE_BYTE_SIZE];
		uint8_t OrigCookie[COOKIE_BYTE_SIZE];

		bHandshakePacket = ParseHandshakePacket(bitbuf, &bRestartHandshake, &SecretId, &Timestamp, Cookie, OrigCookie, fd->mode == Client);
		if (!bHandshakePacket)
		{
			return -4;
		}

		if (fd->mode == Client)
		{
			if (fd->state == UnInitialized || fd->state == InitializedOnLocal)
			{
				if (bRestartHandshake)
				{
					// TODO log
					// "Ignoring restart handshake request, while already restarted."
				}
				// Receiving challenge, verify the timestamp is > 0.0f
				else if (Timestamp > 0.0)
				{
					fd->LastChallengeTimestamp = utcp_gettime_ms();

					SendChallengeResponse(fd, SecretId, Timestamp, Cookie);

					// Utilize this state as an intermediary, indicating that the challenge response has been sent
					utcp_set_state(fd, InitializedOnLocal);
				}
				// Receiving challenge ack, verify the timestamp is < 0.0f
				else if (Timestamp < 0.0)
				{
					if (!fd->bRestartedHandshake)
					{
						// Extract the initial packet sequence from the random Cookie data
						int16_t* CurSequence = (int16_t*)Cookie;
						fd->LastServerSequence = *CurSequence & (MAX_PACKETID - 1);
						fd->LastClientSequence = *(CurSequence + 1) & (MAX_PACKETID - 1);

						utcp_sequence_init(fd, fd->LastServerSequence, fd->LastClientSequence);
						// Save the final authorized cookie
						memcpy(fd->AuthorisedCookie, Cookie, sizeof(fd->AuthorisedCookie));
					}

					// Now finish initializing the handler - flushing the queued packet buffer in the process.
					utcp_set_state(fd, Initialized);
					utcp_on_connect(fd, fd->bRestartedHandshake);
					// TODO 连接成功发包 PacketHandler::HandlerComponentInitialized  -->PacketHandler::HandlerInitialized(通知业务层发送
					// UPendingNetGame::SendInitialJoin) Initialized();
					fd->bRestartedHandshake = false;
				}
			}
			else if (bRestartHandshake)
			{
				uint8_t ZeroCookie[COOKIE_BYTE_SIZE] = {0};
				bool bValidAuthCookie = memcpy(fd->AuthorisedCookie, ZeroCookie, COOKIE_BYTE_SIZE) != 0;

				// The server has requested us to restart the handshake process - this is because
				// it has received traffic from us on a different address than before.
				if (bValidAuthCookie)
				{
					bool bPassedDelayCheck = false;
					bool bPassedDualIPCheck = false;
					int64_t CurrentTime = utcp_gettime_ms();

					if (!fd->bRestartedHandshake)
					{
						// The server may send multiple restart handshake packets, so have a 10 second delay between accepting them
						bPassedDelayCheck = (CurrentTime - fd->LastClientSendTimestamp) > 10 * 1000;

						// Some clients end up sending packets duplicated over multiple IP's, triggering the restart handshake.
						// Detect this by checking if any restart handshake requests have been received in roughly the last second
						// (Dual IP situations will make the server send them constantly) - and override the checks as a failsafe,
						// if no NetConnection packets have been received in the last second.
						int64_t LastRestartPacketTimeDiff = CurrentTime - fd->LastRestartPacketTimestamp;
						int64_t LastNetConnPacketTimeDiff = CurrentTime - fd->LastReceiveRealtime;

						bPassedDualIPCheck = fd->LastRestartPacketTimestamp == 0 || LastRestartPacketTimeDiff > 1100 || LastNetConnPacketTimeDiff > 1000;
					}

					fd->LastRestartPacketTimestamp = CurrentTime;
					if (!fd->bRestartedHandshake && bPassedDelayCheck && bPassedDualIPCheck)
					{
						// UE_LOG(LogHandshake, Log, TEXT("Beginning restart handshake process."));

						fd->bRestartedHandshake = true;
						utcp_set_state(fd, UnInitialized);
						NotifyHandshakeBegin(fd);
					}
					else
					{
						if (fd->bRestartedHandshake)
						{
							// UE_LOG(LogHandshake, Log, TEXT("Ignoring restart handshake request, while already restarted (this is normal)."));
						}
					}
				}
				else
				{
					// UE_LOG(LogHandshake, Log, TEXT("Server sent restart handshake request, when we don't have an authorised cookie."));
					return -5;
				}
			}
			else
			{
				// Ignore, could be a dupe/out-of-order challenge packet
			}
		}
		else if (fd->mode == Server)
		{
			if (fd->LastChallengeSuccessAddress)
			{
				// The server should not be receiving handshake packets at this stage - resend the ack in case it was lost.
				// In this codepath, this component is linked to a UNetConnection, and the Last* values below, cache the handshake info.
				SendChallengeAck(NULL, fd, fd->AuthorisedCookie);
			}
		}
	}
	// Servers should wipe LastChallengeSuccessAddress shortly after the first non-handshake packet is received by the client,
	// in order to disable challenge ack resending
	/*
	* TODO 根据语意, 这段代码永远不会执行, 是我没理解对吗?
	else if (fd->LastInitTimestamp != 0.0 && LastChallengeSuccessAddress.IsValid() && Handler->Mode == Handler::Mode::Server) {
		// Restart handshakes require extra time before disabling challenge ack resends, as NetConnection packets will already be in flight


		const double RestartHandshakeAckResendWindow = 10.0;
		double CurTime = Driver != nullptr ? Driver->GetElapsedTime() : 0.0;

		if (LastInitTimestamp - CurTime >= RestartHandshakeAckResendWindow) {
			LastChallengeSuccessAddress.Reset();
			LastInitTimestamp = 0.0;
		}
	}
	*/
	return 0;
}

// UNetConnection::InitSequence
void utcp_sequence_init(struct utcp_connection* fd, int32_t IncomingSequence, int32_t OutgoingSequence)
{
	fd->InPacketId = IncomingSequence - 1;
	fd->OutPacketId = OutgoingSequence;
	fd->OutAckPacketId = OutgoingSequence - 1;
	fd->LastNotifiedPacketId = fd->OutAckPacketId;

	// Initialize the reliable packet sequence (more useful/effective at preventing attacks)
	fd->InitInReliable = IncomingSequence & (UTCP_MAX_CHSEQUENCE - 1);
	fd->InitOutReliable = OutgoingSequence & (UTCP_MAX_CHSEQUENCE - 1);

	packet_notify_Init(&fd->packet_notify, seq_num_init(fd->InPacketId), seq_num_init(fd->OutPacketId));
}