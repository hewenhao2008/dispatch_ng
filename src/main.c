/* main.c 
 * The main program
 * 
 * Copyright 2015 Akash Rawal
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

int main(int argc, char *argv[])
{
	int i, len;
	int bound = 0, iface_count = 0;
	
	//Call init functions
	utils_init();
	connector_init();
	
	//Read arguments
	len = strlen("--bind=");
	for (i = 1; i < argc; i++)
	{
		if ((strcmp(argv[i], "-h") == 0)
			|| (strcmp(argv[i], "--help") == 0))
		{
			printf("Usage: $0 [--bind=addr:port] "
				"addr1@metric1 addr2@metric2 ...\n");
			exit(1);
		}
		else if (strncmp(argv[i], "--bind=", len) == 0)
		{
			server_create(argv[i] + len);
			bound = 1;
		}
		else
		{
			balancer_add_from_string(argv[i]);
			iface_count++;
		}
	}
	
	if (! iface_count)
	{
		abort_with_error("No addresses to dispatch.");
	}
	
	//Default listening addresses
	if (! bound)
	{
		server_create("127.0.0.1:1080");
		server_create("[::1]:1080");
	}
	
	//Start dispatch
	event_base_loop(evbase, 0);
	
	return 0;
}
