#include "utcp_listener.h"
#include <chrono>
#include <memory>
#include <thread>

static void vlog(int level, const char* fmt, va_list marker)
{
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

int main(int argc, const char* argv[])
{
	log("server start");

	utcp::event_handler::config(vlog);
	std::unique_ptr<udp_utcp_listener> listener(new udp_utcp_listener);

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

	return 0;
}
