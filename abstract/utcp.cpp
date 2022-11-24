#include "utcp.hpp"
extern "C" {
#include "utcp/utcp_def_internal.h"
}

namespace utcp
{
enum
{
	MAX_SINGLE_BUNCH_SIZE_BITS = 7265, // Connection->GetMaxSingleBunchSizeBits();
	MAX_SINGLE_BUNCH_SIZE_BYTES = MAX_SINGLE_BUNCH_SIZE_BITS / 8,
	MAX_PARTIAL_BUNCH_SIZE_BITS = MAX_SINGLE_BUNCH_SIZE_BYTES * 8,
};

void event_handler::add_elapsed_time(int64_t delta_time_ns)
{
	utcp_add_elapsed_time(delta_time_ns);
}

void event_handler::config(decltype(utcp_config::on_log) log_fn)
{
	auto config = utcp_get_config();
	config->on_accept = [](struct utcp_listener* fd, void* userdata, bool reconnect) {
		auto handler = static_cast<event_handler*>(userdata);
		handler->on_accept(reconnect);
	};
	config->on_outgoing = [](void* fd, void* userdata, const void* data, int len) {
		auto handler = static_cast<event_handler*>(userdata);
		handler->on_outgoing(data, len);
	};
	config->on_recv_bunch = [](struct utcp_connection* fd, void* userdata, struct utcp_bunch* const bunches[], int count) {
		auto handler = static_cast<event_handler*>(userdata);
		handler->on_recv_bunch(bunches, count);
	};
	config->on_delivery_status = [](struct utcp_connection* fd, void* userdata, int32_t packet_id, bool ack) {
		auto handler = static_cast<event_handler*>(userdata);
		handler->on_delivery_status(packet_id, ack);
	};
	config->on_log = log_fn;
}

event_handler::~event_handler()
{
}

void event_handler::on_accept(bool reconnect)
{
	throw;
}

void event_handler::on_outgoing(const void* data, int len)
{
	throw;
}

void event_handler::on_recv_bunch(struct utcp_bunch* const bunches[], int count)
{
	throw;
}

void event_handler::on_delivery_status(int32_t packet_id, bool ack)
{
	throw;
}

conn::conn()
{
	_utcp_fd = utcp_connection_create();
	utcp_init(_utcp_fd, this);
}

conn::~conn()
{
	utcp_uninit(_utcp_fd);
	utcp_connection_destroy(_utcp_fd);
}

void conn::connect()
{
	utcp_connect(_utcp_fd);
}

void conn::update()
{
	utcp_update(_utcp_fd);
}

void conn::incoming(uint8_t* data, int count)
{
	auto packet_id = utcp_peep_packet_id(_utcp_fd, data, count);
	if (packet_id <= 0)
	{
		utcp_incoming(_utcp_fd, data, count);
		return;
	}
	_packet_order_cache.emplace(packet_id, data, count);

	flush_packet_order_cache(false);
}

void conn::flush_incoming_cache()
{
	flush_packet_order_cache(true);
}

packet_id_range conn::send_bunch(large_bunch* bunch)
{
	// TODO
	packet_id_range range;
	range.first = utcp_send_bunch(_utcp_fd, bunch);
	range.last = range.first;
	return range;
}

void conn::send_flush()
{
	utcp_send_flush(_utcp_fd);
}

bool conn::same_auth_cookie(const uint8_t* auth_cookie)
{
	return memcmp(_utcp_fd->AuthorisedCookie, auth_cookie, sizeof(_utcp_fd->AuthorisedCookie)) == 0;
}

void conn::flush_packet_order_cache(bool forced_flush)
{
	while (!_packet_order_cache.empty())
	{
		int packet_id = forced_flush ? -1 : utcp_expect_packet_id(_utcp_fd);
		auto& view = _packet_order_cache.top();
		if (packet_id != -1 && view._packet_id != packet_id)
			break;

		utcp_incoming(_utcp_fd, const_cast<uint8_t*>(view._data), view._data_len);
		_packet_order_cache.pop();
	}
}

listener::listener()
{
	_utcp_fd = utcp_listener_create();
	utcp_listener_init(_utcp_fd, this);
}

listener::~listener()
{
	utcp_listener_destroy(_utcp_fd);
}

void listener::update_secret()
{
	utcp_listener_update_secret(_utcp_fd, nullptr);
}

uint8_t* listener::get_auth_cookie()
{
	if (_utcp_fd->LastChallengeSuccessAddress[0])
		return _utcp_fd->AuthorisedCookie;
	return nullptr;
}

void listener::incoming(const char* address, uint8_t* data, int count)
{
	utcp_listener_incoming(_utcp_fd, address, data, count);
}

void listener::accept(conn* c, bool reconnect)
{
	utcp_listener_accept(_utcp_fd, c->_utcp_fd, reconnect);
}

void listener::on_accept(bool reconnect)
{
	if (reconnect)
	{
		// conn::same_auth_cookie(this->get_auth_cookie());
		throw;
	}
	auto c = new conn;
	accept(c, reconnect);
}

large_bunch::iterator large_bunch::begin()
{
	large_bunch::iterator it;
	it.ref = this;
	it.pos = 0;
	return it;
}

large_bunch::iterator large_bunch::end()
{
	large_bunch::iterator it;
	it.ref = this;
	it.pos = 1;
	if (ExtDataBitsLen > 0)
	{

	}
	return it;
}

utcp_bunch& large_bunch::sub_bunch(int pos)
{
	return *this;
}

} // namespace utcp