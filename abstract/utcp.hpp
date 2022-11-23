#pragma once
#include "utcp/utcp.h"
#include "utcp/utcp_def.h"
#include <cstdio>
#include <queue>

#ifndef UTCP_LOG
#define UTCP_LOG vprintf
#endif

namespace utcp
{
class event_handler
{
  public:
	static void add_elapsed_time(int64_t delta_time_ns)
	{
		utcp_add_elapsed_time(delta_time_ns);
	}

	static void config()
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
		config->on_log = [](int level, const char* msg, va_list args) {
#ifdef UTCP_LOG
			UTCP_LOG(msg, args);
#endif
		};
	}

	virtual ~event_handler()
	{
	}

  protected:
	virtual void on_accept(bool reconnect)
	{
		throw;
	}
	virtual void on_outgoing(const void* data, int len)
	{
		throw;
	}
	virtual void on_recv_bunch(struct utcp_bunch* const bunches[], int count)
	{
		throw;
	}
	virtual void on_delivery_status(int32_t packet_id, bool ack)
	{
		throw;
	}
};

struct packet_view
{
	uint32_t _packet_id;
	uint16_t _data_len;
	uint8_t _data[UTCP_MAX_PACKET];

	packet_view(int32_t packet_id, uint8_t* data, int count) : _packet_id(packet_id), _data_len(count)
	{
		assert(count < sizeof(_data));
		memcpy(_data, data, count);
	}

	friend bool operator<(const packet_view& l, const packet_view& r)
	{
		return l._packet_id > r._packet_id;
	}
};

struct packet_id_range
{
	int32_t first;
	int32_t last;
};

struct large_bunch : utcp_bunch
{
	uint32_t ext_data_bits_len;
	uint8_t ext_data[UDP_MTU_SIZE * 64];
};

class conn : public event_handler
{
  public:
	conn()
	{
		utcp_init(&_utcp_fd, this);
	}

	virtual ~conn()
	{
		utcp_uninit(&_utcp_fd);
	}

	virtual void connect()
	{
		utcp_connect(&_utcp_fd);
	}
	
	virtual void update()
	{
		utcp_update(&_utcp_fd);
	}

	virtual void incoming(uint8_t* data, int count)
	{
		auto packet_id = utcp_peep_packet_id(&_utcp_fd, data, count);
		if (packet_id <= 0)
		{
			utcp_incoming(&_utcp_fd, data, count);
			return;
		}
		_packet_order_cache.emplace(packet_id, data, count);

		flush_packet_order_cache(false);
	}

	virtual void flush_incoming_cache()
	{
		flush_packet_order_cache(true);
	}

	virtual packet_id_range send_bunch(large_bunch* bunch)
	{
		// TODO
		packet_id_range range;
		range.first = utcp_send_bunch(&_utcp_fd, bunch);
		range.last = range.first;
		return range;
	}

	virtual void send_flush()
	{
		utcp_send_flush(&_utcp_fd);
	}

	virtual bool same_auth_cookie(const uint8_t* auth_cookie)
	{
		return memcmp(_utcp_fd.AuthorisedCookie, auth_cookie, sizeof(_utcp_fd.AuthorisedCookie)) == 0;
	}

  protected:
	void flush_packet_order_cache(bool forced_flush)
	{
		for (;;)
		{
			int packet_id = forced_flush ? -1 : utcp_expect_packet_id(&_utcp_fd);
			auto& view = _packet_order_cache.top();
			if (packet_id != -1 && view._packet_id != packet_id)
				break;

			utcp_incoming(&_utcp_fd, const_cast<uint8_t*>(view._data), view._data_len);
			_packet_order_cache.pop();
		}
	}

  protected:
	std::priority_queue<packet_view> _packet_order_cache;
	utcp_connection _utcp_fd;

	friend class listener;
};

class listener : public event_handler
{
  public:
	listener()
	{
		utcp_listener_init(&_utcp_fd, this);
	}

	virtual ~listener()
	{
	}

	void update_secret()
	{
		utcp_listener_update_secret(&_utcp_fd, nullptr);
	}

	virtual void incoming(const char* address, uint8_t* data, int count)
	{
		utcp_listener_incoming(&_utcp_fd, address, data, count);
	}

	uint8_t* get_auth_cookie()
	{
		if (_utcp_fd.LastChallengeSuccessAddress[0])
			return _utcp_fd.AuthorisedCookie;  
		return nullptr;
	}

	virtual void accept(conn* c, bool reconnect) 
	{
		utcp_listener_accept(&_utcp_fd, &c->_utcp_fd, reconnect);
	}

  protected:
	virtual void on_accept(bool reconnect) override
	{
		if (reconnect)
		{
			// conn::same_auth_cookie(this->get_auth_cookie());
			throw;
		}
		auto c = new conn;
		accept(c, reconnect);
	}

  protected:
	utcp_listener _utcp_fd;
};
} // namespace utcp
