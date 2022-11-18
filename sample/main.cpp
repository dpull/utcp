#include "utcp_listener.h"
#include <chrono>
#include <memory>
#include <thread>

int main(int argc, const char* argv[])
{
	log("server start");

	utcp_connection::config_utcp();
	std::unique_ptr<utcp_listener> listener(new utcp_listener);

	listener->listen("127.0.0.1", 7777);

	int64_t frame = 0;
	while (true)
	{
		frame++;
		listener->tick();

		if (frame % 10 == 0)
		{
			listener->after_tick();
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	return 0;
}
