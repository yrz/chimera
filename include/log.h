/*
** $Id: log.h,v 1.13 2006/06/07 09:21:28 krishnap Exp $
**
** Matthew Allen
** description: 
*/

#ifndef _CHIMERA_LOG_H_
#define _CHIMERA_LOG_H_

enum
{
    LOG_ERROR,			/* error messages (stderr) */
    LOG_WARN,			/* warning messages (none) */
    LOG_DEBUG,			/* debugging messages (none) */
    LOG_KEYDEBUG,		/* debugging messages for key subsystem (none) */
    LOG_NETWORKDEBUG,		/* debugging messages for network layer (none) */
    LOG_ROUTING,		/* debugging the routing table (none) */
    LOG_SECUREDEBUG,		/* for security module (none) */
    LOG_DATA,			/* for measurement and analysis (none) */
    LOG_COUNT			/* count of log message types */
};


void *log_init ();
void log_message (void *logs, int type, char *format, ...);
void log_direct (void *logs, int type, FILE * fp);


#endif /* _CHIMERA_LOG_H_ */
