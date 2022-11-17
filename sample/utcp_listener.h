#pragma once
#include "utcp_connection.h"
#include <unordered_map>
#include <vector>

class utcp_listener : public utcp_connection
{
  public:
	utcp_listener();
	virtual void tick() override;
	virtual void after_tick() override;
	virtual int send(char* bunch, int count) override;
	virtual void raw_recv(uint8_t* data, int data_len, struct sockaddr_storage* from_addr, socklen_t from_addr_len) override;

	virtual void on_accept(bool reconnect) override;
	virtual void on_recv(const struct rudp_bunch* bunches[], int count) override;
	virtual void on_delivery_status(int32_t packet_id, bool ack) override;

  private:
	std::unordered_map<struct sockaddr_in, utcp_connection*, sockaddr_in_Hash, sockaddr_in_Equal> clients;

	std::vector<utcp_packet_view*> recv_unordered_queue;
	std::vector<utcp_packet_view*> proc_unordered_queue;
};

