#pragma once
#include "utcp/utcp.h"
#include "utcp/utcp_def.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <list>
#include <queue>

namespace utcp
{
// Fairly large number, and probably a bad idea to even have a bunch this size, but want to be safe for now and not throw out legitimate data
constexpr int32_t NetMaxConstructedPartialBunchSizeBytes = 1024 * 64;
constexpr int32_t MAX_SINGLE_BUNCH_SIZE_BITS = 7265; // Connection->GetMaxSingleBunchSizeBits();
constexpr int32_t MAX_SINGLE_BUNCH_SIZE_BYTES = MAX_SINGLE_BUNCH_SIZE_BITS / 8;
constexpr int32_t MAX_PARTIAL_BUNCH_SIZE_BITS = MAX_SINGLE_BUNCH_SIZE_BYTES * 8;
static_assert(UDP_MTU_SIZE > MAX_SINGLE_BUNCH_SIZE_BYTES);

inline size_t bits2bytes(size_t bits_len)
{
	return (bits_len + 7) >> 3;
}

class event_handler
{
  public:
	static void add_elapsed_time(int64_t delta_time_ns);
	static void config(decltype(utcp_config::on_log) log_fn);
	static void enbale_dump_data(bool enable);

	virtual ~event_handler();

  protected:
	virtual void on_accept(bool reconnect);
	virtual void on_connect(bool reconnect);
	virtual void on_disconnect(int close_reason);
	virtual void on_outgoing(const void* data, int len);
	virtual void on_recv_bunch(struct utcp_bunch* const bunches[], int count);
	virtual void on_delivery_status(int32_t packet_id, bool ack);
};

struct packet_view
{
	int32_t _packet_id;
	uint16_t _data_len;
	uint8_t _data[NetMaxConstructedPartialBunchSizeBytes];

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
	explicit large_bunch(const uint8_t* data, size_t data_bits_len);
	explicit large_bunch(utcp_bunch* const bunches[], int count);
    large_bunch();

	uint32_t ExtDataBitsLen : 28;
	uint32_t bExtPartialSetFlag : 1;
	uint32_t bExtPartial : 1;
	uint32_t bExtPartialInitial : 1;
	uint32_t bExtPartialFinal : 1;

	uint8_t ExtData[UDP_MTU_SIZE * 64];

#pragma region Range - based for loop
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
	int num();
	
	large_bunch(const large_bunch&) = delete;
	large_bunch(large_bunch&&) = delete;
#pragma endregion
};

struct packet_id_range
{
	enum
	{
		INDEX_NONE = -1
	};

	int32_t first;
	int32_t last;
};

class conn : public event_handler
{
  public:
	conn();
	virtual ~conn() override;

	virtual void connect();
	virtual void update();
	virtual void incoming(uint8_t* data, int count);
	virtual void flush_incoming_cache();
	virtual packet_id_range send_bunch(large_bunch* bunch);
	virtual void send_flush();

	utcp_connection* get_fd();
	bool is_closed();
	void set_debug_name(const char* debug_name);
	const char* debug_name();

  protected:
	void flush_packet_order_cache(bool forced_flush);

  protected:
	std::priority_queue<packet_view> _packet_order_cache;
	utcp_connection* _utcp_fd;
};

class bufconn : public conn
{
  public:
	virtual void update() override;
	virtual packet_id_range send_bunch(large_bunch* bunch) override;

  protected:
	void try_send();

	std::list<utcp_bunch> _send_buffer;
	int32_t _send_buffer_packet_id = packet_id_range::INDEX_NONE;
};

class listener : public event_handler
{
  public:
	listener();
	virtual ~listener() override;

	void update_secret();

	virtual void incoming(const char* address, uint8_t* data, int count);
	virtual void accept(conn* c, bool reconnect);
	virtual bool does_restarted_handshake_match(conn* c);
	
	utcp_listener* get_fd();

  protected:
	virtual void on_accept(bool reconnect) override;

  protected:
	utcp_listener* _utcp_fd;
};
} // namespace utcp
