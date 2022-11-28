// Copyright DPULL, Inc. All Rights Reserved.

#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "utcp_bunch_def.h"
#include "utcp_def.h"
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
	ADDRSTR_PORT_SIZE = 64, // INET6_ADDRSTRLEN + PORT_LEN
};

#define SECRET_UPDATE_TIME 15.f
#define SECRET_UPDATE_TIME_VARIANCE 5.f
#define UTCP_CONNECT_TIMEOUT (120 * 1000)

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
	ModeUnInitialized,
	Client, // Clientside PacketHandler
	Server	// Serverside PacketHandler
};

struct utcp_listener
{
	void* userdata;

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

	/** The cookie which completed the connection handshake. */
	uint8_t AuthorisedCookie[COOKIE_BYTE_SIZE];

	/** The initial server sequence value, from the last successful handshake */
	int32_t LastServerSequence;

	/** The initial client sequence value, from the last successful handshake */
	int32_t LastClientSequence;

	char LastChallengeSuccessAddress[ADDRSTR_PORT_SIZE];
};

struct utcp_connection
{
	void* userdata;

	uint8_t bClose : 1;
	uint8_t CloseReason : 7;

	// 握手相关
	uint8_t mode : 2;
	uint8_t state : 2;

	uint8_t bHasChannelClose : 1;
	uint8_t bLastChallengeSuccessAddress : 1;

	
	/** Whether or not component handshaking has begun */
	uint8_t bBeganHandshaking : 1;

	/** Client: Whether or not we are in the middle of a restarted handshake. Server: Whether or not the last handshake was a restarted handshake. */
	uint8_t bRestartedHandshake : 1;

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
	struct utcp_open_channels open_channels;

	/** Keep old behavior where we send a packet with only acks even if we have no other outgoing data if we got incoming data */
	uint32_t HasDirtyAcks;

	uint8_t SendBuffer[UTCP_MAX_PACKET + 32 /*MagicHeader*/ + 1 /*EndBits*/];
	size_t SendBufferBitsNum;

	int64_t LastSendTime; // Last time a packet was sent, for keepalives.

	/** Stores the bit number where we wrote the dummy packet info in the packet header */
	// size_t HeaderMarkForPacketInfo;
};

enum UTcpNetCloseResult
{
	/** Control channel closing */
	ControlChannelClose,

	/** Socket send failure */
	SocketSendFailure,

	/** A connection to the net driver has been lost */
	ConnectionLost,

	////////////////////////////////////////////////////////////////////////////////////

	/** NetConnection Cleanup was triggered */
	Cleanup,

	/** PacketHandler Error processing incoming packet */
	PacketHandlerIncomingError,

	/** Attempted to send data before handshake is complete */
	PrematureSend,

	/** A connection to the net driver has timed out */
	ConnectionTimeout,

	/** Packet had zeros in the last byte */
	ZeroLastByte,

	/** Zero size packet */
	ZeroSize,

	/** Failed to read PacketHeader */
	ReadHeaderFail,

	/** Failed to read extra PacketHeader information */
	ReadHeaderExtraFail,

	/** Sequence mismatch while processing acks */
	AckSequenceMismatch,

	/** Bunch channel index exceeds maximum channel limit */
	BunchBadChannelIndex,

	/** Bunch header or data serialization overflowed */
	BunchOverflow,

	/** Reliable buffer overflowed when attempting to send */
	ReliableBufferOverflow,
};
