#include "utcp_connection.h"
#include <cassert>
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

void log(int level, const char* msg)
{
	printf(msg);
	printf("\n");
}

void utcp_connection::config_rudp()
{
	auto config = rudp_get_config();
	config->on_accept = [](struct rudp_fd* fd, void* userdata, bool reconnect) {
		auto conn = static_cast<utcp_connection*>(userdata);
		assert(&conn->rudp == fd);
		static_cast<utcp_connection*>(userdata)->on_accept(reconnect);
	};
	config->on_raw_send = [](struct rudp_fd* fd, void* userdata, const void* data, int len) {
		auto conn = static_cast<utcp_connection*>(userdata);
		assert(&conn->rudp == fd);
		static_cast<utcp_connection*>(userdata)->on_raw_send(data, len);
	};
	config->on_recv = [](struct rudp_fd* fd, void* userdata, const struct rudp_bunch* bunches[], int count) {
		auto conn = static_cast<utcp_connection*>(userdata);
		assert(&conn->rudp == fd);
		static_cast<utcp_connection*>(userdata)->on_recv(bunches, count);
	};
	config->on_delivery_status = [](struct rudp_fd* fd, void* userdata, int32_t packet_id, bool ack) {
		auto conn = static_cast<utcp_connection*>(userdata);
		assert(&conn->rudp == fd);
		static_cast<utcp_connection*>(userdata)->on_delivery_status(packet_id, ack);
	};
	config->on_log = [](int level, const char* msg) { log(level, msg); };
}

utcp_connection::utcp_connection(bool is_client)
{
	rudp_init(&rudp, this, is_client);
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
		rudp_sequence_init(&rudp, listener->rudp.LastClientSequence, listener->rudp.LastServerSequence);
	}
	return true;
}

void utcp_connection::tick()
{
	proc_ordered_cache(false);
}

void utcp_connection::after_tick()
{
	proc_ordered_cache(true);
}

void utcp_connection::raw_recv(utcp_packet_view* view)
{
	auto packet_id = rudp_packet_peep_id(&rudp, view->data, view->data_len);
	if (packet_id <= 0)
	{
		rudp_incoming(&rudp, view->data, view->data_len);
		delete view;
		return;
	}

	view->handle = packet_id;
	ordered_cache->push(view);
}

// > 0 packet id
// = 0 send failed
// < 0 tmp pocket id
int utcp_connection::send(char* bunch, int count)
{
	::sendto(socket_fd, bunch, count, 0, (struct sockaddr*)&dest_addr, dest_addr_len);
	return 0;
}

void utcp_connection::proc_ordered_cache(bool flushing_order_cache)
{
	while (true)
	{
		int handle = -1;
		if (!flushing_order_cache)
		{
			handle = 100;
		}

		auto view = ordered_cache->pop(handle);
		if (!view)
			break;

		// TODO
		delete view;
	}
}

void utcp_connection::on_raw_send(const void* data, int len)
{
	assert(dest_addr_len > 0);
	sendto(socket_fd, (const char*)data, len, 0, (sockaddr*)&dest_addr, dest_addr_len);
}

void utcp_connection::on_recv(const struct rudp_bunch* bunches[], int count)
{
}

void utcp_connection::on_delivery_status(int32_t packet_id, bool ack)
{
}
