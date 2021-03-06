/****************************************************************************
 * net/tcp/tcp.h
 *
 *   Copyright (C) 2014-2016, 2018 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

#ifndef _NET_TCP_TCP_H
#define _NET_TCP_TCP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <queue.h>

#include <nuttx/clock.h>
#include <nuttx/mm/iob.h>
#include <nuttx/net/ip.h>

#ifdef CONFIG_TCP_NOTIFIER
#  include <nuttx/wqueue.h>
#endif

#if defined(CONFIG_NET_TCP) && !defined(CONFIG_NET_TCP_NO_STACK)

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define NET_TCP_HAVE_STACK 1

/* Conditions for support TCP poll/select operations */

#if !defined(CONFIG_DISABLE_POLL) && defined(CONFIG_NET_TCP_READAHEAD)
#  define HAVE_TCP_POLL
#endif

/* Allocate a new TCP data callback */

/* These macros allocate and free callback structures used for receiving
 * notifications of TCP data-related events.
 */

#define tcp_callback_alloc(conn) \
  devif_callback_alloc((conn)->dev, &(conn)->list)
#define tcp_callback_free(conn,cb) \
  devif_conn_callback_free((conn)->dev, (cb), &(conn)->list)

#ifdef CONFIG_NET_TCP_WRITE_BUFFERS
/* TCP write buffer access macros */

#  define TCP_WBSEQNO(wrb)           ((wrb)->wb_seqno)
#  define TCP_WBPKTLEN(wrb)          ((wrb)->wb_iob->io_pktlen)
#  define TCP_WBSENT(wrb)            ((wrb)->wb_sent)
#  define TCP_WBNRTX(wrb)            ((wrb)->wb_nrtx)
#  define TCP_WBIOB(wrb)             ((wrb)->wb_iob)
#  define TCP_WBCOPYOUT(wrb,dest,n)  (iob_copyout(dest,(wrb)->wb_iob,(n),0))
#  define TCP_WBCOPYIN(wrb,src,n) \
     (iob_copyin((wrb)->wb_iob,src,(n),0,false))
#  define TCP_WBTRYCOPYIN(wrb,src,n) \
     (iob_trycopyin((wrb)->wb_iob,src,(n),0,false))

#  define TCP_WBTRIM(wrb,n) \
     do { (wrb)->wb_iob = iob_trimhead((wrb)->wb_iob,(n)); } while (0)

#ifdef CONFIG_DEBUG_FEATURES
#  define TCP_WBDUMP(msg,wrb,len,offset) \
     tcp_wrbuffer_dump(msg,wrb,len,offset)
#  else
#    define TCP_WBDUMP(msg,wrb,len,offset)
#  endif
#endif

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

/* Representation of a TCP connection.
 *
 * The tcp_conn_s structure is used for identifying a connection. All
 * but one field in the structure are to be considered read-only by an
 * application. The only exception is the 'private' fields whose purpose
 * is to let the application store application-specific state (e.g.,
 * file pointers) for the connection.
 */

struct net_driver_s;      /* Forward reference */
struct devif_callback_s;  /* Forward reference */
struct tcp_backlog_s;     /* Forward reference */
struct tcp_hdr_s;         /* Forward reference */

struct tcp_conn_s
{
  dq_entry_t node;        /* Implements a doubly linked list */
  union ip_binding_u u;   /* IP address binding */
  uint8_t  rcvseq[4];     /* The sequence number that we expect to
                           * receive next */
  uint8_t  sndseq[4];     /* The sequence number that was last sent by us */
  uint8_t  crefs;         /* Reference counts on this instance */
#if defined(CONFIG_NET_IPv4) && defined(CONFIG_NET_IPv6)
  uint8_t  domain;        /* IP domain: PF_INET or PF_INET6 */
#endif
  uint8_t  sa;            /* Retransmission time-out calculation state
                           * variable */
  uint8_t  sv;            /* Retransmission time-out calculation state
                           * variable */
  uint8_t  rto;           /* Retransmission time-out */
  uint8_t  tcpstateflags; /* TCP state and flags */
  uint8_t  timer;         /* The retransmission timer (units: half-seconds) */
  uint8_t  nrtx;          /* The number of retransmissions for the last
                           * segment sent */
  uint16_t lport;         /* The local TCP port, in network byte order */
  uint16_t rport;         /* The remoteTCP port, in network byte order */
  uint16_t mss;           /* Current maximum segment size for the
                           * connection */
  uint16_t winsize;       /* Current window size of the connection */
#ifdef CONFIG_NET_TCP_WRITE_BUFFERS
  uint32_t unacked;       /* Number bytes sent but not yet ACKed */
#else
  uint16_t unacked;       /* Number bytes sent but not yet ACKed */
#endif

  /* If the TCP socket is bound to a local address, then this is
   * a reference to the device that routes traffic on the corresponding
   * network.
   */

  FAR struct net_driver_s *dev;

#ifdef CONFIG_NET_TCP_READAHEAD
  /* Read-ahead buffering.
   *
   *   readahead - A singly linked list of type struct iob_qentry_s
   *               where the TCP/IP read-ahead data is retained.
   */

  struct iob_queue_s readahead;   /* Read-ahead buffering */
#endif

#ifdef CONFIG_NET_TCP_WRITE_BUFFERS
  /* Write buffering
   *
   *   write_q   - The queue of unsent I/O buffers.  The head of this
   *               list may be partially sent.  FIFO ordering.
   *   unacked_q - A queue of completely sent, but unacked I/O buffer
   *               chains.  Sequence number ordering.
   */

  sq_queue_t write_q;     /* Write buffering for segments */
  sq_queue_t unacked_q;   /* Write buffering for un-ACKed segments */
  uint16_t   expired;     /* Number segments retransmitted but not yet ACKed,
                           * it can only be updated at TCP_ESTABLISHED state */
  uint32_t   sent;        /* The number of bytes sent (ACKed and un-ACKed) */
  uint32_t   isn;         /* Initial sequence number */
  uint32_t   sndseq_max;  /* The sequence number of next not-retransmitted
                           * segment (next greater sndseq) */
#endif

#ifdef CONFIG_NET_TCPBACKLOG
  /* Listen backlog support
   *
   *   blparent - The backlog parent.  If this connection is backlogged,
   *     this field will be non-null and will refer to the TCP connection
   *     structure in which this connection is backlogged.
   *   backlog - The pending connection backlog.  If this connection is
   *     configured as a listener with backlog, then this refers to the
   *     struct tcp_backlog_s tear-off structure that manages that backlog.
   */

  FAR struct tcp_conn_s    *blparent;
  FAR struct tcp_backlog_s *backlog;
#endif

#ifdef CONFIG_NET_TCP_KEEPALIVE
  /* There fields manage TCP/IP keep-alive.  All times are in units of the
   * system clock tick.
   */

  clock_t    keeptime;    /* Last time that the TCP socket was known to be
                           * alive (ACK or data received) OR time that the
                           * last probe was sent. */
  uint16_t   keepidle;    /* Elapsed idle time before first probe sent (dsec) */
  uint16_t   keepintvl;   /* Interval between probes (dsec) */
  bool       keepalive;   /* True: KeepAlive enabled; false: disabled */
  uint8_t    keepcnt;     /* Number of retries before the socket is closed */
  uint8_t    keepretries; /* Number of retries attempted */
#endif

  /* Application callbacks:
   *
   * Data transfer events are retained in 'list'.  Event handlers in 'list'
   * are called for events specified in the flags set within struct
   * devif_callback_s
   *
   * When an callback is executed from 'list', the input flags are normally
   * returned, however, the implementation may set one of the following:
   *
   *   TCP_CLOSE   - Gracefully close the current connection
   *   TCP_ABORT   - Abort (reset) the current connection on an error that
   *                 prevents TCP_CLOSE from working.
   *
   * And/Or set/clear the following:
   *
   *   TCP_NEWDATA - May be cleared to indicate that the data was consumed
   *                 and that no further process of the new data should be
   *                 attempted.
   *   TCP_SNDACK  - If TCP_NEWDATA is cleared, then TCP_SNDACK may be set
   *                 to indicate that an ACK should be included in the response.
   *                 (In TCP_NEWDATA is cleared bu TCP_SNDACK is not set, then
   *                 dev->d_len should also be cleared).
   */

  FAR struct devif_callback_s *list;

  /* connevents is a list of callbacks for each socket the uses this
   * connection (there can be more that one in the event that the the socket
   * was dup'ed).  It is used with the network monitor to handle
   * asynchronous loss-of-connection events.
   */

  FAR struct devif_callback_s *connevents;

  /* accept() is called when the TCP logic has created a connection
   *
   *   accept_private: This is private data that will be available to the
   *     accept() handler when it is invoked with a point to this structure
   *     as an argument.
   *   accept: This is the pointer to the accept handler.
   */

  FAR void *accept_private;
  int (*accept)(FAR struct tcp_conn_s *listener, FAR struct tcp_conn_s *conn);
};

/* This structure supports TCP write buffering */

#ifdef CONFIG_NET_TCP_WRITE_BUFFERS
struct tcp_wrbuffer_s
{
  sq_entry_t wb_node;      /* Supports a singly linked list */
  uint32_t   wb_seqno;     /* Sequence number of the write segment */
  uint16_t   wb_sent;      /* Number of bytes sent from the I/O buffer chain */
  uint8_t    wb_nrtx;      /* The number of retransmissions for the last
                            * segment sent */
  struct iob_s *wb_iob;    /* Head of the I/O buffer chain */
};
#endif

/* Support for listen backlog:
 *
 *   struct tcp_blcontainer_s describes one backlogged connection
 *   struct tcp_backlog_s is a "tear-off" describing all backlog for a
 *      listener connection
 */

#ifdef CONFIG_NET_TCPBACKLOG
struct tcp_blcontainer_s
{
  sq_entry_t bc_node;             /* Implements a singly linked list */
  FAR struct tcp_conn_s *bc_conn; /* Holds reference to the new connection structure */
};

struct tcp_backlog_s
{
  sq_queue_t bl_free;             /* Implements a singly-linked list of free containers */
  sq_queue_t bl_pending;          /* Implements a singly-linked list of pending connections */
};
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef __cplusplus
#  define EXTERN extern "C"
extern "C"
{
#else
#  define EXTERN extern
#endif

/* List of registered Ethernet device drivers.  You must have the network
 * locked in order to access this list.
 *
 * NOTE that this duplicates a declaration in net/netdev/netdev.h
 */

EXTERN struct net_driver_s *g_netdevices;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

struct file;      /* Forward reference */
struct sockaddr;  /* Forward reference */
struct socket;    /* Forward reference */
struct pollfd;    /* Forward reference */

/****************************************************************************
 * Name: tcp_initialize
 *
 * Description:
 *   Initialize the TCP/IP connection structures.  Called only once and only
 *   from the network layer at start-up.
 *
 ****************************************************************************/

void tcp_initialize(void);

/****************************************************************************
 * Name: tcp_alloc
 *
 * Description:
 *   Find a free TCP/IP connection structure and allocate it
 *   for use.  This is normally something done by the implementation of the
 *   socket() API but is also called from the event processing logic when a
 *   TCP packet is received while "listening"
 *
 ****************************************************************************/

FAR struct tcp_conn_s *tcp_alloc(uint8_t domain);

/****************************************************************************
 * Name: tcp_free
 *
 * Description:
 *   Free a connection structure that is no longer in use. This should be
 *   done by the implementation of close()
 *
 ****************************************************************************/

void tcp_free(FAR struct tcp_conn_s *conn);

/****************************************************************************
 * Name: tcp_active
 *
 * Description:
 *   Find a connection structure that is the appropriate
 *   connection to be used with the provided TCP/IP header
 *
 * Assumptions:
 *   Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

FAR struct tcp_conn_s *tcp_active(FAR struct net_driver_s *dev,
                                  FAR struct tcp_hdr_s *tcp);

/****************************************************************************
 * Name: tcp_nextconn
 *
 * Description:
 *   Traverse the list of active TCP connections
 *
 * Assumptions:
 *   Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

FAR struct tcp_conn_s *tcp_nextconn(FAR struct tcp_conn_s *conn);

/****************************************************************************
 * Name: tcp_local_ipv4_device
 *
 * Description:
 *   Select the network driver to use with the IPv4 TCP transaction based
 *   on the locally bound IPv4 address
 *
 * Input Parameters:
 *   conn - TCP connection structure.  The locally bound address, laddr,
 *     should be set to a non-zero value in this structure.
 *
 * Returned Value:
 *   Zero (OK) is returned on success.  A negated errno value is returned
 *   on failure.  -ENETUNREACH is the only expected error value.
 *
 ****************************************************************************/

#ifdef CONFIG_NET_IPv4
int tcp_local_ipv4_device(FAR struct tcp_conn_s *conn);
#endif

/****************************************************************************
 * Name: tcp_remote_ipv4_device
 *
 * Description:
 *   Select the network driver to use with the IPv4 TCP transaction based
 *   on the remotely connected IPv4 address
 *
 * Input Parameters:
 *   conn - TCP connection structure.  The remotely conected address, raddr,
 *     should be set to a non-zero value in this structure.
 *
 * Returned Value:
 *   Zero (OK) is returned on success.  A negated errno value is returned
 *   on failure.  -ENETUNREACH is the only expected error value.
 *
 ****************************************************************************/

#ifdef CONFIG_NET_IPv4
int tcp_remote_ipv4_device(FAR struct tcp_conn_s *conn);
#endif

/****************************************************************************
 * Name: tcp_local_ipv6_device
 *
 * Description:
 *   Select the network driver to use with the IPv6 TCP transaction based
 *   on the locally bound IPv6 address
 *
 * Input Parameters:
 *   conn - TCP connection structure.  The locally bound address, laddr,
 *     should be set to a non-zero value in this structure.
 *
 * Returned Value:
 *   Zero (OK) is returned on success.  A negated errno value is returned
 *   on failure.  -EHOSTUNREACH is the only expected error value.
 *
 ****************************************************************************/

#ifdef CONFIG_NET_IPv6
int tcp_local_ipv6_device(FAR struct tcp_conn_s *conn);
#endif

/****************************************************************************
 * Name: tcp_remote_ipv6_device
 *
 * Description:
 *   Select the network driver to use with the IPv6 TCP transaction based
 *   on the remotely connected IPv6 address
 *
 * Input Parameters:
 *   conn - TCP connection structure.  The remotely connected address, raddr,
 *     should be set to a non-zero value in this structure.
 *
 * Returned Value:
 *   Zero (OK) is returned on success.  A negated errno value is returned
 *   on failure.  -EHOSTUNREACH is the only expected error value.
 *
 ****************************************************************************/

#ifdef CONFIG_NET_IPv6
int tcp_remote_ipv6_device(FAR struct tcp_conn_s *conn);
#endif

/****************************************************************************
 * Name: tcp_alloc_accept
 *
 * Description:
 *    Called when driver processing matches the incoming packet with a
 *    connection in LISTEN. In that case, this function will create a new
 *    connection and initialize it to send a SYNACK in return.
 *
 * Assumptions:
 *   Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

FAR struct tcp_conn_s *tcp_alloc_accept(FAR struct net_driver_s *dev,
                                        FAR struct tcp_hdr_s *tcp);

/****************************************************************************
 * Name: tcp_bind
 *
 * Description:
 *   This function implements the lower level parts of the standard TCP
 *   bind() operation.
 *
 * Returned Value:
 *   0 on success or -EADDRINUSE on failure
 *
 * Assumptions:
 *   This function is called from normal user level code.
 *
 ****************************************************************************/

int tcp_bind(FAR struct tcp_conn_s *conn, FAR const struct sockaddr *addr);

/****************************************************************************
 * Name: tcp_connect
 *
 * Description:
 *   This function implements the lower level parts of the standard
 *   TCP connect() operation:  It connects to a remote host using TCP.
 *
 *   This function is used to start a new connection to the specified
 *   port on the specified host. It uses the connection structure that was
 *   allocated by a preceding socket() call.  It sets the connection to
 *   the SYN_SENT state and sets the retransmission timer to 0. This will
 *   cause a TCP SYN segment to be sent out the next time this connection
 *   is periodically processed, which usually is done within 0.5 seconds
 *   after the call to tcp_connect().
 *
 * Assumptions:
 *   This function is called from normal user level code.
 *
 ****************************************************************************/

int tcp_connect(FAR struct tcp_conn_s *conn, FAR const struct sockaddr *addr);

/****************************************************************************
 * Name: psock_tcp_connect
 *
 * Description:
 *   Perform a TCP connection
 *
 * Input Parameters:
 *   psock - A reference to the socket structure of the socket to be connected
 *   addr  - The address of the remote server to connect to
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   The network is locked
 *
 ****************************************************************************/

int psock_tcp_connect(FAR struct socket *psock,
                      FAR const struct sockaddr *addr);

/****************************************************************************
 * Name: tcp_start_monitor
 *
 * Description:
 *   Set up to receive TCP connection state changes for a given socket
 *
 * Input Parameters:
 *   psock - The socket of interest
 *
 * Returned Value:
 *   On success, tcp_start_monitor returns OK; On any failure,
 *   tcp_start_monitor will return a negated errno value.  The only failure
 *   that can occur is if the socket has already been closed and, in this
 *   case, -ENOTCONN is returned.
 *
 * Assumptions:
 *   The caller holds the network lock (if not, it will be locked momentarily
 *   by this function).
 *
 ****************************************************************************/

int tcp_start_monitor(FAR struct socket *psock);

/****************************************************************************
 * Name: tcp_stop_monitor
 *
 * Description:
 *   Stop monitoring TCP connection changes for a sockets associated with
 *   a given TCP connection structure.
 *
 * Input Parameters:
 *   conn - The TCP connection of interest
 *   flags    Set of disconnection events
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   The caller holds the network lock (if not, it will be locked momentarily
 *   by this function).
 *
 ****************************************************************************/

void tcp_stop_monitor(FAR struct tcp_conn_s *conn, uint16_t flags);

/****************************************************************************
 * Name: tcp_close_monitor
 *
 * Description:
 *   One socket in a group of dup'ed sockets has been closed.  We need to
 *   selectively terminate just those things that are waiting of events
 *   from this specific socket.  And also recover any resources that are
 *   committed to monitoring this socket.
 *
 * Input Parameters:
 *   psock - The TCP socket structure that is closed
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   The caller holds the network lock (if not, it will be locked momentarily
 *   by this function).
 *
 ****************************************************************************/

void tcp_close_monitor(FAR struct socket *psock);

/****************************************************************************
 * Name: tcp_lost_connection
 *
 * Description:
 *   Called when a loss-of-connection event has been detected by network
 *   event handling logic.  Perform operations like tcp_stop_monitor but
 *   (1) explicitly mark this socket and (2) disable further callbacks
 *   the event handler.
 *
 * Input Parameters:
 *   psock - The TCP socket structure associated.
 *   cb    - devif callback structure
 *   flags - Set of connection events events
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   The caller holds the network lock (if not, it will be locked momentarily
 *   by this function).
 *
 ****************************************************************************/

void tcp_lost_connection(FAR struct socket *psock,
                         FAR struct devif_callback_s *cb, uint16_t flags);

/****************************************************************************
 * Name: tcp_ipv4_select
 *
 * Description:
 *   Configure to send or receive an TCP IPv4 packet
 *
 ****************************************************************************/

#ifdef CONFIG_NET_IPv4
void tcp_ipv4_select(FAR struct net_driver_s *dev);
#endif

/****************************************************************************
 * Name: tcp_ipv6_select
 *
 * Description:
 *   Configure to send or receive an TCP IPv6 packet
 *
 ****************************************************************************/

#ifdef CONFIG_NET_IPv6
void tcp_ipv6_select(FAR struct net_driver_s *dev);
#endif

/****************************************************************************
 * Name: tcp_setsequence
 *
 * Description:
 *   Set the TCP/IP sequence number
 *
 * Assumptions:
 *   Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

void tcp_setsequence(FAR uint8_t *seqno, uint32_t value);

/****************************************************************************
 * Name: tcp_getsequence
 *
 * Description:
 *   Get the TCP/IP sequence number
 *
 * Assumptions:
 *   Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

uint32_t tcp_getsequence(FAR uint8_t *seqno);

/****************************************************************************
 * Name: tcp_addsequence
 *
 * Description:
 *   Add the length to get the next TCP sequence number.
 *
 * Assumptions:
 *   Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

uint32_t tcp_addsequence(FAR uint8_t *seqno, uint16_t len);

/****************************************************************************
 * Name: tcp_initsequence
 *
 * Description:
 *   Set the (initial) the TCP/IP sequence number when a TCP connection is
 *   established.
 *
 * Assumptions:
 *   Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

void tcp_initsequence(FAR uint8_t *seqno);

/****************************************************************************
 * Name: tcp_nextsequence
 *
 * Description:
 *   Increment the TCP/IP sequence number
 *
 * Assumptions:
 *   Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

void tcp_nextsequence(void);

/****************************************************************************
 * Name: tcp_poll
 *
 * Description:
 *   Poll a TCP connection structure for availability of TX data
 *
 * Input Parameters:
 *   dev - The device driver structure to use in the send operation
 *   conn - The TCP "connection" to poll for TX data
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

void tcp_poll(FAR struct net_driver_s *dev, FAR struct tcp_conn_s *conn);

/****************************************************************************
 * Name: tcp_timer
 *
 * Description:
 *   Handle a TCP timer expiration for the provided TCP connection
 *
 * Input Parameters:
 *   dev  - The device driver structure to use in the send operation
 *   conn - The TCP "connection" to poll for TX data
 *   hsec - The polling interval in halves of a second
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

void tcp_timer(FAR struct net_driver_s *dev, FAR struct tcp_conn_s *conn,
               int hsec);

/****************************************************************************
 * Name: tcp_listen_initialize
 *
 * Description:
 *   Setup the listening data structures
 *
 * Assumptions:
 *   Called early in the initialization phase while the system is still
 *   single-threaded.
 *
 ****************************************************************************/

void tcp_listen_initialize(void);

/****************************************************************************
 * Name: tcp_findlistener
 *
 * Description:
 *   Return the connection listener for connections on this port (if any)
 *
 * Assumptions:
 *   The network is locked
 *
 ****************************************************************************/

#if defined(CONFIG_NET_IPv4) && defined(CONFIG_NET_IPv6)
FAR struct tcp_conn_s *tcp_findlistener(uint16_t portno, uint8_t domain);
#else
FAR struct tcp_conn_s *tcp_findlistener(uint16_t portno);
#endif

/****************************************************************************
 * Name: tcp_unlisten
 *
 * Description:
 *   Stop listening to the port bound to the specified TCP connection
 *
 * Assumptions:
 *   Called from normal user code.
 *
 ****************************************************************************/

int tcp_unlisten(FAR struct tcp_conn_s *conn);

/****************************************************************************
 * Name: tcp_listen
 *
 * Description:
 *   Start listening to the port bound to the specified TCP connection
 *
 * Assumptions:
 *   Called from normal user code.
 *
 ****************************************************************************/

int tcp_listen(FAR struct tcp_conn_s *conn);

/****************************************************************************
 * Name: tcp_islistener
 *
 * Description:
 *   Return true is there is a listener for the specified port
 *
 * Assumptions:
 *   Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

#if defined(CONFIG_NET_IPv4) && defined(CONFIG_NET_IPv6)
bool tcp_islistener(uint16_t portno, uint8_t domain);
#else
bool tcp_islistener(uint16_t portno);
#endif

/****************************************************************************
 * Name: tcp_accept_connection
 *
 * Description:
 *   Accept the new connection for the specified listening port.
 *
 * Assumptions:
 *   Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

int tcp_accept_connection(FAR struct net_driver_s *dev,
                          FAR struct tcp_conn_s *conn, uint16_t portno);

/****************************************************************************
 * Name: tcp_send
 *
 * Description:
 *   Setup to send a TCP packet
 *
 * Input Parameters:
 *   dev    - The device driver structure to use in the send operation
 *   conn   - The TCP connection structure holding connection information
 *   flags  - flags to apply to the TCP header
 *   len    - length of the message
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

void tcp_send(FAR struct net_driver_s *dev, FAR struct tcp_conn_s *conn,
              uint16_t flags, uint16_t len);

/****************************************************************************
 * Name: tcp_sendfile
 *
 * Description:
 *   The tcp_sendfile() call may be used only when the INET socket is in a
 *   connected state (so that the intended recipient is known).
 *
 * Input Parameters:
 *   psock    An instance of the internal socket structure.
 *   buf      Data to send
 *   len      Length of data to send
 *   flags    Send flags
 *
 * Returned Value:
 *   On success, returns the number of characters sent.  On  error,
 *   a negated errno value is returned.  See sendfile() for a list
 *   appropriate error return values.
 *
 ****************************************************************************/

#ifdef CONFIG_NET_SENDFILE
ssize_t tcp_sendfile(FAR struct socket *psock, FAR struct file *infile,
                      FAR off_t *offset, size_t count);
#endif

/****************************************************************************
 * Name: tcp_reset
 *
 * Description:
 *   Send a TCP reset (no-data) message
 *
 * Input Parameters:
 *   dev    - The device driver structure to use in the send operation
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

void tcp_reset(FAR struct net_driver_s *dev);

/****************************************************************************
 * Name: tcp_ack
 *
 * Description:
 *   Send the SYN or SYNACK response.
 *
 * Input Parameters:
 *   dev  - The device driver structure to use in the send operation
 *   conn - The TCP connection structure holding connection information
 *   ack  - The ACK response to send
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

void tcp_ack(FAR struct net_driver_s *dev, FAR struct tcp_conn_s *conn,
             uint8_t ack);

/****************************************************************************
 * Name: tcp_appsend
 *
 * Description:
 *   Handle application or TCP protocol response.  If this function is called
 *   with dev->d_sndlen > 0, then this is an application attempting to send
 *   packet.
 *
 * Input Parameters:
 *   dev    - The device driver structure to use in the send operation
 *   conn   - The TCP connection structure holding connection information
 *   result - App result event sent
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

void tcp_appsend(FAR struct net_driver_s *dev, FAR struct tcp_conn_s *conn,
                 uint16_t result);

/****************************************************************************
 * Name: tcp_rexmit
 *
 * Description:
 *   Handle application retransmission
 *
 * Input Parameters:
 *   dev    - The device driver structure to use in the send operation
 *   conn   - The TCP connection structure holding connection information
 *   result - App result event sent
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

void tcp_rexmit(FAR struct net_driver_s *dev, FAR struct tcp_conn_s *conn,
                uint16_t result);

/****************************************************************************
 * Name: tcp_ipv4_input
 *
 * Description:
 *   Handle incoming TCP input with IPv4 header
 *
 * Input Parameters:
 *   dev - The device driver structure containing the received TCP packet.
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   Called from the Ethernet driver with the network stack locked
 *
 ****************************************************************************/

#ifdef CONFIG_NET_IPv4
void tcp_ipv4_input(FAR struct net_driver_s *dev);
#endif

/****************************************************************************
 * Name: tcp_ipv6_input
 *
 * Description:
 *   Handle incoming TCP input with IPv4 header
 *
 * Input Parameters:
 *   dev   - The device driver structure containing the received TCP packet.
 *   iplen - The size of the IPv6 header.  This may be larger than
 *           IPv6_HDRLEN the IPv6 header if IPv6 extension headers are
 *           present.
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   Called from the Ethernet driver with the network stack locked
 *
 ****************************************************************************/

#ifdef CONFIG_NET_IPv6
void tcp_ipv6_input(FAR struct net_driver_s *dev, unsigned int iplen);
#endif

/****************************************************************************
 * Name: tcp_callback
 *
 * Description:
 *   Inform the application holding the TCP socket of a change in state.
 *
 * Assumptions:
 *   Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

uint16_t tcp_callback(FAR struct net_driver_s *dev,
                      FAR struct tcp_conn_s *conn, uint16_t flags);

/****************************************************************************
 * Name: tcp_datahandler
 *
 * Description:
 *   Handle data that is not accepted by the application.  This may be called
 *   either (1) from the data receive logic if it cannot buffer the data, or
 *   (2) from the TCP event logic is there is no listener in place ready to
 *   receive the data.
 *
 * Input Parameters:
 *   conn - A pointer to the TCP connection structure
 *   buffer - A pointer to the buffer to be copied to the read-ahead
 *     buffers
 *   buflen - The number of bytes to copy to the read-ahead buffer.
 *
 * Returned Value:
 *   The number of bytes actually buffered is returned.  This will be either
 *   zero or equal to buflen; partial packets are not buffered.
 *
 * Assumptions:
 * - The caller has checked that TCP_NEWDATA is set in flags and that is no
 *   other handler available to process the incoming data.
 * - Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

#ifdef CONFIG_NET_TCP_READAHEAD
uint16_t tcp_datahandler(FAR struct tcp_conn_s *conn, FAR uint8_t *buffer,
                         uint16_t nbytes);
#endif

/****************************************************************************
 * Name: tcp_backlogcreate
 *
 * Description:
 *   Called from the listen() logic to setup the backlog as specified in the
 *   the listen arguments.
 *
 * Assumptions:
 *   Called from network socket logic.  The network may or may not be locked.
 *
 ****************************************************************************/

#ifdef CONFIG_NET_TCPBACKLOG
int tcp_backlogcreate(FAR struct tcp_conn_s *conn, int nblg);
#else
#  define tcp_backlogcreate(c,n) (-ENOSYS)
#endif

/****************************************************************************
 * Name: tcp_backlogdestroy
 *
 * Description:
 *   (1) Called from tcp_free() whenever a connection is freed.
 *   (2) Called from tcp_backlogcreate() to destroy any old backlog
 *
 *   NOTE: This function may re-enter tcp_free when a connection that
 *   is freed that has pending connections.
 *
 * Assumptions:
 *   Called from network socket logic with the network stack locked
 *
 ****************************************************************************/

#ifdef CONFIG_NET_TCPBACKLOG
int tcp_backlogdestroy(FAR struct tcp_conn_s *conn);
#else
#  define tcp_backlogdestroy(conn)     (-ENOSYS)
#endif

/****************************************************************************
 * Name: tcp_backlogadd
 *
 * Description:
 *  Called tcp_listen when a new connection is made with a listener socket
 *  but when there is no accept() in place to receive the connection.  This
 *  function adds the new connection to the backlog.
 *
 * Assumptions:
 *   Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

#ifdef CONFIG_NET_TCPBACKLOG
int tcp_backlogadd(FAR struct tcp_conn_s *conn,
                   FAR struct tcp_conn_s *blconn);
#else
#  define tcp_backlogadd(conn,blconn)  (-ENOSYS)
#endif

/****************************************************************************
 * Name: tcp_backlogavailable
 *
 * Description:
 *   Called from poll().  Before waiting for a new connection, poll will
 *   call this API to see if there are pending connections in the backlog.
 *
 * Assumptions:
 *   Thne network is locked.
 *
 ****************************************************************************/

#if defined(CONFIG_NET_TCPBACKLOG) && !defined(CONFIG_DISABLE_POLL)
bool tcp_backlogavailable(FAR struct tcp_conn_s *conn);
#else
#  define tcp_backlogavailable(c) (false);
#endif

/****************************************************************************
 * Name: tcp_backlogremove
 *
 * Description:
 *   Called from accept().  Before waiting for a new connection, accept will
 *   call this API to see if there are pending connections in the backlog.
 *
 * Assumptions:
 *   The network is locked.
 *
 ****************************************************************************/

#ifdef CONFIG_NET_TCPBACKLOG
FAR struct tcp_conn_s *tcp_backlogremove(FAR struct tcp_conn_s *conn);
#else
#  define tcp_backlogremove(c) (NULL)
#endif

/****************************************************************************
 * Name: tcp_backlogdelete
 *
 * Description:
 *  Called from tcp_free() when a connection is freed that this also
 *  retained in the pending connection list of a listener.  We simply need
 *  to remove the defunct connection from the list.
 *
 * Assumptions:
 *   Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

#ifdef CONFIG_NET_TCPBACKLOG
int tcp_backlogdelete(FAR struct tcp_conn_s *conn,
                      FAR struct tcp_conn_s *blconn);
#else
#  define tcp_backlogdelete(c,b) (-ENOSYS)
#endif

/****************************************************************************
 * Name: tcp_accept
 *
 * Description:
 *   This function implements accept() for TCP/IP sockets.  See the
 *   description of accept() for further information.
 *
 * Input Parameters:
 *   psock    The listening TCP socket structure
 *   addr     Receives the address of the connecting client
 *   addrlen  Input: allocated size of 'addr', Return: returned size of 'addr'
 *   newconn  The new, accepted TCP connection structure
 *
 * Returned Value:
 *   Returns zero (OK) on success or a negated errno value on failure.
 *   See the description of accept of the possible errno values in the
 *   description of accept().
 *
 * Assumptions:
 *   Network is locked.
 *
 ****************************************************************************/

int psock_tcp_accept(FAR struct socket *psock, FAR struct sockaddr *addr,
                     FAR socklen_t *addrlen, FAR void **newconn);

/****************************************************************************
 * Name: psock_tcp_send
 *
 * Description:
 *   The psock_tcp_send() call may be used only when the TCP socket is in a
 *   connected state (so that the intended recipient is known).
 *
 * Input Parameters:
 *   psock    An instance of the internal socket structure.
 *   buf      Data to send
 *   len      Length of data to send
 *
 * Returned Value:
 *   On success, returns the number of characters sent.  On  error,
 *   -1 is returned, and errno is set appropriately:
 *
 *   EAGAIN or EWOULDBLOCK
 *     The socket is marked non-blocking and the requested operation
 *     would block.
 *   EBADF
 *     An invalid descriptor was specified.
 *   ECONNRESET
 *     Connection reset by peer.
 *   EDESTADDRREQ
 *     The socket is not connection-mode, and no peer address is set.
 *   EFAULT
 *      An invalid user space address was specified for a parameter.
 *   EINTR
 *      A signal occurred before any data was transmitted.
 *   EINVAL
 *      Invalid argument passed.
 *   EISCONN
 *     The connection-mode socket was connected already but a recipient
 *     was specified. (Now either this error is returned, or the recipient
 *     specification is ignored.)
 *   EMSGSIZE
 *     The socket type requires that message be sent atomically, and the
 *     size of the message to be sent made this impossible.
 *   ENOBUFS
 *     The output queue for a network interface was full. This generally
 *     indicates that the interface has stopped sending, but may be
 *     caused by transient congestion.
 *   ENOMEM
 *     No memory available.
 *   ENOTCONN
 *     The socket is not connected, and no target has been given.
 *   ENOTSOCK
 *     The argument s is not a socket.
 *   EPIPE
 *     The local end has been shut down on a connection oriented socket.
 *     In this case the process will also receive a SIGPIPE unless
 *     MSG_NOSIGNAL is set.
 *
 * Assumptions:
 *
 ****************************************************************************/

struct socket;
ssize_t psock_tcp_send(FAR struct socket *psock, FAR const void *buf,
                       size_t len);

/****************************************************************************
 * Name: tcp_setsockopt
 *
 * Description:
 *   tcp_setsockopt() sets the TCP-protocol option specified by the
 *   'option' argument to the value pointed to by the 'value' argument for
 *   the socket specified by the 'psock' argument.
 *
 *   See <netinet/tcp.h> for the a complete list of values of TCP protocol
 *   options.
 *
 * Input Parameters:
 *   psock     Socket structure of socket to operate on
 *   option    identifies the option to set
 *   value     Points to the argument value
 *   value_len The length of the argument value
 *
 * Returned Value:
 *   Returns zero (OK) on success.  On failure, it returns a negated errno
 *   value to indicate the nature of the error.  See psock_setcockopt() for
 *   the list of possible error values.
 *
 ****************************************************************************/

#ifdef CONFIG_NET_TCPPROTO_OPTIONS
int tcp_setsockopt(FAR struct socket *psock, int option,
                   FAR const void *value, socklen_t value_len);
#endif

/****************************************************************************
 * Name: tcp_getsockopt
 *
 * Description:
 *   tcp_getsockopt() retrieves the value for the option specified by the
 *   'option' argument for the socket specified by the 'psock' argument.  If
 *   the size of the option value is greater than 'value_len', the value
 *   stored in the object pointed to by the 'value' argument will be silently
 *   truncated. Otherwise, the length pointed to by the 'value_len' argument
 *   will be modified to indicate the actual length of the 'value'.
 *
 *   The 'level' argument specifies the protocol level of the option. To
 *   retrieve options at the socket level, specify the level argument as
 *   SOL_SOCKET; to retrieve options at the TCP-protocol level, the level
 *   argument is SOL_CP.
 *
 *   See <sys/socket.h> a complete list of values for the socket-level
 *   'option' argument.  Protocol-specific options are are protocol specific
 *   header files (such as netinet/tcp.h for the case of the TCP protocol).
 *
 * Input Parameters:
 *   psock     Socket structure of the socket to query
 *   level     Protocol level to set the option
 *   option    identifies the option to get
 *   value     Points to the argument value
 *   value_len The length of the argument value
 *
 * Returned Value:
 *   Returns zero (OK) on success.  On failure, it returns a negated errno
 *   value to indicate the nature of the error.  See psock_getsockopt() for
 *   the complete list of appropriate return error codes.
 *
 ****************************************************************************/

#ifdef CONFIG_NET_TCPPROTO_OPTIONS
int tcp_getsockopt(FAR struct socket *psock, int option,
                   FAR void *value, FAR socklen_t *value_len);
#endif

/****************************************************************************
 * Name: tcp_get_recvwindow
 *
 * Description:
 *   Calculate the TCP receive window for the specified device.
 *
 * Input Parameters:
 *   dev - The device whose TCP receive window will be updated.
 *
 * Returned Value:
 *   The value of the TCP receive window to use.
 *
 ****************************************************************************/

uint16_t tcp_get_recvwindow(FAR struct net_driver_s *dev);

/****************************************************************************
 * Name: psock_tcp_cansend
 *
 * Description:
 *   psock_tcp_cansend() returns a value indicating if a write to the socket
 *   would block.  No space in the buffer is actually reserved, so it is
 *   possible that the write may still block if the buffer is filled by
 *   another means.
 *
 * Input Parameters:
 *   psock    An instance of the internal socket structure.
 *
 * Returned Value:
 *   OK
 *     At least one byte of data could be successfully written.
 *   -EWOULDBLOCK
 *     There is no room in the output buffer.
 *   -EBADF
 *     An invalid descriptor was specified.
 *   -ENOTCONN
 *     The socket is not connected.
 *
 ****************************************************************************/

int psock_tcp_cansend(FAR struct socket *psock);

/****************************************************************************
 * Name: tcp_wrbuffer_initialize
 *
 * Description:
 *   Initialize the list of free write buffers
 *
 * Assumptions:
 *   Called once early initialization.
 *
 ****************************************************************************/

#ifdef CONFIG_NET_TCP_WRITE_BUFFERS
void tcp_wrbuffer_initialize(void);
#endif /* CONFIG_NET_TCP_WRITE_BUFFERS */

/****************************************************************************
 * Name: tcp_wrbuffer_alloc
 *
 * Description:
 *   Allocate a TCP write buffer by taking a pre-allocated buffer from
 *   the free list.  This function is called from TCP logic when a buffer
 *   of TCP data is about to sent
 *
 * Input Parameters:
 *   None
 *
 * Assumptions:
 *   Called from user logic with the network locked.
 *
 ****************************************************************************/

#ifdef CONFIG_NET_TCP_WRITE_BUFFERS
struct tcp_wrbuffer_s;

FAR struct tcp_wrbuffer_s *tcp_wrbuffer_alloc(void);

/****************************************************************************
 * Name: tcp_wrbuffer_tryalloc
 *
 * Description:
 *   Try to allocate a TCP write buffer by taking a pre-allocated buffer from
 *   the free list.  This function is called from TCP logic when a buffer
 *   of TCP data is about to be sent if the socket is non-blocking. Returns
 *   immediately if allocation fails.
 *
 * Input parameters:
 *   None
 *
 * Assumptions:
 *   Called from user logic with the network locked.
 *
 ****************************************************************************/

FAR struct tcp_wrbuffer_s *tcp_wrbuffer_tryalloc(void);
#endif /* CONFIG_NET_TCP_WRITE_BUFFERS */

/****************************************************************************
 * Name: tcp_wrbuffer_release
 *
 * Description:
 *   Release a TCP write buffer by returning the buffer to the free list.
 *   This function is called from user logic after it is consumed the
 *   buffered data.
 *
 * Assumptions:
 *   Called from network stack logic with the network stack locked
 *
 ****************************************************************************/

#ifdef CONFIG_NET_TCP_WRITE_BUFFERS
void tcp_wrbuffer_release(FAR struct tcp_wrbuffer_s *wrb);
#endif /* CONFIG_NET_TCP_WRITE_BUFFERS */

/****************************************************************************
 * Name: tcp_wrbuffer_test
 *
 * Description:
 *   Check if there is room in the write buffer.  Does not reserve any space.
 *
 * Assumptions:
 *   None.
 *
 ****************************************************************************/

#ifdef CONFIG_NET_TCP_WRITE_BUFFERS
int tcp_wrbuffer_test(void);
#endif /* CONFIG_NET_TCP_WRITE_BUFFERS */

/****************************************************************************
 * Name: tcp_wrbuffer_dump
 *
 * Description:
 *   Dump the contents of a write buffer.
 *
 ****************************************************************************/

#ifdef CONFIG_NET_TCP_WRITE_BUFFERS
#ifdef CONFIG_DEBUG_FEATURES
void tcp_wrbuffer_dump(FAR const char *msg, FAR struct tcp_wrbuffer_s *wrb,
                       unsigned int len, unsigned int offset);
#else
#  define tcp_wrbuffer_dump(msg,wrb)
#endif
#endif /* CONFIG_NET_TCP_WRITE_BUFFERS */

/****************************************************************************
 * Name: tcp_pollsetup
 *
 * Description:
 *   Setup to monitor events on one TCP/IP socket
 *
 * Input Parameters:
 *   psock - The TCP/IP socket of interest
 *   fds   - The structure describing the events to be monitored, OR NULL if
 *           this is a request to stop monitoring events.
 *
 * Returned Value:
 *  0: Success; Negated errno on failure
 *
 ****************************************************************************/

#ifdef HAVE_TCP_POLL
int tcp_pollsetup(FAR struct socket *psock, FAR struct pollfd *fds);
#endif

/****************************************************************************
 * Name: tcp_pollteardown
 *
 * Description:
 *   Teardown monitoring of events on an TCP/IP socket
 *
 * Input Parameters:
 *   psock - The TCP/IP socket of interest
 *   fds   - The structure describing the events to be monitored, OR NULL if
 *           this is a request to stop monitoring events.
 *
 * Returned Value:
 *  0: Success; Negated errno on failure
 *
 ****************************************************************************/

#ifdef HAVE_TCP_POLL
int tcp_pollteardown(FAR struct socket *psock, FAR struct pollfd *fds);
#endif

/****************************************************************************
 * Name: tcp_readahead_notifier_setup
 *
 * Description:
 *   Set up to perform a callback to the worker function when an TCP data
 *   is added to the read-ahead buffer.  The worker function will execute
 *   on the high priority worker thread.
 *
 * Input Parameters:
 *   worker - The worker function to execute on the high priority work
 *            queue when data is available in the TCP read-ahead buffer.
 *   conn  - The TCP connection where read-ahead data is needed.
 *   arg    - A user-defined argument that will be available to the worker
 *            function when it runs.
 *
 * Returned Value:
 *   > 0   - The signal notification is in place.  The returned value is a
 *           key that may be used later in a call to
 *           tcp_notifier_teardown().
 *   == 0  - There is already buffered read-ahead data.  No signal
 *           notification will be provided.
 *   < 0   - An unexpected error occurred and no signal will be sent.  The
 *           returned value is a negated errno value that indicates the
 *           nature of the failure.
 *
 ****************************************************************************/

#ifdef CONFIG_TCP_NOTIFIER
int tcp_readahead_notifier_setup(worker_t worker,
                                 FAR struct tcp_conn_s *conn,
                                 FAR void *arg);
#endif

/****************************************************************************
 * Name: tcp_readahead_disconnect_setup
 *
 * Description:
 *   Set up to perform a callback to the worker function if the TCP
 *   connection is lost.
 *
 * Input Parameters:
 *   worker - The worker function to execute on the high priority work
 *            queue when data is available in the TCP read-ahead buffer.
 *   conn  - The TCP connection where read-ahead data is needed.
 *   arg    - A user-defined argument that will be available to the worker
 *            function when it runs.
 *
 * Returned Value:
 *   > 0   - The signal notification is in place.  The returned value is a
 *           key that may be used later in a call to
 *           tcp_notifier_teardown().
 *   == 0  - There is already buffered read-ahead data.  No signal
 *           notification will be provided.
 *   < 0   - An unexpected error occurred and no signal will be sent.  The
 *           returned value is a negated errno value that indicates the
 *           nature of the failure.
 *
 ****************************************************************************/

#ifdef CONFIG_TCP_NOTIFIER
int tcp_readahead_disconnect_setup(worker_t worker,
                                   FAR struct tcp_conn_s *conn,
                                   FAR void *arg);
#endif

/****************************************************************************
 * Name: tcp_notifier_teardown
 *
 * Description:
 *   Eliminate a TCP read-ahead notification previously setup by
 *   tcp_readahead_notifier_setup().  This function should only be called
 *   if the notification should be aborted prior to the notification.  The
 *   notification will automatically be torn down after the signal is sent.
 *
 * Input Parameters:
 *   key - The key value returned from a previous call to
 *         tcp_readahead_notifier_setup().
 *
 * Returned Value:
 *   Zero (OK) is returned on success; a negated errno value is returned on
 *   any failure.
 *
 ****************************************************************************/

#ifdef CONFIG_TCP_NOTIFIER
int tcp_notifier_teardown(int key);
#endif

/****************************************************************************
 * Name: tcp_readahead_signal
 *
 * Description:
 *   Read-ahead data has been buffered.  Signal all threads waiting for
 *   read-ahead data to become available.
 *
 *   When read-ahead data becomes available, *all* of the workers waiting
 *   for read-ahead data will be executed.  If there are multiple workers
 *   waiting for read-ahead data then only the first to execute will get the
 *   data.  Others will need to call tcp_readahead_notifier_setup() once
 *   again.
 *
 * Input Parameters:
 *   conn  - The TCP connection where read-ahead data was just buffered.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

#ifdef CONFIG_TCP_NOTIFIER
void tcp_readahead_signal(FAR struct tcp_conn_s *conn);
#endif

/****************************************************************************
 * Name: tcp_disconnect_signal
 *
 * Description:
 *   The TCP connection has been lost.  Signal all threads monitoring TCP
 *   state events.
 *
 * Input Parameters:
 *   conn  - The TCP connection where read-ahead data was just buffered.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

#ifdef CONFIG_TCP_NOTIFIER
void tcp_disconnect_signal(FAR struct tcp_conn_s *conn);
#endif

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* CONFIG_NET_TCP && !CONFIG_NET_TCP_NO_STACK */
#endif /* _NET_TCP_TCP_H */
