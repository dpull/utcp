#include "utcp_connection.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#ifdef _MSC_VER
struct WSAGuard
{
	WSAGuard()

	{
		WSADATA wsadata;
		WSAStartup(MAKEWORD(2, 2), &wsadata);
	}
	~WSAGuard()
	{
		WSACleanup();
	}
};
static WSAGuard _WSAGuard;
#endif

static void log(const char* fmt, va_list marker)
{
	static FILE* fd = nullptr;
	if (!fd)
	{
		fd = fopen("sample_server.log", "a+");
	}

	if (fd)
	{
		vfprintf(fd, fmt, marker);
		fprintf(fd, "\n");
		fflush(fd);
	}
	vfprintf(stdout, fmt, marker);
	fprintf(stdout, "\n");
}

void log(const char* fmt, ...)
{
	va_list marker;
	va_start(marker, fmt);
	log(fmt, marker);
	va_end(marker);
}

void utcp_connection::config_utcp()
{
	auto config = utcp_get_config();
	config->on_accept = [](struct utcp_fd* fd, void* userdata, bool reconnect) {
		auto conn = static_cast<utcp_connection*>(userdata);
		assert(&conn->rudp == fd);
		conn->on_accept(reconnect);
	};
	config->on_raw_send = [](struct utcp_fd* fd, void* userdata, const void* data, int len) {
		auto conn = static_cast<utcp_connection*>(userdata);
		assert(&conn->rudp == fd);
		conn->on_raw_send(data, len);
	};
	config->on_recv = [](struct utcp_fd* fd, void* userdata, const struct utcp_bunch* bunches[], int count) {
		auto conn = static_cast<utcp_connection*>(userdata);
		assert(&conn->rudp == fd);
		conn->on_recv(bunches, count);
	};
	config->on_delivery_status = [](struct utcp_fd* fd, void* userdata, int32_t packet_id, bool ack) {
		auto conn = static_cast<utcp_connection*>(userdata);
		assert(&conn->rudp == fd);
		conn->on_delivery_status(packet_id, ack);
	};
	config->on_log = [](int level, const char* msg, va_list args) { log(msg, args); };

	config->enable_debug_cookie = true;
	for (int i = 0; i < sizeof(config->debug_cookie); ++i)
	{
		config->debug_cookie[i] = i + 1;
	}
}

utcp_connection::utcp_connection(bool is_client)
{
	utcp_init(&rudp, this, is_client);
}

utcp_connection::~utcp_connection()
{
	if (ordered_cache)
	{
		delete ordered_cache;
	}
}

bool utcp_connection::accept(utcp_connection* listener, bool reconnect)
{
	memcpy(&dest_addr, &listener->dest_addr, listener->dest_addr_len);
	dest_addr_len = listener->dest_addr_len;

	memcpy(rudp.LastChallengeSuccessAddress, listener->rudp.LastChallengeSuccessAddress, sizeof(rudp.LastChallengeSuccessAddress));

	socket_fd = listener->socket_fd;

	assert(!ordered_cache);
	ordered_cache = new utcp_packet_view_ordered_queue;

	if (!reconnect)
	{
		memcpy(rudp.AuthorisedCookie, listener->rudp.AuthorisedCookie, sizeof(rudp.AuthorisedCookie));
		utcp_sequence_init(&rudp, listener->rudp.LastClientSequence, listener->rudp.LastServerSequence);
		log("accept:(%d, %d)", listener->rudp.LastClientSequence, listener->rudp.LastServerSequence);
	}
	return true;
}

bool utcp_connection::is_cookie_equal(utcp_connection* listener)
{
	return memcmp(rudp.AuthorisedCookie, listener->rudp.AuthorisedCookie, sizeof(rudp.AuthorisedCookie)) == 0;
}

void utcp_connection::tick()
{
	utcp_update(&rudp);
	proc_ordered_cache(false);
}

void utcp_connection::after_tick()
{
	proc_ordered_cache(true);
	utcp_flush(&rudp);
	log("utcp_flush");
}

void utcp_connection::raw_recv(utcp_packet_view* view)
{
	auto packet_id = utcp_peep_packet_id(&rudp, view->data, view->data_len);
	if (packet_id <= 0)
	{
		dump("conn recv_hs", 0, view->data, view->data_len);
		utcp_ordered_incoming(&rudp, view->data, view->data_len);
		delete view;
		return;
	}

	view->handle = packet_id;
	ordered_cache->push(view);
}

// > 0 packet id
// = 0 send failed
// < 0 tmp pocket id
packet_id_range utcp_connection::send(struct utcp_bunch* bunches[], int bunches_count)
{
	return utcp_send(&rudp, bunches, bunches_count);
}

void utcp_connection::proc_ordered_cache(bool flushing_order_cache)
{
	while (true)
	{
		int handle = -1;
		if (!flushing_order_cache)
		{
			handle = utcp_expect_packet_id(&rudp);
		}

		auto view = ordered_cache->pop(handle);
		if (!view)
			break;

		dump("conn recv", view->handle, view->data, view->data_len);
		utcp_ordered_incoming(&rudp, view->data, view->data_len);
		delete view;
	}
}

void utcp_connection::on_raw_send(const void* data, int len)
{
	dump("send", rudp.OutPacketId, data, len);
	assert(dest_addr_len > 0);
	sendto(socket_fd, (const char*)data, len, 0, (sockaddr*)&dest_addr, dest_addr_len);
}

void utcp_connection::dump(const char* type, int ext, const void* data, int len)
{
	char str[16 * 1024];
	int size = 0;

	for (int i = 0; i < len; ++i)
	{
		if (i != 0)
			size += snprintf(str + size, sizeof(str) - size, ", ");
		size += snprintf(str + size, sizeof(str) - size, "0x%hhX", ((const uint8_t*)data)[i]);
	}

	log("%s-%d\t%d\t{%s}", type, ext, len, str);
}