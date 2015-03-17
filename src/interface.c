/* interface.h
 * Handles outgoing addresses
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

#include "interface.h"


//Map AF_* values to PF_* values
static int find_namespace(struct sockaddr *addr)
{
	//Get namespace
	if (addr->sa_family == AF_LOCAL)
		return PF_LOCAL;
	if (addr->sa_family == AF_INET)
		return PF_INET;
	if (addr->sa_family == AF_INET6)
		return PF_INET6;
	
	abort_with_error("Unknown address format %d", addr->sa_family);
	return -1;
}

//Opens a socket bound to the interface.
int interface_open(Interface *interface)
{
	int fd;
	
	fd = socket(find_namespace(&(interface->addr)), SOCK_STREAM, 0);
	if (fd < 0)
		abort_with_liberror("socket()");
	
	if (bind(fd, &(interface->addr), interface->len) < 0)
		abort_with_liberror("bind()");
	
	g_atomic_int_inc(&(interface->use_count));
	
	return;
}

//Closes socket bound to the interface.
void interface_close(Interface *interface)
{
	g_atomic_int_add(&(interface->use_count), -1);
}

//Creates a new blank interface manager
InterfaceManager *interface_manager_new()
{
	InterfaceManager *manager = g_slice_new(InterfaceManager);
	int i;
	
	for (i = 0; i < INTERFACE_N_TYPES; i++)
	{
		manager->ifaces[i] = NULL;
	}
	
	return manager;
}

//Gets a suitable interface for address families
Interface *interface_manager_get
	(InterfaceManager *manager, int addr_type)
{
	Interface *selected = NULL, *iter;
	int i;
	
	for (i = 0; i < INTERFACE_N_TYPES; i++)
	{
		//Ignore interfaces not any of given types
		if (! (addr_type & (1 << i)))
			continue;
		
		//Select interface with minimum use count
		for (iter = manager->ifaces[i]; iter; iter = iter->next)
		{
			if (! selected)
				selected = iter;
			else if (iter->use_count < selected->use_count)
				selected = iter;
		}
	}
	
	return selected;
}

//Adds a dispatch address from string description
void interface_manager_add_from_string
	(InterfaceManager *manager, const char *desc)
{
	Interface *interface;
	char *desc_cp;
	int addr_type;
	
	desc_cp = g_strdup(desc);
	
	//First character will tell address type
	if (desc_cp[0] != '[')
	{
		//IPv4
		InterfaceInet4 *inet4_iface;
		
		//Allocate
		inet4_iface = (InterfaceInet4 *) 
			g_malloc(sizeof(InterfaceInet4));
		
		//Initialize
		inet4_iface->len = sizeof(struct sockaddr_in);
		inet4_iface->use_count = 0;
		inet4_iface->addr.sin_family = AF_INET;
		inet4_iface->addr.sin_port = 0;
		
		//Read address
		if (inet_pton(AF_INET, desc_cp, 
		              (void *) &(inet4_iface->addr.sin_addr)) != 1)
		{
			g_error("Invalid address %s", desc);
		}
		
		//Pass information out
		addr_type = INTERFACE_OFF_INET;
		interface = (Interface *) inet4_iface;
	}
	else
	{
		//IPv6
		InterfaceInet6 *inet6_iface;
		int addr_end;
		
		//Remove ending ']'
		for (addr_end = 0; desc_cp[addr_end]; addr_end++)
			if (desc_cp[addr_end] == ']')
				break;
		if (! desc_cp[addr_end])
			g_error("Invalid address %s", desc);
		desc_cp[addr_end] = 0;
		
		//Allocate
		inet6_iface = (InterfaceInet6 *) 
			g_malloc(sizeof(InterfaceInet6));
		
		//Initialize
		inet6_iface->len = sizeof(struct sockaddr_in6);
		inet6_iface->use_count = 0;
		inet6_iface->addr.sin6_family = AF_INET6;
		inet6_iface->addr.sin6_port = 0;
		
		//Read address
		if (inet_pton(AF_INET6, desc_cp + 1, 
		              (void *) &(inet6_iface->addr.sin6_addr)) != 1)
		{
			g_error("Invalid address %s", desc);
		}
		
		//Pass information out
		addr_type = INTERFACE_OFF_INET6;
		interface = (Interface *) inet6_iface;
	}
	
	g_free(desc_cp);
	
	//Add
	interface->next = manager->ifaces[addr_type];
	manager->ifaces[addr_type] = interface;
}

//Frees interface manager
void interface_manager_free(InterfaceManager *manager)
{
	int i;
	Interface *iter, *next;
	
	for (i = 0; i < INTERFACE_N_TYPES; i++)
	{
		for (iter = manager->ifaces[i]; iter; iter = next)
		{
			next = iter->next;
			
			g_free(iter);
		}
	}
	
	g_slice_free(InterfaceManager, manager);
}
