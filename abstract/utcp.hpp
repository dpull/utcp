#pragma once
#include "utcp/utcp.h"
#include "utcp/utcp_def.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <queue>

namespace utcp
{
class event_handler
{
  public:
	static void add_elapsed_time(int64_t delta_time_ns);
	static void config(decltype(utcp_config::on_log) log_fn);

	virtual ~event_handler();

  protected:
	virtual void on_accept(bool reconnect);
	virtual void on_outgoing(const void* data, int len);
	virtual void on_recv_bunch(struct utcp_bunch* const bunches[], int count);
	virtual void on_delivery_status(int32_t packet_id, bool ack);
};

struct packet_view
{
	uint32_t _packet_id;
	uint16_t _data_len;
	uint8_t _data[UDP_MTU_SIZE];

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

struct large_bunch : utcp_bunch
{
	uint32_t ExtDataBitsLen = 0;
	uint8_t ExtData[UDP_MTU_SIZE * 64];

	struct iterator
	{
		large_bunch* ref;
		int pos;

		void operator++()
		{
			pos++;
		}

		bool operator!=(const iterator& rhs)
		{
			return pos != rhs.pos;
		}

		utcp_bunch& operator*()
		{
			return ref->sub_bunch(pos);
		}
	};
	iterator begin();
	iterator end();
	utcp_bunch& sub_bunch(int pos);
};

struct packet_id_range
{
	int32_t first;
	int32_t last;
};

class conn : public event_handler
{
  public:
	conn();
	virtual ~conn();

	virtual void connect();
	virtual void update();
	virtual void incoming(uint8_t* data, int count);
	virtual void flush_incoming_cache();
	virtual packet_id_range send_bunch(large_bunch* bunch);
	virtual void send_flush();
	virtual bool same_auth_cookie(const uint8_t* auth_cookie);

  protected:
	void flush_packet_order_cache(bool forced_flush);

  protected:
	std::priority_queue<packet_view> _packet_order_cache;
	utcp_connection* _utcp_fd;

	friend class listener;
};

class listener : public event_handler
{
  public:
	listener();
	virtual ~listener();

	void update_secret();
	uint8_t* get_auth_cookie();

	virtual void incoming(const char* address, uint8_t* data, int count);
	virtual void accept(conn* c, bool reconnect);

  protected:
	virtual void on_accept(bool reconnect) override;

  protected:
	utcp_listener* _utcp_fd;
};
} // namespace utcp
