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
		assert(&conn->utcp == fd);
		conn->on_accept(reconnect);
	};
	config->on_raw_send = [](struct utcp_fd* fd, void* userdata, const void* data, int len) {
		auto conn = static_cast<utcp_connection*>(userdata);
		assert(&conn->utcp == fd);
		conn->on_raw_send(data, len);
	};
	config->on_recv = [](struct utcp_fd* fd, void* userdata, struct utcp_bunch* const bunches[], int count) {
		auto conn = static_cast<utcp_connection*>(userdata);
		assert(&conn->utcp == fd);
		conn->on_recv(bunches, count);
	};
	config->on_delivery_status = [](struct utcp_fd* fd, void* userdata, int32_t packet_id, bool ack) {
		auto conn = static_cast<utcp_connection*>(userdata);
		assert(&conn->utcp == fd);
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
	utcp_init(&utcp, this, is_client);
}

utcp_connection::~utcp_connection()
{
	if (ordered_cache)
	{
		delete ordered_cache;
	}
}

bool utcp_connection::accept(utcp_connection* connection, bool reconnect)
{
	memcpy(&connection->dest_addr, &this->dest_addr, connection->dest_addr_len);
	connection->dest_addr_len = this->dest_addr_len;

	memcpy(connection->utcp.LastChallengeSuccessAddress, this->utcp.LastChallengeSuccessAddress, sizeof(connection->utcp.LastChallengeSuccessAddress));

	connection->socket_fd = this->socket_fd;

	assert(!ordered_cache);
	connection->ordered_cache = new utcp_packet_view_ordered_queue;

	if (!reconnect)
	{
		memcpy(connection->utcp.AuthorisedCookie, this->utcp.AuthorisedCookie, sizeof(utcp.AuthorisedCookie));
		utcp_sequence_init(&connection->utcp, this->utcp.LastClientSequence, this->utcp.LastServerSequence);
		log("accept:(%d, %d)", this->utcp.LastClientSequence, this->utcp.LastServerSequence);
	}
	return true;
}

bool utcp_connection::is_cookie_equal(utcp_connection* listener)
{
	return memcmp(utcp.AuthorisedCookie, listener->utcp.AuthorisedCookie, sizeof(utcp.AuthorisedCookie)) == 0;
}

void utcp_connection::tick()
{
	utcp_update(&utcp);
	proc_ordered_cache(false);
}

void utcp_connection::after_tick()
{
	proc_ordered_cache(true);
	utcp_flush(&utcp);
	log("utcp_flush");
}

void utcp_connection::raw_recv(utcp_packet_view* view)
{
	auto packet_id = utcp_peep_packet_id(&utcp, view->data, view->data_len);
	if (packet_id <= 0)
	{
		utcp_ordered_incoming(&utcp, view->data, view->data_len);
		delete view;
		return;
	}

	view->handle = packet_id;
	ordered_cache->push(view);
}

// > 0 packet id
// = 0 send failed
// < 0 tmp pocket id
int utcp_connection::send(struct utcp_bunch* bunch)
{
	return utcp_send_bunch(&utcp, bunch);
}

void utcp_connection::proc_ordered_cache(bool flushing_order_cache)
{
	while (true)
	{
		int handle = -1;
		if (!flushing_order_cache)
		{
			handle = utcp_expect_packet_id(&utcp);
		}

		auto view = ordered_cache->pop(handle);
		if (!view)
			break;

		utcp_ordered_incoming(&utcp, view->data, view->data_len);
		delete view;
	}
}

void utcp_connection::on_raw_send(const void* data, int len)
{
	assert(dest_addr_len > 0);
	sendto(socket_fd, (const char*)data, len, 0, (sockaddr*)&dest_addr, dest_addr_len);
}
