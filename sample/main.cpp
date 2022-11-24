#include "ds_connection.h"
#include "echo_connection.h"
#include "utcp_listener.h"
#include <chrono>
#include <memory>
#include <thread>

static int LOG_LEVEL = 6; 

static void vlog(int level, const char* fmt, va_list marker)
{
	if (level > LOG_LEVEL)
		return;

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
	vlog(0, fmt, marker);
	va_end(marker);
}

void ds()
{
	std::unique_ptr<udp_utcp_listener> listener(new udp_utcp_listener_impl<ds_connection>);

	listener->listen("127.0.0.1", 7777);

	int64_t frame = 0;
	while (true)
	{
		frame++;
		listener->tick();

		if (frame % 10 == 0)
		{
			listener->post_tick();
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}

void echo()
{
	std::unique_ptr<udp_utcp_listener> listener(new udp_utcp_listener_impl<echo_connection>);
	std::unique_ptr<echo_connection> client(new echo_connection);

	listener->listen("127.0.0.1", 8241);
	client->async_connnect("127.0.0.1", 8241);

	int64_t frame = 0;
	while (true)
	{
		frame++;
		listener->tick();
		client->update();

		if (frame % 10 == 0)
		{
			listener->post_tick();
			client->flush_incoming_cache();
			client->send_flush();
			log("post_tick");
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
}

int main(int argc, const char* argv[])
{
	log("server start");

	utcp::event_handler::config(vlog);
	utcp::event_handler::enbale_dump_data(LOG_LEVEL > 5);
	
	// ds();
	echo();

	return 0;
}
