#include "utcp_connection.h"
#include <chrono>
#include <thread>

int main(int argc, const char* argv[])
{
	utcp_connection server;
	utcp_connection client;

	server.listen("127.0.0.1", 8241);
	client.connnect("127.0.0.1", 8241);

	char buf[256];

	for (int i = 0; i < 1024; ++i)
	{
		if (i % 4 == 0)
		{
			server.tick();
		}
		if (i % 8 == 0)
		{
			server.after_tick();
		}

		int len = snprintf(buf, sizeof(buf), "%d", i);
		client.send(buf, len + 1);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return 0;
}
