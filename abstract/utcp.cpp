#include "utcp.hpp"
extern "C" {
#include "utcp/bit_buffer.h"
#include "utcp/utcp_def_internal.h"
}
#include <algorithm>

namespace utcp
{
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
	config->on_connect = [](struct utcp_connection* fd, void* userdata, bool reconnect) {
		auto handler = static_cast<event_handler*>(userdata);
		handler->on_connect(reconnect);
	};
	config->on_disconnect = [](struct utcp_connection* fd, void* userdata, int close_reason) {
		auto handler = static_cast<event_handler*>(userdata);
		handler->on_disconnect(close_reason);
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

void event_handler::enbale_dump_data(bool enable)
{
	auto config = utcp_get_config();
	config->EnableDump = enable;
}

event_handler::~event_handler()
{
}

void event_handler::on_accept(bool reconnect)
{
	throw;
}

void event_handler::on_connect(bool reconnect)
{

}

void event_handler::on_disconnect(int close_reason)
{

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

}

large_bunch::large_bunch(const uint8_t* data, size_t data_bits_len)
{
	memset(this, 0, sizeof(*this));

	auto data_bytes_len = bits2bytes(data_bits_len);
	if (data_bytes_len > sizeof(ExtData))
		throw;

	if (data_bits_len > MAX_PARTIAL_BUNCH_SIZE_BITS)
	{
		assert(data_bytes_len < sizeof(ExtData));
		memcpy(ExtData, data, data_bytes_len);
		ExtDataBitsLen = (uint32_t)data_bits_len;
	}
	else
	{
		assert(data_bytes_len < sizeof(Data));
		memcpy(Data, data, data_bytes_len);
		DataBitsLen = (uint32_t)data_bits_len;
	}
}

large_bunch::large_bunch(utcp_bunch* const bunches[], int count)
{
	memcpy(this, bunches[0], sizeof(utcp_bunch));
	ExtDataBitsLen = 0;
	if (count == 1)
		return;

	struct bitbuf buf;
	bitbuf_write_init(&buf, ExtData, sizeof(ExtData));
	for (int i = 0; i < count; ++i)
	{
		auto bunch = bunches[i];
		assert(bunch->bPartial);
		assert(bunch->bPartialInitial == ((i == 0) ? 1 : 0));
		assert(bunch->bPartialFinal == ((i == (count - 1)) ? 1 : 0));
		ExtDataBitsLen += bunch->DataBitsLen;
		bitbuf_write_bits(&buf, bunch->Data, bunch->DataBitsLen);
	}
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
	it.pos = num();
	return it;
}

utcp_bunch& large_bunch::sub_bunch(int pos)
{
	if (ExtDataBitsLen == 0)
	{
		assert(pos == 0);
		return *this;
	}

	int last = ExtDataBitsLen / MAX_PARTIAL_BUNCH_SIZE_BITS;
	this->bPartial = last > 0;
	this->bPartialInitial = pos == 0;
	this->bPartialFinal = pos == last;

	if (pos == last)
	{
		int offset = pos * MAX_SINGLE_BUNCH_SIZE_BYTES;
		int left = std::min<int>(sizeof(this->ExtData) - offset, MAX_SINGLE_BUNCH_SIZE_BYTES);
		memcpy(this->Data, this->ExtData + offset, left);
		this->DataBitsLen = ExtDataBitsLen - MAX_PARTIAL_BUNCH_SIZE_BITS * pos;
	}
	else
	{
		int offset = pos * MAX_SINGLE_BUNCH_SIZE_BYTES;
		memcpy(this->Data, this->ExtData + offset, MAX_SINGLE_BUNCH_SIZE_BYTES);
		this->DataBitsLen = MAX_PARTIAL_BUNCH_SIZE_BITS;
	}
	return *this;
}

int large_bunch::num()
{
	int cnt = 1;
	if (ExtDataBitsLen > 0)
	{
		auto bytes_len = (int)bits2bytes(ExtDataBitsLen);
		cnt = bytes_len / MAX_SINGLE_BUNCH_SIZE_BYTES + 1;
	}
	return cnt;
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
	packet_id_range range{packet_id_range::INDEX_NONE, packet_id_range::INDEX_NONE};
	for (auto& sub : *bunch)
	{
		auto packet_id = utcp_send_bunch(_utcp_fd, &sub);
		if (range.first == packet_id_range::INDEX_NONE)
			range.first = packet_id;
		range.last = packet_id;
	}
	return range;
}

void conn::send_flush()
{
	utcp_send_flush(_utcp_fd);
}

utcp_connection* conn::get_fd()
{
	return _utcp_fd;
}

bool conn::is_closed()
{
	return _utcp_fd->bClose;
}

void conn::flush_packet_order_cache(bool forced_flush)
{
	while (!_packet_order_cache.empty())
	{
		int packet_id = forced_flush ? -1 : utcp_expect_packet_id(_utcp_fd);
		auto& view = _packet_order_cache.top();
		if (packet_id != -1 && view._packet_id > packet_id)
			break;

		utcp_incoming(_utcp_fd, const_cast<uint8_t*>(view._data), view._data_len);
		_packet_order_cache.pop();
	}
}
void conn::set_debug_name(const char* debug_name)
{
	if (debug_name)
	{
		strncpy(_utcp_fd->debug_name, debug_name, sizeof (_utcp_fd->debug_name));
		_utcp_fd->debug_name[sizeof (_utcp_fd->debug_name)-1] = '\0';
	}
	else
	{
		_utcp_fd->debug_name[0] = '\0';
	}
}
const char* conn::debug_name()
{
	return _utcp_fd->debug_name;
}
void bufconn::update()
{
	try_send();
	conn::update();
}

utcp::packet_id_range bufconn::send_bunch(large_bunch* bunch)
{
	try_send();

	if (!_send_buffer.empty() || utcp_send_would_block(_utcp_fd, bunch->num()))
	{
		if (!bunch->bReliable)
			return {packet_id_range::INDEX_NONE, packet_id_range::INDEX_NONE};
	}

	if (!_send_buffer.empty())
	{
		for (auto& sub : *bunch)
		{
			_send_buffer.push_back(sub);
		}
		_send_buffer_packet_id--;
		return packet_id_range{_send_buffer_packet_id, _send_buffer_packet_id};
	}

	packet_id_range range{packet_id_range::INDEX_NONE, packet_id_range::INDEX_NONE};
	for (auto& sub : *bunch)
	{
		if (!utcp_send_would_block(_utcp_fd, 1))
		{
			auto packet_id = utcp_send_bunch(_utcp_fd, bunch);
			if (range.first == packet_id_range::INDEX_NONE)
				range.first = packet_id;
			range.last = packet_id;
		}
		else
		{
			_send_buffer.push_back(sub);
		}
	}
	return range;
}

void bufconn::try_send()
{
	while (!_send_buffer.empty())
	{
		if (utcp_send_would_block(_utcp_fd, 1))
			break;

		auto& bunch = _send_buffer.back();
		utcp_send_bunch(_utcp_fd, &bunch);
		_send_buffer.pop_back();
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


void listener::incoming(const char* address, uint8_t* data, int count)
{
	utcp_listener_incoming(_utcp_fd, address, data, count);
}

void listener::accept(conn* c, bool reconnect)
{
	utcp_listener_accept(_utcp_fd, c->get_fd(), reconnect);
}

// StatelessConnectHandlerComponent::DoesRestartedHandshakeMatch
bool listener::does_restarted_handshake_match(conn* c)
{
	return memcmp(_utcp_fd->AuthorisedCookie, c->get_fd()->AuthorisedCookie, sizeof(_utcp_fd->AuthorisedCookie)) == 0;
}

utcp_listener* listener::get_fd()
{
	return _utcp_fd;
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
} // namespace utcp
