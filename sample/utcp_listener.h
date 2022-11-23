#pragma once
#include "utcp_connection.h"
#include <unordered_map>
#include <vector>

struct sockaddr_in_Hash
{
	std::size_t operator()(const sockaddr_in& in4) const
	{
		size_t out = in4.sin_port;
		out = out << 32;
		out += in4.sin_addr.s_addr;
		return out;
	}
};

struct sockaddr_in_Equal
{
	bool operator()(const sockaddr_in& lhs, const sockaddr_in& rhs) const
	{
		return lhs.sin_addr.s_addr == rhs.sin_addr.s_addr && lhs.sin_port == rhs.sin_port;
	}
};

class utcp_listener : public utcp_connection
{
  public:
	utcp_listener();
	~utcp_listener();
	bool listen(const char* ip, int port);

	virtual void tick() override;
	virtual void after_tick() override;

	virtual bool accept(utcp_connection* listener, bool reconnect) override DISABLE_FUNCTION;
	virtual void raw_recv(utcp_packet_view* view) override DISABLE_FUNCTION;
	virtual int send(struct utcp_bunch* bunch) override DISABLE_FUNCTION;

  protected:
	virtual void on_accept(bool reconnect) override;

	virtual void on_recv( struct utcp_bunch* const bunches[], int count) override DISABLE_FUNCTION;
	virtual void on_delivery_status(int32_t packet_id, bool ack) override DISABLE_FUNCTION;

	void create_recv_thread();
	void proc_recv(uint8_t* data, int data_len, struct sockaddr_storage* from_addr, socklen_t from_addr_len);
	void proc_recv_queue();

  private:
	volatile int recv_thread_exit_flag = false;
	std::thread* recv_thread = nullptr;
	std::mutex recv_queue_mutex;
	std::vector<utcp_packet_view*> recv_queue;
	std::vector<utcp_packet_view*> proc_queue;

	std::unordered_map<struct sockaddr_in, utcp_connection*, sockaddr_in_Hash, sockaddr_in_Equal> clients;
	std::chrono::time_point<std::chrono::high_resolution_clock> now;
};
