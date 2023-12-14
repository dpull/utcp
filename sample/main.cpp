#include "ds_connection.h"
#include "echo_connection.h"
#include "sample_config.h"
#include "utcp_listener.h"
#include <chrono>
#include <memory>
#include <thread>

sample_config* g_config = nullptr;

void config()
{
	static sample_config config;
	g_config = &config;

	g_config->log_level_limit = log_level::Verbose;
	g_config->outgoing_loss = 0;
}

static void vlog(int level, const char* fmt, va_list marker)
{
	if (level > (int)g_config->log_level_limit)
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

void log(log_level level, const char* fmt, ...)
{
	va_list marker;
	va_start(marker, fmt);
	vlog((int)level, fmt, marker);
	va_end(marker);
}

struct sample_loop
{
	std::chrono::time_point<std::chrono::high_resolution_clock> now;
	int64_t frame = 0;

	sample_loop()
	{
		now = std::chrono::high_resolution_clock::now();
	}

	void tick()
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(5));

		auto cur_now = std::chrono::high_resolution_clock::now();
		utcp::event_handler::add_elapsed_time((cur_now - now).count());
		now = cur_now;
		frame++;
	}

	bool need_post_tick()
	{
		return frame % 10 == 0;
	}
};

void ds()
{
	std::unique_ptr<udp_utcp_listener> listener(new udp_utcp_listener_impl<ds_connection>);
	auto now = std::chrono::high_resolution_clock::now();
	sample_loop loop;

	listener->listen("127.0.0.1", 7777);

	while (true)
	{
		loop.tick();
		listener->tick();
		if (loop.need_post_tick())
		{
			listener->post_tick();
		}
	}
}

void echo()
{
	std::unique_ptr<udp_utcp_listener> listener(new udp_utcp_listener_impl<echo_connection>);
	std::unique_ptr<echo_connection> client(new echo_connection);
	sample_loop loop;

	listener->listen("127.0.0.1", 8241);
	client->async_connnect("127.0.0.1", 8241);

	while (loop.frame < 5000)
	{
		loop.tick();

		listener->tick();
		client->update();

		if (loop.need_post_tick())
		{
			listener->post_tick();
			client->flush_incoming_cache();
			client->send_flush();
			log(log_level::Verbose, "post_tick");
		}
	}
}

int main(int argc, const char* argv[])
{
	config();
	log(log_level::Log, "server start");

	utcp::event_handler::config(vlog);
	utcp::event_handler::enbale_dump_data(g_config->log_level_limit >= log_level::Verbose);

	ds();
	// echo();

	log(log_level::Log, "server stop");
	return 0;
}
