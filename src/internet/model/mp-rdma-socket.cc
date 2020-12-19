 /* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2007 INRIA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */
/* Created by Yuanwei Lu <ywlu@mail.ustc.edu.cn> 
 * 2016-04-09
 */ 

#define __STDC_LIMIT_MACROS

#include "ns3/object.h"
#include "ns3/log.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/boolean.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/nstime.h"
#include "mp-rdma-socket.h"

#define INIT_CWND 1 

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("MpRDMASocket");

NS_OBJECT_ENSURE_REGISTERED (MpRDMASocket);

const char* const
MpRDMASocket::MpRDMAStateName[MpRDMASocket::LAST_STATE] = { "CLOSED", "LISTEN", "SYN_SENT",
                                        "SYN_RCVD", "ESTABLISHED", "CLOSE_WAIT",
                                        "LAST_ACK", "FIN_WAIT_1", "FIN_WAIT_2",
                                        "CLOSING", "TIME_WAIT" };

TypeId
MpRDMASocket::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::MpRDMASocket")
    .SetParent<Socket> ()
    .SetGroupName ("Internet")
    .AddAttribute ("SndBufSize",
                   "MpRDMASocket maximum transmit buffer size (bytes)",
                   //UintegerValue (131072), // 128k
                   UintegerValue (128000000), //128MB 
                   MakeUintegerAccessor (&MpRDMASocket::GetSndBufSize,
                                         &MpRDMASocket::SetSndBufSize),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("RcvBufSize",
                   "MpRDMASocket maximum receive buffer size (bytes)",
                   UintegerValue (2000000000), //2GB 
                   //UintegerValue (131072),
                   MakeUintegerAccessor (&MpRDMASocket::GetRcvBufSize,
                                         &MpRDMASocket::SetRcvBufSize),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("SegmentSize",
                   "MP-RDMA maximum segment size in bytes (may be adjusted based on MTU discovery)",
                   UintegerValue (1436),
                   MakeUintegerAccessor (&MpRDMASocket::GetSegSize,
                                         &MpRDMASocket::SetSegSize),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("InitialSlowStartThreshold",
                   "MpRDMA initial slow start threshold (bytes)",
                   UintegerValue (UINT32_MAX),
                   MakeUintegerAccessor (&MpRDMASocket::GetInitialSSThresh,
                                         &MpRDMASocket::SetInitialSSThresh),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("InitialCwnd",
                   "MP-RDMA initial congestion window size (segments)",
                   UintegerValue (INIT_CWND), 
                   MakeUintegerAccessor (&MpRDMASocket::GetInitialCwnd,
                                         &MpRDMASocket::SetInitialCwnd),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("ConnTimeout",
                   "MP-RDMA retransmission timeout when opening connection (seconds)",
                   TimeValue (Seconds (3)),
                   MakeTimeAccessor (&MpRDMASocket::GetConnTimeout,
                                     &MpRDMASocket::SetConnTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("ConnCount",
                   "Number of connection attempts (SYN retransmissions) before "
                   "returning failure",
                   UintegerValue (6),
                   MakeUintegerAccessor (&MpRDMASocket::GetSynRetries,
                                         &MpRDMASocket::SetSynRetries),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("DataRetries",
                   "Number of data retransmission attempts",
                   UintegerValue (6),
                   MakeUintegerAccessor (&MpRDMASocket::GetDataRetries,
                                         &MpRDMASocket::SetDataRetries),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("DelAckTimeout",
                   "Timeout value for MP-RDMA delayed acks, in seconds",
                   TimeValue (Seconds (0.2)),
                   MakeTimeAccessor (&MpRDMASocket::GetDelAckTimeout,
                                     &MpRDMASocket::SetDelAckTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("DelAckCount",
                   "Number of packets to wait before sending a MP-RDMA ack",
                   UintegerValue (2),
                   MakeUintegerAccessor (&MpRDMASocket::GetDelAckMaxCount,
                                         &MpRDMASocket::SetDelAckMaxCount),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("MpRDMANoDelay", "Set to true to disable Nagle's algorithm",
                   BooleanValue (true),
                   MakeBooleanAccessor (&MpRDMASocket::GetMpRDMANoDelay,
                                        &MpRDMASocket::SetMpRDMANoDelay),
                   MakeBooleanChecker ())
    .AddAttribute ("PersistTimeout",
                   "Persist timeout to probe for rx window",
                   TimeValue (Seconds (6)),
                   MakeTimeAccessor (&MpRDMASocket::GetPersistTimeout,
                                     &MpRDMASocket::SetPersistTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("SenderOOL", "L for sender ooo control",
                   UintegerValue (1000),
                   MakeUintegerAccessor (&MpRDMASocket::m_sndL),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("ReceiverOOL", "L for receiver ooo control",
                   UintegerValue (1000),
                   MakeUintegerAccessor (&MpRDMASocket::m_rcvL),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("DeltaT", "delta t for inter-message ordering.",
                   DoubleValue (0.001),
                   MakeDoubleAccessor (&MpRDMASocket::m_deltaT),
                   MakeDoubleChecker <double> (0))
    .AddAttribute ("MessageSize", "Message Size for inter-message ordering",
                   UintegerValue (32),  //32 segments
                   MakeUintegerAccessor (&MpRDMASocket::m_messageSize),
                   MakeUintegerChecker<uint32_t> ())

  ;
  return tid;
}

MpRDMASocket::MpRDMASocket ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

MpRDMASocket::~MpRDMASocket ()
{
  NS_LOG_FUNCTION_NOARGS ();
}

} // namespace ns3
