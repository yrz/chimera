/*
** $Id: network.c,v 1.27 2006/06/07 09:21:29 krishnap Exp $
**
** Matthew Allen
** description: 
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <netdb.h>
#include <pthread.h>
#include "network.h"
#include "host.h"
#include "message.h"
#include "log.h"
#include "semaphore.h"
#include "jrb.h"
#include "jval.h"
#include "dtime.h"

extern int errno;
#define SEND_SIZE NETWORK_PACK_SIZE

typedef struct
{
    int sock;
    JRB waiting;
    unsigned long seqstart, seqend;
    pthread_mutex_t lock;
} NetworkGlobal;

/** network_address:
 ** returns the ip address of the #hostname#
 */
unsigned long network_address (void *networkglobal, char *hostname)
{

    int is_addr;
    struct hostent *he;
    unsigned long addr;
    unsigned long local;
    int i;
    NetworkGlobal *netglob = (NetworkGlobal *) networkglobal;


    /* apparently gethostbyname does not portably recognize ip addys */

#ifdef SunOS
    is_addr = inet_addr (hostname);
    if (is_addr == -1)
	is_addr = 0;
    else
	{
	    memcpy (&addr, (struct in_addr *) &is_addr, sizeof (addr));
	    is_addr = inet_addr ("127.0.0.1");
	    memcpy (&local, (struct in_addr *) &is_addr, sizeof (addr));
	    is_addr = 1;
	}
#else
    is_addr = inet_aton (hostname, (struct in_addr *) &addr);
    inet_aton ("127.0.0.1", (struct in_addr *) &local);
#endif

    pthread_mutex_lock (&(netglob->lock));
    if (is_addr)
	he = gethostbyaddr ((char *) &addr, sizeof (addr), AF_INET);
    else
	he = gethostbyname (hostname);

    if (he == NULL)
	{
	    pthread_mutex_unlock (&(netglob->lock));
	    return (0);
	}

    /* make sure the machine is not returning localhost */

    addr = *(unsigned long *) he->h_addr_list[0];
    for (i = 1; he->h_addr_list[i] != NULL && addr == local; i++)
	addr = *(unsigned long *) he->h_addr_list[i];
    pthread_mutex_unlock (&(netglob->lock));

    return (addr);

}

/** network_init:
 ** initiates the networking layer by creating socket and bind it to #port# 
 */

void *network_init (void *logs, int port)
{

    int sd;
    int ret;
    struct sockaddr_in saddr;
    int one;
    NetworkGlobal *ng;

    ng = (NetworkGlobal *) malloc (sizeof (NetworkGlobal));

    /* create socket */
    sd = socket (AF_INET, SOCK_DGRAM, 0);
    if (sd < 0)
	{
	    log_message (logs, LOG_ERROR, "network: socket: %s\n",
			 strerror (errno));
	    return (NULL);
	}
    if (setsockopt (sd, SOL_SOCKET, SO_REUSEADDR, (void *) &one, sizeof (one))
	== -1)
	{
	    log_message (logs, LOG_ERROR, "network: setsockopt: %s\n: ",
			 strerror (errno));
	    close (sd);
	    return (NULL);
	}

    /* attach socket to #port#. */
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl (INADDR_ANY);
    saddr.sin_port = htons ((short) port);
    if (bind (sd, (struct sockaddr *) &saddr, sizeof (saddr)) < 0)
	{
	    log_message (logs, LOG_ERROR, "network: bind: %s:\n",
			 strerror (errno));
	    close (sd);
	    return (NULL);
	}

    if ((ret = pthread_mutex_init (&(ng->lock), NULL)) != 0)
	{
	    log_message (logs, LOG_ERROR,
			 "network: pthread_mutex_init: %s:\n",
			 strerror (ret));
	    close (sd);
	    return (NULL);
	}

    ng->sock = sd;
    ng->waiting = make_jrb ();
    ng->seqstart = 0;
    ng->seqend = 0;

    return ((void *) ng);

}


/**
 ** network_activate: 
 ** NEVER RETURNS. Puts the network layer into listen mode. This thread
 ** manages acknowledgements, delivers incomming messages to the message
 ** handler, and drives the network layer. It should only be called once.
 */
void *network_activate (void *state)
{

    fd_set fds, thisfds;
    int ret, retack;
    char data[SEND_SIZE];
    struct sockaddr_in from;
    int socklen = sizeof (from);
    unsigned long type, seq;
    JRB node;
    void *semaphore;
    ChimeraState *chstate = (ChimeraState *) state;
    NetworkGlobal *ng = (NetworkGlobal *) chstate->network;


    FD_ZERO (&fds);
    FD_SET (ng->sock, &fds);

    while (1)
	{

	    /* block until information becomes available */
	    memcpy (&thisfds, &fds, sizeof (fd_set));
	    ret = select (ng->sock + 1, &thisfds, NULL, NULL, NULL);
	    if (ret < 0)
		{
		    log_message (chstate->log, LOG_ERROR,
				 "network: select: %s\n", strerror (errno));
		    continue;
		}

	    /* receive the new data */
	    ret =
		recvfrom (ng->sock, data, SEND_SIZE, 0,
			  (struct sockaddr *) &from, &socklen);
	    if (ret < 0)
		{
		    log_message (chstate->log, LOG_ERROR,
				 "network: recvfrom: %s\n", strerror (errno));
		    continue;
		}
	    memcpy (&type, data, sizeof (unsigned long));
	    type = ntohl (type);
	    memcpy (&seq, data + sizeof (unsigned long),
		    sizeof (unsigned long));
	    seq = ntohl (seq);

	    /* process acknowledgement */
	    if (type == 0)
		{
		    log_message (chstate->log, LOG_NETWORKDEBUG,
				 "network_activtae: recieved acknowledgement seq=%d from %s:%d\n",
				 seq, inet_ntoa (from.sin_addr),
				 from.sin_port);
		    pthread_mutex_lock (&(ng->lock));
		    node = jrb_find_int (ng->waiting, seq);
		    if (node != NULL)
			{
			    semaphore = node->val.v;
			    sema_v (semaphore);
			}
		    pthread_mutex_unlock (&(ng->lock));
		}

	    /* process recieve and send acknowledgement */
	    else if (type == 1)
		{
		    log_message (chstate->log, LOG_NETWORKDEBUG,
				 "network_activate: recieved message seq=%d  data:%s\n",
				 seq, data + (2 * sizeof (unsigned long)));
		    type = htonl (0);
		    memcpy (data, &type, sizeof (unsigned long));
		    retack =
			sendto (ng->sock, data, 2 * sizeof (unsigned long), 0,
				(struct sockaddr *) &from, sizeof (from));
		    if (retack < 0)
			{
			    log_message (chstate->log, LOG_ERROR,
					 "network: sendto: %s\n",
					 strerror (errno));
			    continue;
			}
		    log_message (chstate->log, LOG_NETWORKDEBUG,
				 "network_activate: sent out ack for  message seq=%d\n",
				 seq);
		    message_recieved (state,
				      data + (2 * sizeof (unsigned long)),
				      ret - (2 * sizeof (unsigned long)));
		}
	    else if (type == 2)
		{
		    message_recieved (state,
				      data + (2 * sizeof (unsigned long)),
				      ret - (2 * sizeof (unsigned long)));
		}
	    else
		{
		    log_message (chstate->log, LOG_ERROR,
				 "network: received unrecognized messagee type=%d seq=%d\n",
				 type, seq);
		}
	}

}

/**
 ** network_send: host, data, size
 ** Sends a message to host, updating the measurement info.
 */
int network_send (void *state, ChimeraHost * host, char *data, int size,
		  unsigned long type)
{

    struct sockaddr_in to;
    int ret, retval;
    unsigned long seq, ntype;
    char s[SEND_SIZE];
    void *semaphore;
    JRB node;
    double start;
    ChimeraState *chstate = (ChimeraState *) state;
    NetworkGlobal *ng;

    ng = (NetworkGlobal *) chstate->network;

    if (size > NETWORK_PACK_SIZE)
	{
	    log_message (chstate->log, LOG_ERROR,
			 "network_send: cannot send data over %lu bytes!\n",
			 NETWORK_PACK_SIZE);
	    return (0);
	}
    if (type != 1 && type != 2)
	{
	    log_message (chstate->log, LOG_ERROR,
			 "network_send: FAILED, unrecognized network data type %i !\n",
			 type);
	    return (0);
	}
    memset (&to, 0, sizeof (to));
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = host->address;
    to.sin_port = htons ((short) host->port);

    /* get sequence number and initialize acknowledgement semaphore */
    pthread_mutex_lock (&(ng->lock));
    if (type == 1)
	{			/* data should be acked */
	    semaphore = sema_create (0);
	    node =
		jrb_insert_int (ng->waiting, ng->seqend,
				new_jval_v (semaphore));
	}
    seq = htonl (ng->seqend);
    ng->seqend++;		/* needs to be fixed to modplus */
    pthread_mutex_unlock (&(ng->lock));

    /* create network header */
    ntype = htonl (type);
    memcpy (s, &ntype, sizeof (unsigned long));
    memcpy (s + sizeof (unsigned long), &seq, sizeof (unsigned long));
    memcpy (s + (2 * sizeof (unsigned long)), data, size);
    size += (2 * sizeof (unsigned long));

    /* send data */
    seq = ntohl (seq);
    log_message (chstate->log, LOG_NETWORKDEBUG,
		 "network_send: sending message seq=%d type=%d to %s:%d  data:%s\n",
		 seq, type, host->name, host->port, data);
    start = dtime ();

    ret = sendto (ng->sock, s, size, 0, (struct sockaddr *) &to, sizeof (to));
    log_message (chstate->log, LOG_NETWORKDEBUG,
		 "network_send: sent message: %s\n", s);
    if (ret < 0)
	{
	    log_message (chstate->log, LOG_ERROR,
			 "network_send: sendto: %s\n", strerror (errno));
	    host_update_stat (host, 0);
	    return (0);
	}

    if (type == 1)
	{
	    /* wait for ack */
	    log_message (chstate->log, LOG_NETWORKDEBUG,
			 "network_send: waiting for acknowledgement for seq=%d\n",
			 seq);
	    retval = sema_p (semaphore, TIMEOUT);
	    if (retval != 0)
		log_message (chstate->log, LOG_NETWORKDEBUG,
			     "network_send: acknowledgement timer seq=%d TIMEDOUT\n",
			     seq);
	    else
		log_message (chstate->log, LOG_NETWORKDEBUG,
			     "network_send: acknowledgement for seq=%d received\n",
			     seq);

	    pthread_mutex_lock (&(ng->lock));
	    sema_destroy (semaphore);
	    jrb_delete_node (node);
	    pthread_mutex_unlock (&(ng->lock));


	    if (retval != 0)
		{
		    host_update_stat (host, 0);
		    return (0);
		}

	    /* update latency info */
	    if (host->latency == 0.0)
		{
		    host->latency = dtime () - start;
		}
	    else
		{
		    host->latency =
			(0.9 * host->latency) + (0.1 * (dtime () - start));
		}
	}
    /* update link quality status */
    host_update_stat (host, 1);

    return (1);

}
