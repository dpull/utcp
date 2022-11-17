#pragma once

#ifdef WIN32
#include <Winsock2.h>
#include <Ws2tcpip.h>
#include <Windows.h>

using socket_t = SOCKET;
inline int get_socket_error()
{
	return WSAGetLastError();
}
using ssize_t = int;
#endif

#if defined(__linux) || defined(__APPLE__)
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <netdb.h>

using socket_t = int;
constexpr socket_t INVALID_SOCKET = -1;
constexpr int SOCKET_ERROR = -1;

inline int get_socket_error()
{
	return errno;
}
inline void closesocket(socket_t fd)
{
	close(fd);
}
#endif
