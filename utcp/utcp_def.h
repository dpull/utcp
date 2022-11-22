// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "utcp_bunch_def.h"
#include "utcp_packet_notify_def.h"

enum
{
	PACKET_ID_INDEX_NONE = -1,
	HANDSHAKE_PACKET_SIZE_BITS = 227,
	RESTART_HANDSHAKE_PACKET_SIZE_BITS = 2,
	RESTART_RESPONSE_SIZE_BITS = 387,
	SECRET_BYTE_SIZE = 64,
	SECRET_COUNT = 2,
	COOKIE_BYTE_SIZE = 20,
};

#define SECRET_UPDATE_TIME 15.f
#define SECRET_UPDATE_TIME_VARIANCE 5.f

// The maximum allowed lifetime (in seconds) of any one handshake cookie
#define MAX_COOKIE_LIFETIME ((SECRET_UPDATE_TIME + SECRET_UPDATE_TIME_VARIANCE) * (float)SECRET_COUNT)

// The minimum amount of possible time a cookie may exist (for calculating when the clientside should timeout a challenge response)
#define MIN_COOKIE_LIFETIME SECRET_UPDATE_TIME

enum utcp_state
{
	UnInitialized,		// HandlerComponent not yet initialized
	InitializedOnLocal, // Initialized on local instance
	InitializeOnRemote, // Initialized on remote instance, not on local instance
	Initialized			// Initialized on both local and remote instances
};

enum utcp_mode
{
	Client, // Clientside PacketHandler
	Server	// Serverside PacketHandler
};

struct utcp_fd
{
	void* userdata;

	// 握手相关
	enum utcp_mode mode;
	enum utcp_state state;

	/** Whether or not component handshaking has begun */
	uint8_t bBeganHandshaking : 1;

	/** Client: Whether or not we are in the middle of a restarted handshake. Server: Whether or not the last handshake was a restarted handshake. */
	uint8_t bRestartedHandshake : 1;

	/** The serverside-only 'secret' value, used to help with generating cookies. */
	uint8_t HandshakeSecret[SECRET_BYTE_SIZE][SECRET_COUNT];

	/** Which of the two secret values above is active (values are changed frequently, to limit replay attacks) */
	uint8_t ActiveSecret;

	/** The time of the last secret value update */
	double LastSecretUpdateTimestamp;

	/** The local (client) time at which the challenge was last updated */
	int64_t LastChallengeTimestamp;

	/** The last time a handshake packet was sent - used for detecting failed sends. */
	int64_t LastClientSendTimestamp;

	/** The Timestamp value of the last challenge response sent */
	double LastTimestamp;

	/** The cookie which completed the connection handshake. */
	uint8_t AuthorisedCookie[COOKIE_BYTE_SIZE];

	/** The initial server sequence value, from the last successful handshake */
	int32_t LastServerSequence;

	/** The initial client sequence value, from the last successful handshake */
	int32_t LastClientSequence;

	char LastChallengeSuccessAddress[64]; // INET6_ADDRSTRLEN + PORT

	/** The SecretId value of the last challenge response sent */
	uint8_t LastSecretId;

	/** The Cookie value of the last challenge response sent. Will differ from AuthorisedCookie, if a handshake retry is triggered. */
	uint8_t LastCookie[COOKIE_BYTE_SIZE];

	int64_t LastReceiveRealtime; // Last time a packet was received, using real time seconds (FPlatformTime::Seconds)

	/** The local (client) time at which the last restart handshake request was received */
	int64_t LastRestartPacketTimestamp;

	// packet相关
	int32_t InPacketId;		// Full incoming packet index.
	int32_t OutPacketId;	// Most recently sent packet.
	int32_t OutAckPacketId; // Most recently acked outgoing packet.
	/** Full PacketId  of last sent packet that we have received notification for (i.e. we know if it was delivered or not). Related to OutAckPacketId which is
	 * tha last successfully delivered PacketId */
	int32_t LastNotifiedPacketId;

	struct packet_notify packet_notify;

	int32_t InitOutReliable;
	int32_t InitInReliable;
	struct utcp_channel* Channels[DEFAULT_MAX_CHANNEL_SIZE];

	/** Keep old behavior where we send a packet with only acks even if we have no other outgoing data if we got incoming data */
	uint32_t HasDirtyAcks;

	uint8_t SendBuffer[UTCP_MAX_PACKET + 32 /*MagicHeader*/ + 1 /*EndBits*/];
	size_t SendBufferBitsNum;

	int64_t LastSendTime; // Last time a packet was sent, for keepalives.

	/** Stores the bit number where we wrote the dummy packet info in the packet header */
	size_t HeaderMarkForPacketInfo;

	uint8_t AllowMerge; // Whether to allow merging.
};

struct utcp_config
{
	void (*on_accept)(struct utcp_fd* fd, void* userdata, bool reconnect);
	void (*on_raw_send)(struct utcp_fd* fd, void* userdata, const void* data, int len);
	void (*on_recv)(struct utcp_fd* fd, void* userdata, struct utcp_bunch  * const bunches[], int count);
	void (*on_delivery_status)(struct utcp_fd* fd, void* userdata, int32_t packet_id, bool ack);
	void (*on_log)(int level, const char* msg, va_list args);
	void* (*on_realloc)(void* ptr, size_t size);

	int64_t ElapsedTime;
	uint32_t MagicHeader;
	uint8_t MagicHeaderBits;
	uint8_t enable_debug_cookie;
	uint8_t debug_cookie[COOKIE_BYTE_SIZE];
};

#ifdef __cplusplus
}
#endif
