/*
** $Id: network.h,v 1.13 2006/06/07 09:21:28 krishnap Exp $
**
** Matthew Allen
** description: 
*/

#ifndef _CHIMERA_NETWORK_H_
#define _CHIMERA_NETWORK_H_

#include "host.h"
#include "jrb.h"

/** 
 ** NETWORK_PACK_SIZE is the maximum packet size that will be handled by chimera network layer
 */
#define NETWORK_PACK_SIZE 65536
/** 
 ** TIMEOUT is the number of seconds to wait for receiving ack from the destination, if you want 
 ** the sender to wait forever put 0 for TIMEOUT. 
 */
#define TIMEOUT 1.0


/** network_address:
 ** returns the ip address of the #hostname#
 */
unsigned long network_address (void *networkglobal, char *hostname);

/** network_init:
 ** initiates the networking layer by creating socket and bind it to #port# 
 */
void *network_init (void *logs, int port);

/**
 ** network_activate: 
 ** NEVER RETURNS. Puts the network layer into listen mode. This thread
 ** manages acknowledgements, delivers incomming messages to the message
 ** handler, and drives the network layer. It should only be called once.
 */
void *network_activate (void *state);

/**
 ** network_send: host, data, size
 ** Sends a message to host, updating the measurement info.
 ** type are 1 or 2, 1 indicates that the data should be acknowledged by the
 ** receiver, and 2 indicates that no ack is necessary.
 */
int network_send (void *state, ChimeraHost * host, char *data, int size,
		  unsigned long type);

#endif /* _CHIMERA_NETWORK_H_ */
