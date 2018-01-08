/* network.c
 * Network API abstraction
 * 
 * Copyright 2015-2018 Akash Rawal
 * This file is part of dispatch_ng.
 * 
 * dispatch_ng is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * dispatch_ng is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with dispatch_ng.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "incl.h"

#include <errno.h>
#include <ctype.h>

#ifdef _WIN32
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT _WIN32_WINNT_VISTA
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#endif

//Socket errors
#ifdef _WIN32
#define nv_error WSAGetLastError()
#else
#define nv_error errno
#endif

//IO_TEMP_ERROR: whether the error is a temporary issue
#if ! defined(EWOULDBLOCK)
#define IO_TEMP_ERROR(e) ((e == EAGAIN) || (e == EINTR))
#elif (EAGAIN == EWOULDBLOCK)
#define IO_TEMP_ERROR(e) ((e == EAGAIN) || (e == EINTR))
#else
#define IO_TEMP_ERROR(e) ((e == EAGAIN) || (e == EWOULDBLOCK) || (e == EINTR))
#endif

//Socket error definitions
const char socket_error_generic[] = "Generic socket error";

const char socket_error_invalid_socket[] = "Invalid socket handle";
const char socket_error_invalid_address[] = "Invalid address";
const char socket_error_again[] = "Resource temoporarily unavailable";
const char socket_error_in_progress[] = "In progress";
const char socket_error_already[] = "Socket is already connecting/connected";
const char socket_error_timeout[] = "Operation timed out";
const char socket_error_network_unreachable[] = "Network unreachable";
const char socket_error_host_unreachable[] = "Host unreachable";
const char socket_error_connection_refused[] = "Connection refused";
const char socket_error_unsupported_backend_feature[]
= "The current socket backend exhibits a feature that we cannot handle";

//Workarounds for implementation quirks
typedef enum
{
	WA_CONNECT = 1 << 1
} WAFlags;

//Convert errno errors to Error
static const char *errno_to_error_type(int errno_val, WAFlags wa_flags)
{
	if (IO_TEMP_ERROR(errno_val))
		return socket_error_again;
	else if (errno_val == EBADF || errno_val == ENOTSOCK)
		return socket_error_invalid_socket;
	else if (errno_val == EINPROGRESS)
		return socket_error_in_progress;
#ifdef _WIN32
	else if (wa_flags & WA_CONNECT && errno_val == WSAEWOULDBLOCK)
		return socket_error_in_progress;
#endif
	else if (errno_val == EALREADY)
		return socket_error_already;
	else if (errno_val == ETIMEDOUT)
		return socket_error_timeout;
	else if (errno_val == ENETUNREACH)
		return socket_error_network_unreachable;
	else if (errno_val == EHOSTUNREACH)
		return socket_error_host_unreachable;
	else if (errno_val == ECONNREFUSED)
		return socket_error_connection_refused;
	else
		return socket_error_generic;
}

static const Error *error_from_errno
	(int errno_val, WAFlags wa_flags, const char *fmt, ...)
{
	va_list arglist;
	char *str;
	const Error *e;
	const char *type;
	const char *errno_val_str;

#ifdef _WIN32
	char errno_val_buf[32];
	char *fmstr = NULL;
	if (FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER
			| FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, errno_val, 0, (char *) &fmstr, 0, NULL) > 0)
	{
		errno_val_str = fmstr;
	}
	else
	{
		snprintf(errno_val_buf, 31, "Unknown error %d", errno_val);
		errno_val_buf[31] = 0;
		errno_val_str = errno_val_buf;
		fmstr = NULL;
	}
#else
	errno_val_str = strerror(errno_val);
#endif

	va_start(arglist, fmt);
	str = fs_strdup_vprintf(fmt, arglist);
	va_end(arglist);

	type = errno_to_error_type(errno_val, wa_flags);

	e = error_printf(type, "%s: %s", str, errno_val_str);

	free(str);

#ifdef _WIN32
	if (fmstr)
		LocalFree(fmstr);
#endif

	return e;
}



//////////////////////////////////
//Host address functions
Status host_address_from_str(const char *str, HostAddress *addr_out)
{
	HostAddress addr;

	memset(&addr, 0, sizeof(HostAddress));

	//Skip all whitespace if any
	while(isspace(*str))
		str++;

#ifdef _WIN32
#define _inet_pton_fn InetPtonA
#else
#define _inet_pton_fn inet_pton
#endif


	//Check the family
	if (*str == '[')
	{
		//IPv6
		char *ip6_str;
		int res;
		
		//Remove trailing ']'
		ip6_str = split_string(str + 1, ']', NULL);
		if (! ip6_str)
			return STATUS_FAILURE;

		//Do the conversion
		res = _inet_pton_fn(AF_INET6, ip6_str, (void *) addr.ip);
		free(ip6_str);
		if (res != 1)
			return STATUS_FAILURE;
		addr.type = NETWORK_INET6;
	}
	else
	{
		//IPv4
		if (_inet_pton_fn(AF_INET, str, (void *) addr.ip) != 1)
			return STATUS_FAILURE;
		addr.type = NETWORK_INET;
	}

#undef _inet_pton_fn

	*addr_out = addr;
	return STATUS_SUCCESS;
}

//Creates string representation of the address.
//Min size of output buffer is ADDRESS_MAX_LEN.
void host_address_to_str(HostAddress addr, char *out)
{
	if (addr.type == NETWORK_INET)
	{
		unsigned char *pun = (unsigned char *) addr.ip;
		//Write the octets
		snprintf(out, ADDRESS_MAX_LEN, "%u.%u.%u.%u", 
				(unsigned int) pun[0], (unsigned int) pun[1],
				(unsigned int) pun[2], (unsigned int) pun[3]);
	}
	else
	{
		//IPv6
		const char lut[16] = "0123456789abcdef";
		char *out_ptr = out;
		uint16_t *pun = (uint16_t *) addr.ip;
		int i, j;

		//Find the largest sequence of zeroes.
		int pos = -1, len = 0, maxpos = -1, maxlen = 0;
		for (i = 0; i < 8; i++)
		{
			if (pun[i])
			{
				pos = -1;
				len = 0;
			}
			else
			{
				if (pos == -1)
				{
					pos = i;
					len = 1;
				}
				else
				{
					len++;
				}

				if (maxlen < len)
				{
					maxpos = pos;
					maxlen = len;
				}
			}
		}

		//Now write the address
		int colon_flag = 0;
		*out_ptr = '[';
		out_ptr++;
		for (i = 0; i < 8; i++)
		{
			if (i == maxpos)
			{
				//Represent zeroes with ::
				colon_flag = 0;
				out_ptr[0] = out_ptr[1] = ':';
				out_ptr += 2;
				i += maxlen - 1;
			}
			else
			{
				//Write one 16-bit chunk
				uint16_t part = ntohs(pun[i]);
				int mode = 0;
				if (colon_flag)
				{
					*out_ptr = ':';
					out_ptr++;
				}
				else
				{
					colon_flag = 1;
				}
				for (j = 3; j >= 0; j--)
				{
					int digit = (part >> (j * 4)) & 0xf;

					if (digit || j == 0)
						mode = 1;

					if (mode)
					{
						*out_ptr = lut[digit];
						out_ptr++;
					}
				}
			}
		}
		*out_ptr = ']';
		out_ptr++;
		*out_ptr = 0;
	}
}


////////////////////////////////////////////
//Socket address functions

//Creates string representation of the address.
//Min size of output buffer is ADDRESS_MAX_LEN.
void socket_address_to_str(SocketAddress addr, char *out)
{
	char buf[10];

	snprintf(buf, 10, ":%u", (unsigned int) ntohs(addr.port));
	host_address_to_str(addr.host, out);
	strcat(out, buf);
} 

//Reads an address from string in human readable format.
Status socket_address_from_str(const char *str, SocketAddress *addr_out)
{
	char *host_str, *port_str;
	Status sub_res = STATUS_FAILURE;

	host_str = split_string(str, ':',  &port_str);
	if (! host_str)
		return STATUS_FAILURE;

	if (*port_str)
	{
		long port;

		if (parse_long(port_str, &port) == STATUS_SUCCESS)
		{
			SocketAddress addr;
			memset(&addr, 0, sizeof(SocketAddress));

			addr.port = htons(port);

			sub_res = host_address_from_str(host_str, &addr.host);

			if (sub_res == STATUS_SUCCESS)
				*addr_out = addr;
		}
	}

	free(host_str);
	return sub_res;
}

/////////////////////////////
//Socket functions

//getsockopt/setsockopt option paramater
#ifdef _WIN32
#define sockopt(x) ((char *) (x))
#else
#define sockopt(x) (x)
#endif


//Native address
typedef struct
{
	socklen_t size;
	int pf;
	union {
		struct sockaddr generic;
		struct sockaddr_in inet;
		struct sockaddr_in6 inet6;
	} data;
} NativeAddress;

//Create native address
static NativeAddress native_address_create
	(NetworkType type, void *ip, uint16_t port)
{
	NativeAddress native_addr;

	memset(&native_addr, 0, sizeof(native_addr));

	if (type == NETWORK_INET)
	{
		native_addr.data.inet.sin_family = AF_INET;
		memcpy(&native_addr.data.inet.sin_addr, ip, 4);
		native_addr.data.inet.sin_port = port;
		native_addr.pf = PF_INET;
		native_addr.size = sizeof(struct sockaddr_in);
	}
	else
	{
		native_addr.data.inet6.sin6_family = AF_INET6;
		memcpy(&native_addr.data.inet6.sin6_addr, ip, 16);
		native_addr.data.inet6.sin6_port = port;
		native_addr.pf = PF_INET6;
		native_addr.size = sizeof(struct sockaddr_in6);
	}

	return native_addr;
}

//Create socket
static const Error *create_socket(NetworkType type, void *ip, uint16_t port,
		evutil_socket_t *fd_out)
{
	evutil_socket_t fd;
	NativeAddress native_addr;

	native_addr = native_address_create(type, ip, port);

	//mark1
	if ((fd = socket(native_addr.pf, SOCK_STREAM, IPPROTO_TCP)) < 0)
		return error_from_errno(nv_error, 0, "socket() failed");

	{
		const int one = 1;
		//mark1
		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, 
					sockopt(&one), sizeof(one)) < 0)
		{
			close(fd);
			return error_from_errno(nv_error, 0,
					"setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, 1) failed");
		}
	}

	if (bind(fd, &native_addr.data.generic, native_addr.size) < 0)
	{
		close(fd);
		return error_from_errno(nv_error, 0, "bind(fd = %d) failed", fd);
	}

	*fd_out = fd;

	return NULL;
}

//Closes the file descriptor
void socket_handle_close(SocketHandle hd)
{
	close(hd.fd);
}

//Creates a new socket bound to the network interface and random port
const Error *socket_handle_create_bound
	(SocketAddress addr, SocketHandle *hd_out)
{
	return create_socket(addr.host.type, addr.host.ip, addr.port, &hd_out->fd);
}

//Creates a listening socket bound to the given address
const Error *socket_handle_create_listener
	(SocketAddress addr, SocketHandle *hd_out)
{
	const Error *e;
	SocketHandle hd;

	e = create_socket(addr.host.type, addr.host.ip, addr.port, &hd.fd);
	if (e)
		return e;

	if (listen(hd.fd, SOMAXCONN) < 0)
	{
		close(hd.fd);
		return error_from_errno(nv_error, 0, "listen(fd = %d) failed", hd.fd);
	}

	*hd_out = hd;
	return NULL;
}

//Compares socket with native one
int socket_handle_equal_with_native(SocketHandle hd, evutil_socket_t socket)
{
	return hd.fd == socket;
}

//Creates an event structure for given socket
struct event *socket_handle_create_event(SocketHandle hd, short events,
		event_callback_fn callback_fn, void *callback_arg)
{
	return event_new(evbase, hd.fd, events, callback_fn, callback_arg);
}

//Connects to a listening socket bound to given address
const Error *socket_handle_connect(SocketHandle hd, SocketAddress addr)
{
	NativeAddress native_addr;

	native_addr = native_address_create
		(addr.host.type, addr.host.ip, addr.port);
	
	if (connect(hd.fd, &native_addr.data.generic, native_addr.size) < 0)
		return error_from_errno(nv_error, WA_CONNECT,
				"connect(fd = %d) failed", hd.fd);

	return NULL;
}

//Accepts a connection
const Error *socket_handle_accept(SocketHandle hd, SocketHandle *hd_out)
{
	int res_fd;

	res_fd = accept(hd.fd, NULL, NULL);
	if (res_fd < 0)
		return error_from_errno(nv_error, 0, "accept(fd = %d) failed", hd.fd);

	hd_out->fd = res_fd;
	return NULL;
}

//Checks error status of given socket
const Error *socket_handle_get_status(SocketHandle hd)
{
	int res;
	socklen_t optlen = sizeof(res);
	
	if (getsockopt(hd.fd, SOL_SOCKET, SO_ERROR, sockopt(&res), &optlen) < 0)
		abort_with_liberror("getsockopt()");
	
	if (res == 0)
		return NULL;
	else
		return error_from_errno(res, 0, "Error for fd=%d", hd.fd);
}

//Returns address bound to the socket
const Error *socket_handle_getsockname
	(SocketHandle hd, SocketAddress *addr_out)
{
	union
	{
		struct sockaddr generic;
		struct sockaddr_in ipv4;
		struct sockaddr_in6 ipv6;
	} native_addr;
	socklen_t native_addr_len = sizeof(native_addr);
	
	if (getsockname(hd.fd, &native_addr.generic, &native_addr_len) < 0)
		return error_from_errno(nv_error, 0, "getsockname(fd = %d)", hd.fd);

	//Convert native address to SocketAddress	
	if (native_addr.generic.sa_family == AF_INET)
	{
		addr_out->host.type = NETWORK_INET;
		memcpy(addr_out->host.ip, &native_addr.ipv4.sin_addr, 4);
		addr_out->port = native_addr.ipv4.sin_port;
	}
	else if (native_addr.generic.sa_family == AF_INET6)
	{
		addr_out->host.type = NETWORK_INET6;
		memcpy(addr_out->host.ip, &native_addr.ipv6.sin6_addr, 16);
		addr_out->port = native_addr.ipv6.sin6_port;
	}
	else
	{
		return error_printf(socket_error_unsupported_backend_feature,
				"Unsupported address family %d",
				(int) native_addr.generic.sa_family);
	}
	return NULL;
}

//Enables or disables nonblocking IO mode
const Error *socket_handle_set_blocking(SocketHandle hd, int val)
{
#ifdef _WIN32
	unsigned long mode = val ? 0 : 1;
	if (ioctlsocket(hd.fd, FIONBIO, &mode) != 0)
	{
		return error_from_errno(nv_error, 0, "ioctlsocket(SOCKET, FIONBIO, %lu)",
				mode);
	}
#else
	int flags = fcntl(hd.fd, F_GETFL);
	if (flags < 0)
	{
		return error_from_errno(nv_error, 0, "fcntl(%d, F_GETFL)", hd.fd);
	}
	if (val)
		flags &= (~O_NONBLOCK);
	else
		flags |= O_NONBLOCK;
	if (fcntl(hd.fd, F_SETFL, flags))
	{
		return error_from_errno(nv_error, 0, "fcntl(%d, F_SETFL, %d)", 
				hd.fd, flags);
	}
#endif

	return NULL;
}

const Error *socket_handle_write
	(SocketHandle hd, const void *data, size_t len, size_t *out)
{
	ssize_t res = send(hd.fd, data, len, 0);

	if (res < 0)
		return error_from_errno(nv_error, 0, "send(fd = %d) failed", hd.fd);

	*out = res;
	return NULL;
}

const Error *socket_handle_read
	(SocketHandle hd, void *data, size_t len, size_t *out)
{
	ssize_t res = recv(hd.fd, data, len, 0);

	if (res < 0)
		return error_from_errno(nv_error, 0, "recv(fd = %d) failed", hd.fd);

	*out = res;
	return NULL;
}


