/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2007 Georgia Tech Research Corporation
 * Copyright (c) 2010 Adrian Sai-wah Tam
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
 * Author: Adrian Sai-wah Tam <adrian.sw.tam@gmail.com>
 */
/* Created by Yuanwei Lu <ywlu@mail.ustc.edu.cn> 
 * 2016-04-10
 */ 

#define NS_LOG_APPEND_CONTEXT \
  if (m_node) { std::clog << " [node " << m_node->GetId () << "] "; }

#include "ns3/abort.h"
#include "ns3/node.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/log.h"
#include "ns3/ipv4.h"
#include "ns3/ipv6.h"
#include "ns3/ipv4-interface-address.h"
#include "ns3/ipv4-route.h"
#include "ns3/ipv6-route.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv6-routing-protocol.h"
#include "ns3/simulation-singleton.h"
#include "ns3/simulator.h"
#include "ns3/packet.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/pointer.h"
#include "ns3/trace-source-accessor.h"
#include "mp-rdma-socket-impl.h"
#include "mp-rdma-l4-protocol.h"
#include "ipv4-end-point.h"
#include "ipv6-end-point.h"
#include "ipv6-l3-protocol.h"
#include "mp-rdma-header.h"
#include "tcp-option-winscale.h"
#include "tcp-option-ts.h"
#include "rtt-estimator.h"
#include "ns3/ecn-tag.h"
#include "ns3/pathid-tag.h"
#include "ns3/timestamp-tag.h" 
#include "ns3/retx-tag.h" 
#include "ns3/fence-tag.h" 
#include "ns3/log-manager.h" 
#include "ns3/aack-tag.h"

#include <math.h>
#include <algorithm>

#define PRINT_DEBUG_INFO false//wsq 

#define PATH_NUM 8  

#define SS_STATE 0 
#define CA_STATE 1 

#define MINIMAL 1

#define ENABLE_AACK 0

#define L_RATIO     0.5
#define RCV_L_VALUE 1000
#define SND_L_VALUE 1000
//#define SND_L_VALUE (RCV_L_VALUE * L_RATIO)

#define SINGLE_PATH_ACK 0
#define ENABLE_PROBING  0

#define PENALIZE_BAD_PATH 1

#define TEST_DELTA_T 0
#define N_MESSAGES 32
#define PKT_QUOTA 100

//lyj add
#define SENDER_RETX    1

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("MpRDMASocketImpl");

NS_OBJECT_ENSURE_REGISTERED (MpRDMASocketImpl);

TypeId
MpRDMASocketImpl::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::MpRDMASocketImpl")
    .SetParent<MpRDMASocket> ()
    .SetGroupName ("Internet")
    .AddConstructor<MpRDMASocketImpl> ()
//    .AddAttribute ("MpRDMAState", "State in MP-RDMA state machine",
//                   TypeId::ATTR_GET,
//                   EnumValue (CLOSED),
//                   MakeEnumAccessor (&MpRDMASocketImpl::m_state),
//                   MakeEnumChecker (CLOSED, "Closed"))
    .AddAttribute ("MaxSegLifetime",
                   "Maximum segment lifetime in seconds, use for TIME_WAIT state transition to CLOSED state",
                   DoubleValue (120), /* RFC793 says MSL=2 minutes*/
                   MakeDoubleAccessor (&MpRDMASocketImpl::m_msl),
                   MakeDoubleChecker<double> (0))
    .AddAttribute ("MaxWindowSize", "Max size of advertised window",
                   UintegerValue (65535),
                   MakeUintegerAccessor (&MpRDMASocketImpl::m_maxWinSize),
                   MakeUintegerChecker<uint16_t> ())
    .AddAttribute ("IcmpCallback", "Callback invoked whenever an icmp error is received on this socket.",
                   CallbackValue (),
                   MakeCallbackAccessor (&MpRDMASocketImpl::m_icmpCallback),
                   MakeCallbackChecker ())
    .AddAttribute ("IcmpCallback6", "Callback invoked whenever an icmpv6 error is received on this socket.",
                   CallbackValue (),
                   MakeCallbackAccessor (&MpRDMASocketImpl::m_icmpCallback6),
                   MakeCallbackChecker ())
    .AddAttribute ("WindowScaling", "Enable or disable Window Scaling option",
                   BooleanValue (true),
                   MakeBooleanAccessor (&MpRDMASocketImpl::m_winScalingEnabled),
                   MakeBooleanChecker ())
    .AddAttribute ("Timestamp", "Enable or disable Timestamp option",
                   BooleanValue (true),
                   MakeBooleanAccessor (&MpRDMASocketImpl::m_timestampEnabled),
                   MakeBooleanChecker ())
    .AddAttribute ("MinRto",
                   "Minimum retransmit timeout value",
                   TimeValue (Seconds (0.01)), // RFC 6298 says min RTO=1 sec, but Linux uses 200ms.
                   // See http://www.postel.org/pipermail/end2end-interest/2004-November/004402.html
                   MakeTimeAccessor (&MpRDMASocketImpl::SetMinRto,
                                     &MpRDMASocketImpl::GetMinRto),
                   MakeTimeChecker ())
    .AddAttribute ("ClockGranularity",
                   "Clock Granularity used in RTO calculations",
                   TimeValue (MilliSeconds (1)), // RFC6298 suggest to use fine clock granularity
                   MakeTimeAccessor (&MpRDMASocketImpl::SetClockGranularity,
                                     &MpRDMASocketImpl::GetClockGranularity),
                   MakeTimeChecker ())
    .AddAttribute ("TxBuffer",
                   "MP-RDMA Tx buffer",
                   PointerValue (),
                   MakePointerAccessor (&MpRDMASocketImpl::GetTxBuffer),
                   MakePointerChecker<TcpTxBuffer> ())
    .AddAttribute ("RxBuffer",
                   "MP-RDMA Rx buffer", 
                   PointerValue (),
                   MakePointerAccessor (&MpRDMASocketImpl::GetRxBuffer),
                   MakePointerChecker<TcpRxBuffer> ())
    .AddAttribute ("ReTxThreshold", "Threshold for fast retransmit",
                   UintegerValue (3),
                   MakeUintegerAccessor (&MpRDMASocketImpl::m_retxThresh),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("LimitedTransmit", "Enable limited transmit",
                   BooleanValue (true),
                   MakeBooleanAccessor (&MpRDMASocketImpl::m_limitedTx),
                   MakeBooleanChecker ())
    .AddAttribute ("OldPathProbability", "Probability to choose old path",
                   DoubleValue (0.0),  
                   MakeDoubleAccessor (&MpRDMASocketImpl::m_oldPathProbability),
                   MakeDoubleChecker<double> (0)) 
    .AddAttribute ("MaxPathNum", "The maximum number of paths",
                   UintegerValue (PATH_NUM), 
                   MakeUintegerAccessor (&MpRDMASocketImpl::m_pathNum), 
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("FastRecoveryAlpha", "Alpha for fast recovery threshold",
                   DoubleValue (1.5),  
                   MakeDoubleAccessor (&MpRDMASocketImpl::m_fastAlpha), 
                   MakeDoubleChecker<double> (0)) 
    //.AddAttribute ("SenderOOL", "L for sender ooo control",
    //               UintegerValue (SND_L_VALUE),
    //               MakeUintegerAccessor (&MpRDMASocketImpl::m_sndL),
    //               MakeUintegerChecker<uint32_t> ()) 
    //.AddAttribute ("ReceiverOOL", "L for receiver ooo control",
    //               UintegerValue (RCV_L_VALUE),
    //               MakeUintegerAccessor (&MpRDMASocketImpl::m_rcvL),
    //               MakeUintegerChecker<uint32_t> ()) 
    .AddAttribute ("SndUnaReTxed", "SND.UNA retxed",
                   BooleanValue (false),
                   MakeBooleanAccessor (&MpRDMASocketImpl::m_unaReTxed),
                   MakeBooleanChecker ())
    .AddAttribute ("ReTxSendThreshold", "Threshold for send retransmit",
                   UintegerValue (1000),
                   MakeUintegerAccessor (&MpRDMASocketImpl::retx_thresold),
                   MakeUintegerChecker<uint32_t> ())
    .AddTraceSource ("RTO",
                     "Retransmission timeout",
                     MakeTraceSourceAccessor (&MpRDMASocketImpl::m_rto),
                     "ns3::Time::TracedValueCallback")
    .AddTraceSource ("RTT",
                     "Last RTT sample",
                     MakeTraceSourceAccessor (&MpRDMASocketImpl::m_lastRtt),
                     "ns3::Time::TracedValueCallback")
    .AddTraceSource ("NextTxSequence",
                     "Next sequence number to send (SND.NXT)",
                     MakeTraceSourceAccessor (&MpRDMASocketImpl::m_nextTxSequence),
                     "ns3::SequenceNumber32TracedValueCallback")
    .AddTraceSource ("HighestSequence",
                     "Highest sequence number ever sent in socket's life time",
                     MakeTraceSourceAccessor (&MpRDMASocketImpl::m_highTxMark),
                     "ns3::SequenceNumber32TracedValueCallback")
    .AddTraceSource ("State",
                     "MP-RDMA state",
                     MakeTraceSourceAccessor (&MpRDMASocketImpl::m_state),
                     "ns3::MpRDMAStatesTracedValueCallback")
    .AddTraceSource ("CongState",
                     "MP-RDMA Congestion machine state",
                     MakeTraceSourceAccessor (&MpRDMASocketImpl::m_congStateTrace),
                     "ns3::MpRDMASocketState::MpRDMACongStatesTracedValueCallback")
    .AddTraceSource ("RWND",
                     "Remote side's flow control window",
                     MakeTraceSourceAccessor (&MpRDMASocketImpl::m_rWnd),
                     "ns3::TracedValueCallback::Uint32")
    .AddTraceSource ("BytesInFlight",
                     "Socket estimation of bytes in flight",
                     MakeTraceSourceAccessor (&MpRDMASocketImpl::m_bytesInFlight),
                     "ns3::TracedValueCallback::Uint32")
    .AddTraceSource ("HighestRxSequence",
                     "Highest sequence number received from peer",
                     MakeTraceSourceAccessor (&MpRDMASocketImpl::m_highRxMark),
                     "ns3::SequenceNumber32TracedValueCallback")
    .AddTraceSource ("HighestRxAck",
                     "Highest ack received from peer",
                     MakeTraceSourceAccessor (&MpRDMASocketImpl::m_highRxAckMark),
                     "ns3::SequenceNumber32TracedValueCallback")
    .AddTraceSource ("CongestionWindow",
                     "The MP-RDMA connection's congestion window",
                     MakeTraceSourceAccessor (&MpRDMASocketImpl::m_cWndTrace),
                     "ns3::TracedValueCallback::Uint32")
    .AddTraceSource ("SlowStartThreshold",
                     "MP-RDMA slow start threshold (bytes)",
                     MakeTraceSourceAccessor (&MpRDMASocketImpl::m_ssThTrace),
                     "ns3::TracedValueCallback::Uint32")
    .AddTraceSource ("Tx",
                     "Send MP-RDMA packet to IP protocol",
                     MakeTraceSourceAccessor (&MpRDMASocketImpl::m_txTrace),
                     "ns3::MpRDMASocketImpl::MpRDMATxRxTracedCallback")
    .AddTraceSource ("Rx",
                     "Receive MP-RDMA packet from IP protocol",
                     MakeTraceSourceAccessor (&MpRDMASocketImpl::m_rxTrace),
                     "ns3::MpRDMASocketImpl::MpRDMATxRxTracedCallback") 
  ;
  return tid;
}

TypeId
MpRDMASocketImpl::GetInstanceTypeId () const
{
  return MpRDMASocketImpl::GetTypeId ();
}


TypeId
MpRDMASocketState::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::MpRDMASocketState")
    .SetParent<Object> ()
    .SetGroupName ("Internet")
    .AddConstructor <MpRDMASocketState> ()
    .AddTraceSource ("CongestionWindow",
                     "The MP-RDMA connection's congestion window",
                     MakeTraceSourceAccessor (&MpRDMASocketState::m_cWnd),
                     "ns3::TracedValue::Uint32Callback")
    .AddTraceSource ("SlowStartThreshold",
                     "MP-RDMA slow start threshold (bytes)",
                     MakeTraceSourceAccessor (&MpRDMASocketState::m_ssThresh),
                     "ns3::TracedValue::Uint32Callback")
    .AddTraceSource ("CongState",
                     "MP-RDMA Congestion machine state",
                     MakeTraceSourceAccessor (&MpRDMASocketState::m_congState),
                     "ns3::TracedValue::MpRDMACongStatesTracedValueCallback") 
  ;
  return tid;
}

MpRDMASocketState::MpRDMASocketState (void)
  : Object (),
    m_cWnd (0),
    m_ssThresh (0),
    m_initialCWnd (0),
    m_initialSsThresh (0),
    m_segmentSize (0),
    m_congState (CA_OPEN)
{
}

MpRDMASocketState::MpRDMASocketState (const MpRDMASocketState &other)
  : Object (other),
    m_cWnd (other.m_cWnd),
    m_ssThresh (other.m_ssThresh),
    m_initialCWnd (other.m_initialCWnd),
    m_initialSsThresh (other.m_initialSsThresh),
    m_segmentSize (other.m_segmentSize),
    m_congState (other.m_congState)
{
}

const char* const
MpRDMASocketState::MpRDMACongStateName[MpRDMASocketState::CA_LAST_STATE] =
{
  "CA_OPEN", "CA_DISORDER", "CA_CWR", "CA_RECOVERY", "CA_LOSS"
};

MpRDMASocketImpl::MpRDMASocketImpl (void)
  : MpRDMASocket (),
    m_retxEvent (),
    m_lastAckEvent (),
    m_delAckEvent (),
    m_persistEvent (),
    m_timewaitEvent (),
    m_dupAckCount (0),
    m_delAckCount (0),
    m_delAckMaxCount (0),
    m_noDelay (false),
    m_synCount (0),
    m_synRetries (0),
    m_dataRetrCount (0),
    m_dataRetries (0),
    m_rto (Seconds (0.0)),
    //m_minRto (Time::Max ()),
    m_minRto (Seconds(0.1)),
    m_clockGranularity (Seconds (0.001)),
    m_lastRtt (Seconds (0.0)),
    m_delAckTimeout (Seconds (0.0)),
    m_persistTimeout (Seconds (0.0)),
    m_cnTimeout (Seconds (0.0)),
    m_endPoint (0),
    m_endPoint6 (0),
    m_node (0),
    m_mpRDMA (0), 
    m_rtt (0),
    m_nextTxSequence (0), // Change this for non-zero initial sequence number
    m_highTxMark (0),
    m_state (CLOSED),
    m_errno (ERROR_NOTERROR),
    m_closeNotified (false),
    m_closeOnEmpty (false),
    m_shutdownSend (false),
    m_shutdownRecv (false),
    m_connected (false),
    m_msl (0),
    m_maxWinSize (0),
    m_rWnd (0),
    m_highRxMark (0),
    m_highTxAck (0),
    m_highRxAckMark (0),
    m_bytesAckedNotProcessed (0),
    m_bytesInFlight (0),
    m_winScalingEnabled (false),
    m_rcvWindShift (0),
    m_sndWindShift (0),
    m_timestampEnabled (true),
    m_timestampToEcho (0),
    m_sendPendingDataEvent (),
    m_recover (0), // Set to the initial sequence number
    m_retxThresh (3),
    m_limitedTx (false),
    m_retransOut (0),
    m_congestionControl (0),
    m_isFirstPartialAck (true),
    m_lastAckPathId(0),
    m_maxTxNum(0),  
    m_maxPathId(0),
    m_path1(0), 
    m_path2(0), 
    m_path3(0), 
    m_oldPathProbability(0), 
    m_pathNum(1),
    m_fastAlpha(1.5),
    m_pipe(0), 
    m_highReTxMark(0), 
    m_fastRecoveryThreshold(128000),
    m_aackSeq(0), 
    m_mpRdmaRto(Seconds(0.01)), 
    macroRTOEvent(), 
    m_rWndTimeStamp(0), 
    m_reTxSuspCount(0), 
    m_reTxOppCount(0),
    //m_sndL(SND_L_VALUE),
    //m_rcvL(RCV_L_VALUE),
    m_ooP(0),
    m_ooL(0),
    m_sndMax(0),
    m_inflate(0),
    m_unaReTxed(0),
    m_probe (0),
    m_probeOpCount(0),
    m_notInflateCount(0),
    m_move_ooL(0),
    m_bReTx(0),
    m_bSendPkt(0),
    m_logOOO(1),
    m_wait(0),
    m_markFence(0),
    m_totalFenceCount(0),
    m_violateFenceCount(0),
    m_pktTotalQuota(0),
    m_recoveryPoint(0),
    m_totalSendPackets(0),
    m_probingPackets(0),
    m_startRecordProbe(1),
    mLogCwnd(1),
    m_sendretx(false),
    m_detect(0),
    m_High_resend_pos(0),
    retx_thresold(1000),
    m_oversendretx(0),
    m_startsendretx(0)
{
  NS_LOG_FUNCTION (this);
  m_rxBuffer = CreateObject<TcpRxBuffer> ();
  m_txBuffer = CreateObject<TcpTxBuffer> ();
  m_tcb      = CreateObject<TcpSocketState> ();

  m_senderState = InitState; 

  bool ok;

  ok = m_tcb->TraceConnectWithoutContext ("CongestionWindow",
                                          MakeCallback (&MpRDMASocketImpl::UpdateCwnd, this));
  NS_ASSERT (ok == true);

  ok = m_tcb->TraceConnectWithoutContext ("SlowStartThreshold",
                                          MakeCallback (&MpRDMASocketImpl::UpdateSsThresh, this));
  NS_ASSERT (ok == true);

  //ok = m_tcb->TraceConnectWithoutContext ("CongState",
  //                                        MakeCallback (&MpRDMASocketImpl::UpdateCongState, this));
  NS_ASSERT (ok == true);

  for(uint32_t i = 0; i < 10; i++)
  {
      normal_ACK[i] = 1; 
      ecn_ACK[i] = 0; 
  }

  for(uint32_t i = 0; i < 10; i++)
  {
      pathRTT[i] = 0;
      RTTCount[i] = 0; 
  }

  enqueue_ECN = 0; 

  begin_ACK_record = false; 
 
  ObjectFactory rttFactory;
  TypeId rttTypeId = RttMeanDeviation::GetTypeId (); 
  rttFactory.SetTypeId (rttTypeId);

  Ptr<RttEstimator> pathRTT = rttFactory.Create<RttEstimator> (); 
  m_allPathRTT = pathRTT; 
}

MpRDMASocketImpl::MpRDMASocketImpl (const MpRDMASocketImpl& sock)
  : MpRDMASocket (sock),
    //copy object::m_tid and socket::callbacks
    m_dupAckCount (sock.m_dupAckCount),
    m_delAckCount (0),
    m_delAckMaxCount (sock.m_delAckMaxCount),
    m_noDelay (sock.m_noDelay),
    m_synCount (sock.m_synCount),
    m_synRetries (sock.m_synRetries),
    m_dataRetrCount (sock.m_dataRetrCount),
    m_dataRetries (sock.m_dataRetries),
    m_rto (sock.m_rto),
    m_minRto (sock.m_minRto),
    m_clockGranularity (sock.m_clockGranularity),
    m_lastRtt (sock.m_lastRtt),
    m_delAckTimeout (sock.m_delAckTimeout),
    m_persistTimeout (sock.m_persistTimeout),
    m_cnTimeout (sock.m_cnTimeout),
    m_endPoint (0),
    m_endPoint6 (0),
    m_node (sock.m_node),
    m_mpRDMA (sock.m_mpRDMA), 
    m_nextTxSequence (sock.m_nextTxSequence),
    m_highTxMark (sock.m_highTxMark),
    m_state (sock.m_state),
    m_errno (sock.m_errno),
    m_closeNotified (sock.m_closeNotified),
    m_closeOnEmpty (sock.m_closeOnEmpty),
    m_shutdownSend (sock.m_shutdownSend),
    m_shutdownRecv (sock.m_shutdownRecv),
    m_connected (sock.m_connected),
    m_msl (sock.m_msl),
    m_maxWinSize (sock.m_maxWinSize),
    m_rWnd (sock.m_rWnd),
    m_highRxMark (sock.m_highRxMark),
    m_highRxAckMark (sock.m_highRxAckMark),
    m_bytesAckedNotProcessed (sock.m_bytesAckedNotProcessed),
    m_bytesInFlight (sock.m_bytesInFlight),
    m_winScalingEnabled (sock.m_winScalingEnabled),
    m_rcvWindShift (sock.m_rcvWindShift),
    m_sndWindShift (sock.m_sndWindShift),
    m_timestampEnabled (sock.m_timestampEnabled),
    m_timestampToEcho (sock.m_timestampToEcho),
    m_recover (sock.m_recover),
    m_retxThresh (sock.m_retxThresh),
    m_limitedTx (sock.m_limitedTx),
    m_retransOut (sock.m_retransOut),
    m_isFirstPartialAck (sock.m_isFirstPartialAck),
    m_txTrace (sock.m_txTrace),
    m_rxTrace (sock.m_rxTrace), 
    m_lastAckPathId(0), 
    m_maxTxNum(0),  
    m_maxPathId(0),
    m_path1(0), 
    m_path2(0), 
    m_path3(0),  
    m_oldPathProbability(0), 
    m_pathNum(1), 
    m_fastAlpha(1.5), 
    m_pipe(0), 
    m_highReTxMark(0), 
    m_fastRecoveryThreshold(128000), 
    m_aackSeq(0), 
    m_mpRdmaRto(Seconds(0.01)), 
    m_rWndTimeStamp(0),  
    m_reTxSuspCount(0), 
    m_reTxOppCount(0),
    //m_sndL(SND_L_VALUE),
    //m_rcvL(RCV_L_VALUE),
    m_ooP(0),
    m_ooL(0),
    m_sndMax(0),
    m_inflate(0),
    m_unaReTxed(0),
    m_probe (0),
    m_probeOpCount(0),
    m_notInflateCount(0),
    m_move_ooL(0),
    m_bReTx(0),
    m_bSendPkt(0),
    m_logOOO(1),
    m_wait(0),
    m_markFence(0),
    m_totalFenceCount(0),
    m_violateFenceCount(0),
    m_pktTotalQuota(0),
    m_recoveryPoint(0),
    m_totalSendPackets(0),
    m_probingPackets(0),
    m_startRecordProbe(1)
{
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC ("Invoked the copy constructor");
  // Copy the rtt estimator if it is set
  if (sock.m_rtt)
    {
      m_rtt = sock.m_rtt->Copy ();
    }
  
  if (sock.m_allPathRTT)
    {
      m_allPathRTT = sock.m_allPathRTT->Copy (); 
    }
  // Reset all callbacks to null
  Callback<void, Ptr< Socket > > vPS = MakeNullCallback<void, Ptr<Socket> > ();
  Callback<void, Ptr<Socket>, const Address &> vPSA = MakeNullCallback<void, Ptr<Socket>, const Address &> ();
  Callback<void, Ptr<Socket>, uint32_t> vPSUI = MakeNullCallback<void, Ptr<Socket>, uint32_t> ();
  SetConnectCallback (vPS, vPS);
  SetDataSentCallback (vPSUI);
  SetSendCallback (vPSUI);
  SetRecvCallback (vPS);
  m_txBuffer = CopyObject (sock.m_txBuffer);
  m_rxBuffer = CopyObject (sock.m_rxBuffer);
  m_tcb = CopyObject (sock.m_tcb);
  if (sock.m_congestionControl)
    {
      m_congestionControl = sock.m_congestionControl->Fork ();
    }

  bool ok;

  ok = m_tcb->TraceConnectWithoutContext ("CongestionWindow",
                                          MakeCallback (&MpRDMASocketImpl::UpdateCwnd, this));
  NS_ASSERT (ok == true);

  ok = m_tcb->TraceConnectWithoutContext ("SlowStartThreshold",
                                          MakeCallback (&MpRDMASocketImpl::UpdateSsThresh, this));
  NS_ASSERT (ok == true);

  //ok = m_tcb->TraceConnectWithoutContext ("CongState",
  //                                        MakeCallback (&MpRDMASocketImpl::UpdateCongState, this));
  NS_ASSERT (ok == true);
}

MpRDMASocketImpl::~MpRDMASocketImpl (void)
{
  NS_LOG_FUNCTION (this);
  m_node = 0;
  if (m_endPoint != 0)
    {
      NS_ASSERT (m_mpRDMA != 0);
      /*
       * Upon Bind, an Ipv4Endpoint is allocated and set to m_endPoint, and
       * DestroyCallback is set to MpRDMASocketImpl::Destroy. If we called
       * m_mpRDMA->DeAllocate, it wil destroy its Ipv4EndpointDemux::DeAllocate,
       * which in turn destroys my m_endPoint, and in turn invokes
       * MpRDMASocketImpl::Destroy to nullify m_node, m_endPoint, and m_mpRDMA.
       */
      NS_ASSERT (m_endPoint != 0);
      m_mpRDMA->DeAllocate (m_endPoint);
      NS_ASSERT (m_endPoint == 0);
    }
  if (m_endPoint6 != 0)
    {
      NS_ASSERT (m_mpRDMA != 0);
      NS_ASSERT (m_endPoint6 != 0);
      m_mpRDMA->DeAllocate (m_endPoint6);
      NS_ASSERT (m_endPoint6 == 0);
    }
  m_mpRDMA = 0;
  CancelAllTimers ();
}

/* Associate a node with this MP-RDMA socket */ 
void
MpRDMASocketImpl::SetNode (Ptr<Node> node)
{
  m_node = node;
}

/* Associate the L4 protocol (e.g. mux/demux) with this socket */
void
MpRDMASocketImpl::SetMpRDMA (Ptr<MpRDMAL4Protocol> mpRDMA)
{
  m_mpRDMA = mpRDMA; 
}

/* Set an RTT estimator with this socket */
void
MpRDMASocketImpl::SetRtt (Ptr<RttEstimator> rtt)
{
  m_rtt = rtt;
}

/* Inherit from Socket class: Returns error code */
enum Socket::SocketErrno
MpRDMASocketImpl::GetErrno (void) const
{
  return m_errno;
}

/* Inherit from Socket class: Returns socket type, NS3_SOCK_STREAM */
enum Socket::SocketType
MpRDMASocketImpl::GetSocketType (void) const
{
  return NS3_SOCK_STREAM;
}

/* Inherit from Socket class: Returns associated node */
Ptr<Node>
MpRDMASocketImpl::GetNode (void) const
{
  NS_LOG_FUNCTION_NOARGS ();
  return m_node;
}

/* Inherit from Socket class: Bind socket to an end-point in MpRDMAL4Protocol */ 
int
MpRDMASocketImpl::Bind (void)
{
  NS_LOG_FUNCTION (this);
  m_endPoint = m_mpRDMA->Allocate ();
  if (0 == m_endPoint)
    {
      m_errno = ERROR_ADDRNOTAVAIL;
      return -1;
    }

  m_mpRDMA->AddSocket (this);

  return SetupCallback ();
}

int
MpRDMASocketImpl::Bind6 (void)
{
  NS_LOG_FUNCTION (this);
  m_endPoint6 = m_mpRDMA->Allocate6 ();
  if (0 == m_endPoint6)
    {
      m_errno = ERROR_ADDRNOTAVAIL;
      return -1;
    }

  m_mpRDMA->AddSocket (this);

  return SetupCallback ();
}

/* Inherit from Socket class: Bind socket (with specific address) to an end-point in MpRDMAL4Protocol */
int
MpRDMASocketImpl::Bind (const Address &address)
{
  NS_LOG_FUNCTION (this << address);
  if (InetSocketAddress::IsMatchingType (address))
    {
      InetSocketAddress transport = InetSocketAddress::ConvertFrom (address);
      Ipv4Address ipv4 = transport.GetIpv4 ();
      uint16_t port = transport.GetPort ();
      if (ipv4 == Ipv4Address::GetAny () && port == 0)
        {
          m_endPoint = m_mpRDMA->Allocate ();
        }
      else if (ipv4 == Ipv4Address::GetAny () && port != 0)
        {
          m_endPoint = m_mpRDMA->Allocate (port);
        }
      else if (ipv4 != Ipv4Address::GetAny () && port == 0)
        {
          m_endPoint = m_mpRDMA->Allocate (ipv4);
        }
      else if (ipv4 != Ipv4Address::GetAny () && port != 0)
        {
          m_endPoint = m_mpRDMA->Allocate (ipv4, port);
        }
      if (0 == m_endPoint)
        {
          m_errno = port ? ERROR_ADDRINUSE : ERROR_ADDRNOTAVAIL;
          return -1;
        }
    }
  else if (Inet6SocketAddress::IsMatchingType (address))
    {
      Inet6SocketAddress transport = Inet6SocketAddress::ConvertFrom (address);
      Ipv6Address ipv6 = transport.GetIpv6 ();
      uint16_t port = transport.GetPort ();
      if (ipv6 == Ipv6Address::GetAny () && port == 0)
        {
          m_endPoint6 = m_mpRDMA->Allocate6 ();
        }
      else if (ipv6 == Ipv6Address::GetAny () && port != 0)
        {
          m_endPoint6 = m_mpRDMA->Allocate6 (port);
        }
      else if (ipv6 != Ipv6Address::GetAny () && port == 0)
        {
          m_endPoint6 = m_mpRDMA->Allocate6 (ipv6);
        }
      else if (ipv6 != Ipv6Address::GetAny () && port != 0)
        {
          m_endPoint6 = m_mpRDMA->Allocate6 (ipv6, port);
        }
      if (0 == m_endPoint6)
        {
          m_errno = port ? ERROR_ADDRINUSE : ERROR_ADDRNOTAVAIL;
          return -1;
        }
    }
  else
    {
      m_errno = ERROR_INVAL;
      return -1;
    }

  m_mpRDMA->AddSocket (this);

  NS_LOG_LOGIC ("MpRDMASocketImpl " << this << " got an endpoint: " << m_endPoint);

  return SetupCallback ();
}

void
MpRDMASocketImpl::SetInitialSSThresh (uint32_t threshold)
{
  NS_ABORT_MSG_UNLESS ( (m_state == CLOSED) || threshold == m_tcb->m_initialSsThresh,
                        "MpRDMASocketImpl::SetSSThresh() cannot change initial ssThresh after connection started.");

  m_tcb->m_initialSsThresh = threshold;
}

uint32_t
MpRDMASocketImpl::GetInitialSSThresh (void) const
{
  return m_tcb->m_initialSsThresh;
}

void
MpRDMASocketImpl::SetInitialCwnd (uint32_t cwnd)
{
  NS_ABORT_MSG_UNLESS ( (m_state == CLOSED) || cwnd == m_tcb->m_initialCWnd,
                        "MpRDMASocketImpl::SetInitialCwnd() cannot change initial cwnd after connection started.");

  m_tcb->m_initialCWnd = cwnd;
}

uint32_t
MpRDMASocketImpl::GetInitialCwnd (void) const
{
  return m_tcb->m_initialCWnd;
}

/* Inherit from Socket class: Initiate connection to a remote address:port */
int
MpRDMASocketImpl::Connect (const Address & address)
{
  NS_LOG_FUNCTION (this << address);

  // If haven't do so, Bind() this socket first
  if (InetSocketAddress::IsMatchingType (address) && m_endPoint6 == 0)
    {
      if (m_endPoint == 0)
        {
          if (Bind () == -1)
            {
              NS_ASSERT (m_endPoint == 0);
              return -1; // Bind() failed
            }
          NS_ASSERT (m_endPoint != 0);
        }
      InetSocketAddress transport = InetSocketAddress::ConvertFrom (address);
      m_endPoint->SetPeer (transport.GetIpv4 (), transport.GetPort ());
      m_endPoint6 = 0;

      // Get the appropriate local address and port number from the routing protocol and set up endpoint
      if (SetupEndpoint () != 0)
        {
          NS_LOG_ERROR ("Route to destination does not exist ?!");
          return -1;
        }
    }
  else if (Inet6SocketAddress::IsMatchingType (address)  && m_endPoint == 0)
    {
      // If we are operating on a v4-mapped address, translate the address to
      // a v4 address and re-call this function
      Inet6SocketAddress transport = Inet6SocketAddress::ConvertFrom (address);
      Ipv6Address v6Addr = transport.GetIpv6 ();
      if (v6Addr.IsIpv4MappedAddress () == true)
        {
          Ipv4Address v4Addr = v6Addr.GetIpv4MappedAddress ();
          return Connect (InetSocketAddress (v4Addr, transport.GetPort ()));
        }

      if (m_endPoint6 == 0)
        {
          if (Bind6 () == -1)
            {
              NS_ASSERT (m_endPoint6 == 0);
              return -1; // Bind() failed
            }
          NS_ASSERT (m_endPoint6 != 0);
        }
      m_endPoint6->SetPeer (v6Addr, transport.GetPort ());
      m_endPoint = 0;

      // Get the appropriate local address and port number from the routing protocol and set up endpoint
      if (SetupEndpoint6 () != 0)
        { // Route to destination does not exist
          return -1;
        }
    }
  else
    {
      m_errno = ERROR_INVAL;
      return -1;
    }

  // Re-initialize parameters in case this socket is being reused after CLOSE
  m_rtt->Reset ();
  m_synCount = m_synRetries;
  m_dataRetrCount = m_dataRetries;

  // DoConnect() will do state-checking and send a SYN packet
  return DoConnect ();
}

/* Inherit from Socket class: Listen on the endpoint for an incoming connection */
int
MpRDMASocketImpl::Listen (void)
{
  NS_LOG_FUNCTION (this);

  // Linux quits EINVAL if we're not in CLOSED state, so match what they do
  if (m_state != CLOSED)
    {
      m_errno = ERROR_INVAL;
      return -1;
    }
  // In other cases, set the state to LISTEN and done
  NS_LOG_DEBUG ("CLOSED -> LISTEN");
  m_state = LISTEN;
  return 0;
}

/* Inherit from Socket class: Kill this socket and signal the peer (if any) */
int
MpRDMASocketImpl::Close (void)
{
  NS_LOG_FUNCTION (this);
  /// \internal
  /// First we check to see if there is any unread rx data.
  /// \bugid{426} claims we should send reset in this case.
  if (m_rxBuffer->Size () != 0)
    {
      NS_LOG_WARN ("Socket " << this << " << unread rx data during close.  Sending reset." <<
                   "This is probably due to a bad sink application; check its code");
      SendRST ();
      return 0;
    }

  if (m_txBuffer->SizeFromSequence (m_nextTxSequence) > 0)
    { // App close with pending data must wait until all data transmitted
      if (m_closeOnEmpty == false)
        {
          m_closeOnEmpty = true;
          NS_LOG_INFO ("Socket " << this << " deferring close, state " << MpRDMAStateName[m_state]);
        }
      return 0;
    }
  return DoClose ();
}

/* Inherit from Socket class: Signal a termination of send */
int
MpRDMASocketImpl::ShutdownSend (void)
{
  NS_LOG_FUNCTION (this);

  //this prevents data from being added to the buffer
  m_shutdownSend = true;
  m_closeOnEmpty = true;
  //if buffer is already empty, send a fin now
  //otherwise fin will go when buffer empties.
  if (m_txBuffer->Size () == 0)
    {
      if (m_state == ESTABLISHED || m_state == CLOSE_WAIT)
        {
          NS_LOG_INFO ("Emtpy tx buffer, send fin");
          SendEmptyPacket (MpRDMAHeader::FIN);

          if (m_state == ESTABLISHED)
            { // On active close: I am the first one to send FIN
              NS_LOG_DEBUG ("ESTABLISHED -> FIN_WAIT_1");
              m_state = FIN_WAIT_1;
            }
          else
            { // On passive close: Peer sent me FIN already
              NS_LOG_DEBUG ("CLOSE_WAIT -> LAST_ACK");
              m_state = LAST_ACK;
            }
        }
    }

  return 0;
}

/* Inherit from Socket class: Signal a termination of receive */
int
MpRDMASocketImpl::ShutdownRecv (void)
{
  NS_LOG_FUNCTION (this);
  m_shutdownRecv = true;
  return 0;
}

/* Inherit from Socket class: Send a packet. Parameter flags is not used.
    Packet has no MP-RDMA header. Invoked by upper-layer application */
int
MpRDMASocketImpl::Send (Ptr<Packet> p, uint32_t flags)
{
  NS_LOG_FUNCTION (this << p);
  NS_ABORT_MSG_IF (flags, "use of flags is not supported in MpRDMASocketImpl::Send()");
  if (m_state == ESTABLISHED || m_state == SYN_SENT || m_state == CLOSE_WAIT)
    {
      // Store the packet into Tx buffer
      if (!m_txBuffer->Add (p))
        { // TxBuffer overflow, send failed
          m_errno = ERROR_MSGSIZE;
          return -1;
        }
      if (m_shutdownSend)
        {
          m_errno = ERROR_SHUTDOWN;
          return -1;
        }
      // Submit the data to lower layers
      NS_LOG_LOGIC ("txBufSize=" << m_txBuffer->Size () << " state " << MpRDMAStateName[m_state]);
      if (m_state == ESTABLISHED || m_state == CLOSE_WAIT)
        { // Try to send the data out
          //if (!m_sendPendingDataEvent.IsRunning ())
          //  {
          //    m_sendPendingDataEvent = Simulator::Schedule (TimeStep (1),
          //                                                  &MpRDMASocketImpl::SendPendingData,
          //                                                  this, m_connected);
          //  }
            //SendPendingData(m_connected); 
            //printf("at node %u, Send() call MpRDMASend()\n", GetNode()->GetId());
            MpRDMASend(); 
        }
      return p->GetSize ();
    }
  else
    { // Connection not established yet
      m_errno = ERROR_NOTCONN;
      return -1; // Send failure
    }
}

/* Inherit from Socket class: In MpRDMASocketImpl, it is same as Send() call */
int
MpRDMASocketImpl::SendTo (Ptr<Packet> p, uint32_t flags, const Address &address)
{
  return Send (p, flags); // SendTo() and Send() are the same
}

/* Inherit from Socket class: Return data to upper-layer application. Parameter flags
   is not used. Data is returned as a packet of size no larger than maxSize */
Ptr<Packet>
MpRDMASocketImpl::Recv (uint32_t maxSize, uint32_t flags)
{
  NS_LOG_FUNCTION (this);
  NS_ABORT_MSG_IF (flags, "use of flags is not supported in MpRDMASocketImpl::Recv()");
  if (m_rxBuffer->Size () == 0 && m_state == CLOSE_WAIT)
    {
      return Create<Packet> (); // Send EOF on connection close
    }
  Ptr<Packet> outPacket = m_rxBuffer->Extract (maxSize);
  if (outPacket != 0 && outPacket->GetSize () != 0)
    {
      SocketAddressTag tag;
      if (m_endPoint != 0)
        {
          tag.SetAddress (InetSocketAddress (m_endPoint->GetPeerAddress (), m_endPoint->GetPeerPort ()));
        }
      else if (m_endPoint6 != 0)
        {
          tag.SetAddress (Inet6SocketAddress (m_endPoint6->GetPeerAddress (), m_endPoint6->GetPeerPort ()));
        }
      outPacket->AddPacketTag (tag);
    }
  return outPacket;
}

/* Inherit from Socket class: Recv and return the remote's address */
Ptr<Packet>
MpRDMASocketImpl::RecvFrom (uint32_t maxSize, uint32_t flags, Address &fromAddress)
{
  NS_LOG_FUNCTION (this << maxSize << flags);
  Ptr<Packet> packet = Recv (maxSize, flags);
  // Null packet means no data to read, and an empty packet indicates EOF
  if (packet != 0 && packet->GetSize () != 0)
    {
      if (m_endPoint != 0)
        {
          fromAddress = InetSocketAddress (m_endPoint->GetPeerAddress (), m_endPoint->GetPeerPort ());
        }
      else if (m_endPoint6 != 0)
        {
          fromAddress = Inet6SocketAddress (m_endPoint6->GetPeerAddress (), m_endPoint6->GetPeerPort ());
        }
      else
        {
          fromAddress = InetSocketAddress (Ipv4Address::GetZero (), 0);
        }
    }
  return packet;
}

/* Inherit from Socket class: Get the max number of bytes an app can send */
uint32_t
MpRDMASocketImpl::GetTxAvailable (void) const
{
  NS_LOG_FUNCTION (this);
  return m_txBuffer->Available ();
}

/* Inherit from Socket class: Get the max number of bytes an app can read */
uint32_t
MpRDMASocketImpl::GetRxAvailable (void) const
{
  NS_LOG_FUNCTION (this);
  return m_rxBuffer->Available ();
}

/* Inherit from Socket class: Return local address:port */
int
MpRDMASocketImpl::GetSockName (Address &address) const
{
  NS_LOG_FUNCTION (this);
  if (m_endPoint != 0)
    {
      address = InetSocketAddress (m_endPoint->GetLocalAddress (), m_endPoint->GetLocalPort ());
    }
  else if (m_endPoint6 != 0)
    {
      address = Inet6SocketAddress (m_endPoint6->GetLocalAddress (), m_endPoint6->GetLocalPort ());
    }
  else
    { // It is possible to call this method on a socket without a name
      // in which case, behavior is unspecified
      // Should this return an InetSocketAddress or an Inet6SocketAddress?
      address = InetSocketAddress (Ipv4Address::GetZero (), 0);
    }
  return 0;
}

int
MpRDMASocketImpl::GetPeerName (Address &address) const
{
  NS_LOG_FUNCTION (this << address);

  if (!m_endPoint && !m_endPoint6)
    {
      m_errno = ERROR_NOTCONN;
      return -1;
    }

  if (m_endPoint)
    {
      address = InetSocketAddress (m_endPoint->GetPeerAddress (),
                                   m_endPoint->GetPeerPort ());
    }
  else if (m_endPoint6)
    {
      address = Inet6SocketAddress (m_endPoint6->GetPeerAddress (),
                                    m_endPoint6->GetPeerPort ());
    }
  else
    {
      NS_ASSERT (false);
    }

  return 0;
}

/* Inherit from Socket class: Bind this socket to the specified NetDevice */
void
MpRDMASocketImpl::BindToNetDevice (Ptr<NetDevice> netdevice)
{
  NS_LOG_FUNCTION (netdevice);
  Socket::BindToNetDevice (netdevice); // Includes sanity check
  if (m_endPoint == 0)
    {
      if (Bind () == -1)
        {
          NS_ASSERT (m_endPoint == 0);
          return;
        }
      NS_ASSERT (m_endPoint != 0);
    }
  m_endPoint->BindToNetDevice (netdevice);

  if (m_endPoint6 == 0)
    {
      if (Bind6 () == -1)
        {
          NS_ASSERT (m_endPoint6 == 0);
          return;
        }
      NS_ASSERT (m_endPoint6 != 0);
    }
  m_endPoint6->BindToNetDevice (netdevice);

  return;
}

/* Clean up after Bind. Set up callback functions in the end-point. */
int
MpRDMASocketImpl::SetupCallback (void)
{
  NS_LOG_FUNCTION (this);

  if (m_endPoint == 0 && m_endPoint6 == 0)
    {
      return -1;
    }
  if (m_endPoint != 0)
    {
      m_endPoint->SetRxCallback (MakeCallback (&MpRDMASocketImpl::ForwardUp, Ptr<MpRDMASocketImpl> (this)));
      m_endPoint->SetIcmpCallback (MakeCallback (&MpRDMASocketImpl::ForwardIcmp, Ptr<MpRDMASocketImpl> (this)));
      m_endPoint->SetDestroyCallback (MakeCallback (&MpRDMASocketImpl::Destroy, Ptr<MpRDMASocketImpl> (this)));
    }
  if (m_endPoint6 != 0)
    {
      m_endPoint6->SetRxCallback (MakeCallback (&MpRDMASocketImpl::ForwardUp6, Ptr<MpRDMASocketImpl> (this)));
      m_endPoint6->SetIcmpCallback (MakeCallback (&MpRDMASocketImpl::ForwardIcmp6, Ptr<MpRDMASocketImpl> (this)));
      m_endPoint6->SetDestroyCallback (MakeCallback (&MpRDMASocketImpl::Destroy6, Ptr<MpRDMASocketImpl> (this)));
    }

  return 0;
}

/* Perform the real connection tasks: Send SYN if allowed, RST if invalid */
int
MpRDMASocketImpl::DoConnect (void)
{
  NS_LOG_FUNCTION (this);

  // A new connection is allowed only if this socket does not have a connection
  if (m_state == CLOSED || m_state == LISTEN || m_state == SYN_SENT || m_state == LAST_ACK || m_state == CLOSE_WAIT)
    { // send a SYN packet and change state into SYN_SENT
      SendEmptyPacket (MpRDMAHeader::SYN);
      NS_LOG_DEBUG (MpRDMAStateName[m_state] << " -> SYN_SENT");
      m_state = SYN_SENT;
    }
  else if (m_state != TIME_WAIT)
    { // In states SYN_RCVD, ESTABLISHED, FIN_WAIT_1, FIN_WAIT_2, and CLOSING, an connection
      // exists. We send RST, tear down everything, and close this socket.
      SendRST ();
      CloseAndNotify ();
    }
  return 0;
}

/* Do the action to close the socket. Usually send a packet with appropriate
    flags depended on the current m_state. */
int
MpRDMASocketImpl::DoClose (void)
{
  NS_LOG_FUNCTION (this);
  switch (m_state)
    {
    case SYN_RCVD:
    case ESTABLISHED:
      // send FIN to close the peer
      SendEmptyPacket (MpRDMAHeader::FIN); 
      NS_LOG_DEBUG ("ESTABLISHED -> FIN_WAIT_1");
      m_state = FIN_WAIT_1;
      break;
    case CLOSE_WAIT:
      // send FIN+ACK to close the peer
      SendEmptyPacket (MpRDMAHeader::FIN | MpRDMAHeader::ACK);
      NS_LOG_DEBUG ("CLOSE_WAIT -> LAST_ACK");
      m_state = LAST_ACK;
      break;
    case SYN_SENT:
    case CLOSING:
      // Send RST if application closes in SYN_SENT and CLOSING
      SendRST ();
      CloseAndNotify ();
      break;
    case LISTEN:
    case LAST_ACK:
      // In these three states, move to CLOSED and tear down the end point
      CloseAndNotify ();
      break;
    case CLOSED:
    case FIN_WAIT_1:
    case FIN_WAIT_2:
    case TIME_WAIT:
    default: /* mute compiler */
      // Do nothing in these four states
      break;
    }
  return 0;
}

/* Peacefully close the socket by notifying the upper layer and deallocate end point */
void
MpRDMASocketImpl::CloseAndNotify (void)
{
  NS_LOG_FUNCTION (this);

  if (!m_closeNotified)
    {
      NotifyNormalClose ();
      m_closeNotified = true;
    }

  NS_LOG_DEBUG (MpRDMAStateName[m_state] << " -> CLOSED");
  m_state = CLOSED;
  DeallocateEndPoint ();
}


/* Tell if a sequence number range is out side the range that my rx buffer can
    accpet */
bool
MpRDMASocketImpl::OutOfRange (SequenceNumber32 head, SequenceNumber32 tail) const
{
  if (m_state == LISTEN || m_state == SYN_SENT || m_state == SYN_RCVD)
    { // Rx buffer in these states are not initialized.
      return false;
    }
  if (m_state == LAST_ACK || m_state == CLOSING || m_state == CLOSE_WAIT)
    { // In LAST_ACK and CLOSING states, it only wait for an ACK and the
      // sequence number must equals to m_rxBuffer->NextRxSequence ()
      return (m_rxBuffer->NextRxSequence () != head);
    }

  // In all other cases, check if the sequence number is in range
  return (tail < m_rxBuffer->NextRxSequence () || m_rxBuffer->MaxRxSequence () <= head);
}

/* Function called by the L3 protocol when it received a packet to pass on to
    the MP-RDMA. This function is registered as the "RxCallback" function in
    SetupCallback(), which invoked by Bind(), and CompleteFork() */
void
MpRDMASocketImpl::ForwardUp (Ptr<Packet> packet, Ipv4Header header, uint16_t port,
                          Ptr<Ipv4Interface> incomingInterface)
{
  NS_LOG_LOGIC ("Socket " << this << " forward up " <<
                m_endPoint->GetPeerAddress () <<
                ":" << m_endPoint->GetPeerPort () <<
                " to " << m_endPoint->GetLocalAddress () <<
                ":" << m_endPoint->GetLocalPort ());

  Address fromAddress = InetSocketAddress (header.GetSource (), port);
  Address toAddress = InetSocketAddress (header.GetDestination (),
                                         m_endPoint->GetLocalPort ());

  DoForwardUp (packet, fromAddress, toAddress);
}

void
MpRDMASocketImpl::ForwardUp6 (Ptr<Packet> packet, Ipv6Header header, uint16_t port,
                           Ptr<Ipv6Interface> incomingInterface)
{
  NS_LOG_LOGIC ("Socket " << this << " forward up " <<
                m_endPoint6->GetPeerAddress () <<
                ":" << m_endPoint6->GetPeerPort () <<
                " to " << m_endPoint6->GetLocalAddress () <<
                ":" << m_endPoint6->GetLocalPort ());

  Address fromAddress = Inet6SocketAddress (header.GetSourceAddress (), port);
  Address toAddress = Inet6SocketAddress (header.GetDestinationAddress (),
                                          m_endPoint6->GetLocalPort ());

  DoForwardUp (packet, fromAddress, toAddress);
}

void
MpRDMASocketImpl::ForwardIcmp (Ipv4Address icmpSource, uint8_t icmpTtl,
                            uint8_t icmpType, uint8_t icmpCode,
                            uint32_t icmpInfo)
{
  NS_LOG_FUNCTION (this << icmpSource << (uint32_t)icmpTtl << (uint32_t)icmpType <<
                   (uint32_t)icmpCode << icmpInfo);
  if (!m_icmpCallback.IsNull ())
    {
      m_icmpCallback (icmpSource, icmpTtl, icmpType, icmpCode, icmpInfo);
    }
}

void
MpRDMASocketImpl::ForwardIcmp6 (Ipv6Address icmpSource, uint8_t icmpTtl,
                             uint8_t icmpType, uint8_t icmpCode,
                             uint32_t icmpInfo)
{
  NS_LOG_FUNCTION (this << icmpSource << (uint32_t)icmpTtl << (uint32_t)icmpType <<
                   (uint32_t)icmpCode << icmpInfo);
  if (!m_icmpCallback6.IsNull ())
    {
      m_icmpCallback6 (icmpSource, icmpTtl, icmpType, icmpCode, icmpInfo);
    }
}

void MpRDMASocketImpl::logCwnd()
{
  for(std::map<uint32_t,int>::iterator it=m_path_cwnd_log.begin();it!=m_path_cwnd_log.end();it++)
//wsq
    //fprintf(stderr,"%lu %u,%u %d\n",Simulator::Now().GetMicroSeconds()/100,GetNode()->GetId(),it->first,it->second);
  //fprintf(stderr,"%lu %u,total %u\n",Simulator::Now().GetMicroSeconds()/100,GetNode()->GetId(),m_tcb->m_cWnd.Get()/m_tcb->m_segmentSize);
    
  Simulator::Schedule(Time::FromDouble(0.0001, Time::S), &MpRDMASocketImpl::logCwnd, this); 
}

void
MpRDMASocketImpl::DoForwardUp (Ptr<Packet> packet, const Address &fromAddress,
                            const Address &toAddress)
{
  if(GetNode()->GetId()<=10)
  {
    // MpRDMAHeader l4H;
    // packet->PeekHeader (l4H);
    // fprintf(stderr, "node %u MpRDMAReceive<---: local port %u, peer port %u\n", GetNode()->GetId(), l4H.GetSourcePort(), l4H.GetDestinationPort());

    if (mLogCwnd && (Simulator::Now().GetMicroSeconds()/100)>0)
    {
      logCwnd();
      mLogCwnd=0;
    }

    AAckTag aackTag; 
    if(packet->PeekPacketTag(aackTag))
    {
      if ( m_path_cwnd_log.find(aackTag.pathId) == m_path_cwnd_log.end() )
        fprintf(stderr,"ERROR!\n");
      else
        m_path_cwnd_log[aackTag.pathId]--;
    }
  }


  if (Simulator::Now().GetMicroSeconds() % 500 == 0)
  {
//wsq
     // printf("at node %u, now time is %lu, sendL %u, receiveL %u, messageSize %u, deltaT %lf, local port %u, peer port %u\n", 
    //          GetNode()->GetId(), Simulator::Now().GetMicroSeconds(), 
      //        m_sndL, m_rcvL, m_messageSize, m_deltaT, m_endPoint->GetLocalPort (), m_endPoint->GetPeerPort ()); 
  } 

  // Peel off MP-RDMA header and do validity checking
  MpRDMAHeader mpRDMAHeader;
  uint32_t bytesRemoved = packet->RemoveHeader (mpRDMAHeader);
  SequenceNumber32 seq = mpRDMAHeader.GetSequenceNumber ();
  if (bytesRemoved == 0 || bytesRemoved > 60)
    {
      NS_LOG_ERROR ("Bytes removed: " << bytesRemoved << " invalid");
      return; // Discard invalid packet
    }
  /* Yuanwei: out of range packet will also be sent with an ACK */ 
  else if (packet->GetSize () > 0 && OutOfRange (seq, seq + packet->GetSize ()))
    {
        //printf("node %u, packet is out of range, drop, seq %u, buffer %u\n", GetNode()->GetId(), seq.GetValue(), m_rxBuffer->Size()); 

      // Discard fully out of range data packets
      NS_LOG_LOGIC ("At state " << MpRDMAStateName[m_state] <<
                    " received packet of seq [" << seq <<
                    ":" << seq + packet->GetSize () <<
                    ") out of range [" << m_rxBuffer->NextRxSequence () << ":" <<
                    m_rxBuffer->MaxRxSequence () << ")");
      // Acknowledgement should be sent for all unacceptable packets (RFC793, p.69)
      if (m_state == ESTABLISHED && !(mpRDMAHeader.GetFlags () & MpRDMAHeader::RST))
        {
            if (seq + packet->GetSize() < m_rxBuffer->NextRxSequence ())  //a false reTx packet 
            {
                SendDataAckPacket(packet, mpRDMAHeader); 
            }
            //SendEmptyPacket (MpRDMAHeader::ACK);
        }
      return;
    }

  m_rxTrace (packet, mpRDMAHeader, this);

  //Yuanwei_Note: receiving SYN, follow TCP's operations 
  if (mpRDMAHeader.GetFlags () & MpRDMAHeader::SYN)
    {
      /* The window field in a segment where the SYN bit is set (i.e., a <SYN>
       * or <SYN,ACK>) MUST NOT be scaled (from RFC 7323 page 9). But should be
       * saved anyway..
       */
      m_rWnd = mpRDMAHeader.GetWindowSize ();

      if (mpRDMAHeader.HasOption (TcpOption::WINSCALE) && m_winScalingEnabled)
        {
          ProcessOptionWScale (mpRDMAHeader.GetOption (TcpOption::WINSCALE));
        }
      else
        {
          m_winScalingEnabled = false;
        }

      // When receiving a <SYN> or <SYN-ACK> we should adapt TS to the other end
      if (mpRDMAHeader.HasOption (TcpOption::TS) && m_timestampEnabled)
        {
          ProcessOptionTimestamp (mpRDMAHeader.GetOption (TcpOption::TS),
                                  mpRDMAHeader.GetSequenceNumber ());
        }
      else
        {
          m_timestampEnabled = false;
        }

      // Initialize cWnd and ssThresh
      m_tcb->m_cWnd = GetInitialCwnd () * GetSegSize ();
      m_tcb->m_ssThresh = GetInitialSSThresh ();

      if (mpRDMAHeader.GetFlags () & MpRDMAHeader::ACK)
        {
          EstimateRtt (mpRDMAHeader);
          m_highRxAckMark = mpRDMAHeader.GetAckNumber ();
        }
    }

  /* Yuanwei_Note: receiving ACK, should do the following 
   * 1. ECN operation 
   * 2. cWnd, rWnd operation 
   * 3. record ACK pathId 
   * 4. update received_data bitmap 
   */ 
  else if (mpRDMAHeader.GetFlags () & MpRDMAHeader::ACK)
    {
      NS_ASSERT (!(mpRDMAHeader.GetFlags () & MpRDMAHeader::SYN));
      if (m_timestampEnabled)
        {
          if (!mpRDMAHeader.HasOption (TcpOption::TS))
            {
              // Ignoring segment without TS, RFC 7323
              NS_LOG_LOGIC ("At state " << MpRDMAStateName[m_state] <<
                            " received packet of seq [" << seq <<
                            ":" << seq + packet->GetSize () <<
                            ") without TS option. Silently discard it");
              return;
            }
          else
            {
              ProcessOptionTimestamp (mpRDMAHeader.GetOption (TcpOption::TS),
                                      mpRDMAHeader.GetSequenceNumber ());
            }
        }

      EstimateRtt (mpRDMAHeader);
      /* Yuanwei: update rWnd only when rWnd timestamp is larger than last one */ 
      TimeStampTag tsTag; 
      packet->PeekPacketTag(tsTag); 
      if(tsTag.rWndTS > m_rWndTimeStamp) 
      {
          UpdateWindowSize (mpRDMAHeader);
          m_rWndTimeStamp = tsTag.rWndTS; 
      } 
    }

  /* Yuanwei: when availableWindow == 0, do opportunistic retransmission */ 
  //if (m_rWnd.Get () == 0 && m_persistEvent.IsExpired ())
  //  { // Zero window: Enter persist state to send 1 byte to probe
  //    NS_LOG_LOGIC (this << " Enter zerowindow persist state");
  //    NS_LOG_LOGIC (this << " Cancelled ReTxTimeout event which was set to expire at " <<
  //                  (Simulator::Now () + Simulator::GetDelayLeft (m_retxEvent)).GetSeconds ());
  //    m_retxEvent.Cancel ();
  //    NS_LOG_LOGIC ("Schedule persist timeout at time " <<
  //                  Simulator::Now ().GetSeconds () << " to expire at time " <<
  //                  (Simulator::Now () + m_persistTimeout).GetSeconds ());
  //    m_persistEvent = Simulator::Schedule (m_persistTimeout, &MpRDMASocketImpl::PersistTimeout, this);
  //    NS_ASSERT (m_persistTimeout == Simulator::GetDelayLeft (m_persistEvent));
  //  }
  //if(m_rWnd.Get() == 0)
  //{
  //    /* enter opportunistic retransmission */ 
  //    printf("zero rWnd, enter opportunistic reTx, available window is %u\n", AvailableWindow ()); 
  //    OpportunisticReTx(); 
  //}

  // MP-RDMA state machine code in different process functions
  // C.f.: tcp_rcv_state_process() in tcp_input.c in Linux kernel
  switch (m_state)
    {
    case ESTABLISHED:
      ProcessEstablished (packet, mpRDMAHeader);
      break;
    case LISTEN:
      ProcessListen (packet, mpRDMAHeader, fromAddress, toAddress);
      break;
    case TIME_WAIT:
      // Do nothing
      break;
    case CLOSED:
      // Send RST if the incoming packet is not a RST
      if ((mpRDMAHeader.GetFlags () & ~(MpRDMAHeader::PSH | MpRDMAHeader::URG)) != MpRDMAHeader::RST)
        { // Since m_endPoint is not configured yet, we cannot use SendRST here
          MpRDMAHeader h;
          Ptr<Packet> p = Create<Packet> ();
          h.SetFlags (MpRDMAHeader::RST);
          h.SetSequenceNumber (m_nextTxSequence);
          h.SetAckNumber (m_rxBuffer->NextRxSequence ());
          h.SetSourcePort (mpRDMAHeader.GetDestinationPort ());
          h.SetDestinationPort (mpRDMAHeader.GetSourcePort ());
          h.SetWindowSize (AdvertisedWindowSize ());
          AddOptions (h);
          m_txTrace (p, h, this);
          m_mpRDMA->SendPacket (p, h, toAddress, fromAddress, m_boundnetdevice);
        }
      break;
    case SYN_SENT:
      ProcessSynSent (packet, mpRDMAHeader);
      break;
    case SYN_RCVD:
      ProcessSynRcvd (packet, mpRDMAHeader, fromAddress, toAddress);
      break;
    case FIN_WAIT_1:
    case FIN_WAIT_2:
    case CLOSE_WAIT:
      ProcessWait (packet, mpRDMAHeader);
      break;
    case CLOSING:
      ProcessClosing (packet, mpRDMAHeader);
      break;
    case LAST_ACK:
      ProcessLastAck (packet, mpRDMAHeader);
      break;
    default: // mute compiler
      break;
    }

  if (m_rWnd.Get () != 0 && m_persistEvent.IsRunning ())
    { // persist probes end, the other end has increased the window
      NS_ASSERT (m_connected);
      NS_LOG_LOGIC (this << " Leaving zerowindow persist state");
      m_persistEvent.Cancel ();

      // Try to send more data, since window has been updated
      //if (!m_sendPendingDataEvent.IsRunning ())
      //  {
      //    m_sendPendingDataEvent = Simulator::Schedule (TimeStep (1),
      //                                                  &MpRDMASocketImpl::SendPendingData,
      //                                                  this, m_connected);
      //  }
      //printf("at node %u, DoForwardUp() leave zerowindow call MpRDMASend()\n", GetNode()->GetId());
      MpRDMASend(); 
    }
}

/* Received a packet upon ESTABLISHED state. This function is mimicking the
    role of tcp_rcv_established() in tcp_input.c in Linux kernel. */
void
MpRDMASocketImpl::ProcessEstablished (Ptr<Packet> packet, const MpRDMAHeader& mpRDMAHeader)
{
  NS_LOG_FUNCTION (this << mpRDMAHeader);

  // Extract the flags. PSH and URG are not honoured.
  uint8_t tcpflags = mpRDMAHeader.GetFlags () & ~(MpRDMAHeader::PSH | MpRDMAHeader::URG);

  // Different flags are different events
  if (tcpflags == MpRDMAHeader::ACK)
    {
      /* Yuanwei: duplicate ACK will be processed */ 
      if (mpRDMAHeader.GetAckNumber () < m_txBuffer->HeadSequence ())
        {
          // Case 1:  If the ACK is a duplicate (SEG.ACK < SND.UNA), it can be ignored.
          // Pag. 72 RFC 793
          NS_LOG_LOGIC ("Ignored ack of " << mpRDMAHeader.GetAckNumber () <<
                        " SND.UNA = " << m_txBuffer->HeadSequence ());

          // TODO: RFC 5961 5.2 [Blind Data Injection Attack].[Mitigation]
          
          ReceivedAck (packet, mpRDMAHeader); 
           
        }
      else if (mpRDMAHeader.GetAckNumber () > m_highTxMark)
        {
          // If the ACK acks something not yet sent (SEG.ACK > HighTxMark) then
          // send an ACK, drop the segment, and return.
          // Pag. 72 RFC 793
          NS_LOG_LOGIC ("Ignored ack of " << mpRDMAHeader.GetAckNumber () <<
                        " HighTxMark = " << m_highTxMark);

          SendEmptyPacket (MpRDMAHeader::ACK);
        }
      else
        {
          // SND.UNA < SEG.ACK =< HighTxMark
          // Pag. 72 RFC 793
          ReceivedAck (packet, mpRDMAHeader); 
        }
    }
  else if (tcpflags == MpRDMAHeader::SYN)
    { // Received SYN, old NS-3 behaviour is to set state to SYN_RCVD and
      // respond with a SYN+ACK. But it is not a legal state transition as of
      // RFC793. Thus this is ignored.
    }
  else if (tcpflags == (MpRDMAHeader::SYN | MpRDMAHeader::ACK))
    { // No action for received SYN+ACK, it is probably a duplicated packet
    }
  else if (tcpflags == MpRDMAHeader::FIN || tcpflags == (MpRDMAHeader::FIN | MpRDMAHeader::ACK))
    { // Received FIN or FIN+ACK, bring down this socket nicely
      PeerClose (packet, mpRDMAHeader);
    }
  else if (tcpflags == 0)
    { // No flags means there is only data
      ReceivedData (packet, mpRDMAHeader);
      if (m_rxBuffer->Finished ())
        {
          PeerClose (packet, mpRDMAHeader);
        }
    }
  else
    { // Received RST or the TCP flags is invalid, in either case, terminate this socket
      if (tcpflags != MpRDMAHeader::RST)
        { // this must be an invalid flag, send reset
          NS_LOG_LOGIC ("Illegal flag " << MpRDMAHeader::FlagsToString (tcpflags) << " received. Reset packet is sent.");
          SendRST ();
        }
      CloseAndNotify ();
    }
}

/* Process the newly received ACK */
/* Yuanwei_Note: when receiving a new ACK, should do the following: 
 * 1. update seq UnACKed block 
 * 2. do window operation, cwnd, rwnd, ECN, adjust tx_buffer pointers 
 * 3. record the returned pathId for next use
 * ACK.sequenceNumber == data.Sequence, not cummulative ACK 
 */ 
void
MpRDMASocketImpl::ReceivedAck (Ptr<Packet> packet, const MpRDMAHeader& mpRDMAHeader) 
{



#if (TEST_DELTA_T == 1)
  FenceTag fenceTag;
  packet->PeekPacketTag(fenceTag);

  if(fenceTag.fence == 1)
  {
      //m_wait = false;
  }
#endif



  //printf("at node %u, received an ACK\n", GetNode()->GetId());

  if(!begin_ACK_record && GetNode()->GetId() == 0) 
  {
      char file_name[1024];
      sprintf(file_name, "totalPath_%u_node_%u_ECN_ratio_log_probability_0.0_new_algo3_ECN_0.5.txt", m_pathNum, GetNode()->GetId()); 

      LogManager::RegisterLog(20000 + GetNode()->GetId(), file_name);
      //LogECNRatio(); 

      begin_ACK_record = true;
      m_maxPathId = GetNode()->GetId() * 1000;
  } 

  NS_LOG_FUNCTION (this << mpRDMAHeader);

  NS_ASSERT (0 != (mpRDMAHeader.GetFlags () & MpRDMAHeader::ACK));
  NS_ASSERT (m_tcb->m_segmentSize > 0); 

  SequenceNumber32 ackNumber = mpRDMAHeader.GetAckNumber ();

  /* update the accumulative ACK */ 
  AAckTag aackTag; 
  packet->PeekPacketTag(aackTag); 
  /*lyj debug*/
  //printf("this is aack ack in pkt is %u,in local is %u\n",aackTag.aackSeq,m_aackSeq);
  if(aackTag.aackSeq > m_aackSeq) 
  {
      m_aackSeq = aackTag.aackSeq; 
      //lyj debug
      //printf("update aack to %u\n",m_aackSeq);
  }
  //lyj debug
  // printf("ack of %u snd.una=%u snd.nxt=%u\n",ackNumber.GetValue(),
  //                 m_txBuffer->HeadSequence ().GetValue(),
  //                 m_nextTxSequence.Get().GetValue());
  NS_LOG_DEBUG ("ACK of " << ackNumber <<
                " SND.UNA=" << m_txBuffer->HeadSequence () <<
                " SND.NXT=" << m_nextTxSequence);



  /* outdated ACK */
  uint32_t accept_point = SafeSubtraction(m_txBuffer->HeadSequence().GetValue(), 2 * m_sndL * m_tcb->m_segmentSize);
  //if(aackTag.aackSeq < accept_point)
  if(ackNumber.GetValue() < accept_point)
  {
      printf("at node %u, accept_point is %u don't accept an ACK:%u\n", GetNode()->GetId(),accept_point,ackNumber.GetValue());
      return;
  }

  //printf("at node %u, ACK aack %u, sack %u, head %u, reTx %u, next %u, recoveryPoint %u, isNack %u\n", 
  //        GetNode()->GetId(), aackTag.aackSeq, ackNumber.GetValue(),
  //        m_txBuffer->HeadSequence().GetValue(), m_highReTxMark.GetValue(), m_nextTxSequence.Get().GetValue(), m_recoveryPoint.GetValue(), aackTag.nack);




  if(PRINT_DEBUG_INFO)
  {
printf("wsqat node %u, enter CA, cWnd is %u\n", GetNode()->GetId(), m_tcb->m_cWnd.Get());//wsq
    printf("at node %u, receiving an ACK of %u, SND.UNA= %u, SND.NXT= %u\n", GetNode()->GetId(), 
            ackNumber.GetValue(), m_txBuffer->HeadSequence().GetValue(), m_nextTxSequence.Get().GetValue()); 

    printf("before adjustment: \n"); 
    for(std::map<SequenceNumber32, AckedBlock>::iterator it = m_seqAckedMap.begin(); it != m_seqAckedMap.end(); it++)
    {
        printf("block %u received %u\n", it->first.GetValue(), it->second.acked); 
    }
  } 
  
  /* measure all path RTT*/ 
  TimeStampTag timeTag; 
  packet->PeekPacketTag(timeTag);
 
  double measuredRTT = Simulator::Now().GetSeconds() - timeTag.timeStamp;
  m_allPathRTT->Measurement (Time(Seconds(measuredRTT)));                // Log the measurement

  LogManager::WriteLog(5800, "%lf\n", measuredRTT); 

  /* calculate thresold for fast recovery*/ 
  m_fastRecoveryThreshold = uint32_t(m_fastAlpha * (m_allPathRTT->GetEstimate().GetSeconds() + 3 * m_allPathRTT->GetVariation().GetSeconds()) / m_allPathRTT->GetEstimate().GetSeconds() * double(m_tcb->m_cWnd.Get()));  

  /* add txBuffer size as the upper bound to fastRecoveryThreshold */ 
  if(m_txBuffer->MaxBufferSize() - 2 * m_tcb->m_segmentSize <= m_fastRecoveryThreshold) 
  {
      m_fastRecoveryThreshold = m_txBuffer->MaxBufferSize() - 2 * m_tcb->m_segmentSize; 
  }

  //calculate RTO for retrnasmission 
  m_mpRdmaRto = Max (m_allPathRTT->GetEstimate () + Max (m_clockGranularity, m_allPathRTT->GetVariation () * 4), m_minRto); 



  PathIdTag pathTag; 
  packet->PeekPacketTag(pathTag);
  pathTag.pid = aackTag.pathId;  /* ACK goes single-path, now path ID is embeded in aacktag */
  

  /* ====================================== update cWnd according to ECN information ==========================================*/

  if(PRINT_DEBUG_INFO)
  {
    printf("after adjustment: \n"); 
    for(std::map<SequenceNumber32, AckedBlock>::iterator it = m_seqAckedMap.begin(); it != m_seqAckedMap.end(); it++)
    {
         printf("block %u received %u\n", it->first.GetValue(), it->second.acked); 
    }
  } 

  // update cWnd/rWnd according to Sender State  
  if(m_senderState == InitState)
  {
      m_senderState = SlowStart; 
  } 

  EcnTag ecnTag; 
  packet->PeekPacketTag(ecnTag);

  /* for logger */ 
  pathRTT[pathTag.pid % m_pathNum] += measuredRTT; 
  RTTCount[pathTag.pid % m_pathNum]++;

  m_maxTxNum = 1; 

  if(m_senderState == SlowStart)
  {
      if(ecnTag.ip_bit_1 && ecnTag.ip_bit_2) //ECN marked ACK 
      {
          printf("at node %u, enter CA, cWnd is %u\n", GetNode()->GetId(), m_tcb->m_cWnd.Get()); 
          
          m_tcb->m_cWnd = SafeSubtraction(m_tcb->m_cWnd, m_tcb->m_segmentSize / 2); 
          if(m_tcb->m_cWnd < MINIMAL * m_tcb->m_segmentSize)
          {
            m_tcb->m_cWnd = MINIMAL * m_tcb->m_segmentSize; 
          }
          m_senderState = CA;
      }
      else  //normal ACK 
      {
          m_tcb->m_cWnd += m_tcb->m_segmentSize;
          m_maxTxNum = 2;
      }
  }
  else if(m_senderState == CA)
  {   
      if(ecnTag.ip_bit_1 && ecnTag.ip_bit_2) //ECN marked ACK 
      {
          m_tcb->m_cWnd = SafeSubtraction(m_tcb->m_cWnd, m_tcb->m_segmentSize / 2); 
          if(m_tcb->m_cWnd < MINIMAL * m_tcb->m_segmentSize)
          {
            m_tcb->m_cWnd = MINIMAL * m_tcb->m_segmentSize; 
          }
      }
      else //normal ACK 
      {
          uint32_t old_cWnd = m_tcb->m_cWnd / m_tcb->m_segmentSize;

          double adder = static_cast<double> (m_tcb->m_segmentSize * m_tcb->m_segmentSize) / m_tcb->m_cWnd.Get (); 
          adder = std::max (1.0, adder);
          m_tcb->m_cWnd += static_cast<uint32_t> (adder);

          if(m_tcb->m_cWnd.Get() / m_tcb->m_segmentSize - old_cWnd >= 1)
          {
              m_probeOpCount ++;
              if(m_maxPathId < GetNode()->GetId() * 1000 + 64 || m_probeOpCount % 10 == 0)
                  m_probe = true;

              //printf("at node %u, should probe\n", GetNode()->GetId());
          }
      }
  }
//wsq
/*
  if(PRINT_DEBUG_INFO) 
  {
    printf("dump the sack map, cWnd %u, pipe %u, highreTx %u, AACK %u\n", 
            m_tcb->m_cWnd.Get(), m_pipe, m_highReTxMark.GetValue(), m_aackSeq); 

    printf("after adjustment 2: \n"); 
    for(std::map<SequenceNumber32, AckedBlock>::iterator it = m_seqAckedMap.begin(); it != m_seqAckedMap.end(); it++)
    {
        printf("block %u received %u\n", it->first.GetValue(), it->second.acked); 
    }
    printf("after receiving, SND.UNA = %u\n", m_txBuffer->HeadSequence().GetValue()); 
  } 
*/
  /* ====================================== END update cWnd according to ECN information ==========================================*/



  /* ======================== ooo control update m_ooP, m_ooL, inflate, cWnd, pipe =============================== */

  if(aackTag.maxSeq > m_sndMax.GetValue())
  {
      m_sndMax = SequenceNumber32(aackTag.maxSeq);
  }

  ReTxTag retxTag;
  packet->PeekPacketTag(retxTag);

  /* On ACK */
  if(aackTag.nack == 0)
  {
      m_bSendPkt = false;

      /* update OOP, OOL */
      if(ackNumber > m_ooP)
      {
          m_ooP = ackNumber;
          m_ooL = SequenceNumber32(SafeSubtraction(m_ooP.GetValue(), m_sndL * m_tcb->m_segmentSize));
      }

      if(aackTag.aackSeq <= m_txBuffer->HeadSequence().GetValue())  //dup ACK
      {
          if(ackNumber >= m_recoveryPoint)
          {
              m_inflate += m_tcb->m_segmentSize;

#if (PENALIZE_BAD_PATH == 1)
              if(ackNumber < m_ooL)
              {
                  m_tcb->m_cWnd = SafeSubtraction(m_tcb->m_cWnd, m_tcb->m_segmentSize);
                  if(m_tcb->m_cWnd < MINIMAL * m_tcb->m_segmentSize)
                  {
                    m_tcb->m_cWnd = MINIMAL * m_tcb->m_segmentSize; 
                  }
              }
#endif
          }
      }
      else if(aackTag.aackSeq > m_txBuffer->HeadSequence().GetValue())  //positive ACK 
      {
          if(ackNumber >= m_recoveryPoint)
          {
              m_inflate += m_tcb->m_segmentSize;
             
#if (PENALIZE_BAD_PATH == 1)
              if(ackNumber < m_ooL)
              {
                  m_tcb->m_cWnd = SafeSubtraction(m_tcb->m_cWnd, m_tcb->m_segmentSize);
                  if(m_tcb->m_cWnd < MINIMAL * m_tcb->m_segmentSize)
                  {
                    m_tcb->m_cWnd = MINIMAL * m_tcb->m_segmentSize; 
                  }
              }
#endif
          }

          if(!m_bReTx)
          {
              m_inflate -= (aackTag.aackSeq - m_txBuffer->HeadSequence().GetValue());

              /* yuanwei patch 5*/
              //int t_pipe = (m_nextTxSequence.Get().GetValue() - m_txBuffer->HeadSequence().GetValue());
              //int wnd = m_inflate + (int)m_tcb->m_cWnd;
              //if(wnd < t_pipe + (int)m_tcb->m_segmentSize)
              //{
              //  m_inflate = m_tcb->m_segmentSize + t_pipe - (int)m_tcb->m_cWnd;
              //}
          }

          /* move SND.UNA */
          SequenceNumber32 new_head = SequenceNumber32(aackTag.aackSeq);
          //lyj add
          if(new_head > m_nextTxSequence){
            m_nextTxSequence = (m_nextTxSequence-m_txBuffer->HeadSequence ())+new_head;
            m_ooP = m_nextTxSequence - m_tcb->m_segmentSize;
            m_ooL = SequenceNumber32(SafeSubtraction(m_ooP.GetValue(), m_sndL * m_tcb->m_segmentSize));
          }
          m_txBuffer->DiscardUpTo(new_head);
//recode sender_retx
#if(SENDER_RETX == 1)
          //if find order ,resend from head
          if(new_head > m_startsendretx){
            m_sendretx = false;
          }
          if(!m_bReTx && (ackNumber > m_detect) && !m_sendretx){
            m_sendretx = true;
            m_High_resend_pos = new_head;
            m_oversendretx = ackNumber;
            m_startsendretx = new_head;
          }
              // if(ackNumber < m_ooL){
              //   if(!m_bReTx)
              //   {
              //       //printf("into resnd!\n");
              //       m_bReTx = true;
              //       m_bRecorySend=true;
              //       if(m_highReTxMark < m_txBuffer->HeadSequence())
              //           m_highReTxMark = m_txBuffer->HeadSequence();

              //       m_ooP = m_nextTxSequence - m_tcb->m_segmentSize;
              //       m_ooL = SequenceNumber32(SafeSubtraction(m_ooP.GetValue(), m_sndL * m_tcb->m_segmentSize));

              //       m_recoveryPoint = m_nextTxSequence;
              //   }
              // }
#endif
          if(m_highReTxMark < new_head)
          {
              m_highReTxMark = new_head;
              //lyj add
              //printf("at ackfun %u\n",m_highReTxMark.GetValue());
          }

          



          /* yuanwei patch 4 */
          //if(m_nextTxSequence < m_txBuffer->HeadSequence())
          //{
          //    m_nextTxSequence = m_txBuffer->HeadSequence();

          //    if(m_recoveryPoint > m_nextTxSequence)
          //    {
          //        m_recoveryPoint = m_nextTxSequence;
          //    }
          //}




          /* end of loss recovery */
          if (m_bReTx && m_txBuffer->HeadSequence() >= m_recoveryPoint)
          {
              m_bReTx = false;
              //m_bSendPkt = true;
              
              /* yuanwei patch 1 */
              //uint32_t t_pipe = (m_nextTxSequence.Get().GetValue() - m_txBuffer->HeadSequence().GetValue());
              //m_inflate = m_tcb->m_segmentSize + (int)t_pipe - (int)m_tcb->m_cWnd;

          
              printf("at node %u, return from loss recovery, available window %d, recoveryPoint %u, SND.NXT %u, SND.OOP %u, SND.UNA %u, inflate %d, cWnd %u\n", 
                      GetNode()->GetId(), 
                      m_inflate + m_tcb->m_cWnd.Get() - (m_nextTxSequence.Get().GetValue() - m_txBuffer->HeadSequence().GetValue()), 
                      m_recoveryPoint.GetValue(), m_nextTxSequence.Get().GetValue(), m_ooP.GetValue(), new_head.GetValue(), m_inflate, m_tcb->m_cWnd.Get());
          }
      }

      /* yuanwei patch 2 */
      if(ackNumber >= m_ooL || retxTag.isReTx)
      //if(ackNumber >= m_ooL)
      {
          m_lastAckPathId = pathTag.pid;
          m_bSendPkt = true;
      }
      else 
      {
          //printf("at node %u, path %u dropped, ooL %u, ack %u, reTx %u, head %u, reTxState %u, available window %d\n",
          //        GetNode()->GetId(), pathTag.pid, m_ooL.GetValue(), ackNumber.GetValue(), retxTag.isReTx, m_txBuffer->HeadSequence().GetValue(), m_bReTx, 
          //        m_inflate + m_tcb->m_cWnd.Get() - (m_nextTxSequence.Get().GetValue() - m_txBuffer->HeadSequence().GetValue()));
      }

      if(m_bReTx)
      {
          if((retxTag.isReTx || ackNumber >= m_ooL))
          {
              if(m_highReTxMark < m_recoveryPoint)
              {
                  MpRDMAreTx(pathTag.pid);
              }
              else
              {
                  //MpRDMASend();

                  
                  
                  /* yuanwei patch 2 */
                  SequenceNumber32 toTxSeq; 
                  toTxSeq = m_nextTxSequence.Get(); 

                  uint32_t s = m_tcb->m_segmentSize;  
                  //lyj debug
                  // if(s == 0){
                  //   printf("%s %d\n",__FUNCTION__,__LINE__);
                  // }
                  uint32_t sz = SendDataPacket(toTxSeq, s, false, pathTag.pid);

                  m_nextTxSequence += sz;



              }
          }
      } else if (m_bSendPkt)
      {
#if(SENDER_RETX == 1)
          if(m_sendretx){
            SequenceNumber32 toTxSeq; 
            toTxSeq = m_High_resend_pos;
            m_High_resend_pos += m_tcb->m_segmentSize;
            m_inflate -= m_tcb->m_segmentSize;
            SendDataPacket(toTxSeq, m_tcb->m_segmentSize, true, pathTag.pid);
            if(m_High_resend_pos >= m_oversendretx){
              m_sendretx = false;
            }
          }else{
            MpRDMASend();
          }
#else
          //printf("at node %u, ReceivedAck() call MpRDMASend()\n", GetNode()->GetId());
          MpRDMASend();
#endif
      }
  }
  else /* On NACK */
  {
      if(!m_bReTx)
      {
          m_bReTx = true;

          if(m_highReTxMark < m_txBuffer->HeadSequence())
              m_highReTxMark = m_txBuffer->HeadSequence();

          m_ooP = m_nextTxSequence - m_tcb->m_segmentSize;
          m_ooL = SequenceNumber32(SafeSubtraction(m_ooP.GetValue(), m_sndL * m_tcb->m_segmentSize));

          m_recoveryPoint = m_nextTxSequence;

          m_inflate = 0;
          printf("at node %u, on NACK, resend data from %u\n", GetNode()->GetId(),m_highReTxMark.GetValue());
      }

      /* Yuanwei patch 6 */
      //lyj modify just retrans don't accept pkt
      if(ackNumber < m_highReTxMark && ackNumber >= m_txBuffer->HeadSequence())
      {
          m_highReTxMark = ackNumber;
          //lyj add
          // if(m_seqAckedMap[m_txBuffer->HeadSequence()].packetSize == 0){
          //   printf("%d seq is %u\n",m_seqAckedMap.size(),m_txBuffer->HeadSequence().GetValue());
          // }
          uint32_t s = m_tcb->m_segmentSize;
          SendDataPacket(m_txBuffer->HeadSequence(), s, true, pathTag.pid);
      }


      /* yuanwei patch 4 */
      //if(ackNumber < m_nextTxSequence)
      //{
      //    m_nextTxSequence = ackNumber;

      //    if(m_nextTxSequence < m_txBuffer->HeadSequence())
      //    {
      //        m_nextTxSequence = m_txBuffer->HeadSequence();
      //    }

      //    m_recoveryPoint = m_nextTxSequence;

      //    if(m_highReTxMark > m_recoveryPoint)
      //    {
      //        m_highReTxMark = m_recoveryPoint;
      //    }
      //}



      if(m_highReTxMark < m_recoveryPoint)
      {
          MpRDMAreTx(pathTag.pid);
      }
      else
      {
          printf("at node %u, on NACK, not send new data,now resendpoint is %u\n", GetNode()->GetId(),m_highReTxMark.GetValue());

          
          /* yuanwei pending patch */
          //lyj debug
          // SequenceNumber32 toTxSeq; 
          // toTxSeq = m_nextTxSequence.Get(); 

          // uint32_t s = m_tcb->m_segmentSize;  
          // uint32_t sz = SendDataPacket(toTxSeq, s, false, pathTag.pid);

          // m_nextTxSequence += sz;


      }
  }

  //printf("at node %u, maxSeq in ACK %u, m_oop %u\n", GetNode()->GetId(), aackTag.maxSeq, m_ooP.GetValue());

  /* ======================== END update m_ooP, m_ooL, inflate, cWnd, pipe =============================== */
 


  if (GetTxAvailable () > 0)
    {
      NotifySend (GetTxAvailable ());
    } 



  //MpRDMASend(); 

  // If there is any data piggybacked, store it into m_rxBuffer
  if (packet->GetSize () > 0)
    {
      ReceivedData (packet, mpRDMAHeader);
    }

  //Yuanwei_Note: FIN will send only when m_seqAckedMap are all acknowledged
  if (m_closeOnEmpty && (m_txBuffer->Size() == 0) && m_seqAckedMap.size() == 0 && m_txBuffer->HeadSequence().GetValue() != 1) 
    {
      
      if (m_state == ESTABLISHED)
        { // On active close: I am the first one to send FIN
          NS_LOG_DEBUG ("ESTABLISHED -> FIN_WAIT_1");
          m_state = FIN_WAIT_1;
          
          CancelAllTimers ();
          macroRTOEvent.Cancel(); 
          SendEmptyPacket (MpRDMAHeader::FIN); 
            printf("at node %u, Send out a self-constructed FIN\n", GetNode()->GetId()); 
        }
      else if (m_state == CLOSE_WAIT)
        { // On passive close: Peer sent me FIN already
          NS_LOG_DEBUG ("CLOSE_WAIT -> LAST_ACK");
          m_state = LAST_ACK;
          
          CancelAllTimers ();
          macroRTOEvent.Cancel(); 
          SendEmptyPacket (MpRDMAHeader::FIN); 
            printf("at node %u, Send out a self-constructed FIN\n", GetNode()->GetId()); 
        }
    }
}

/* Received a packet upon LISTEN state. */
void
MpRDMASocketImpl::ProcessListen (Ptr<Packet> packet, const MpRDMAHeader& mpRDMAHeader,
                              const Address& fromAddress, const Address& toAddress)
{
  NS_LOG_FUNCTION (this << mpRDMAHeader);

  // Extract the flags. PSH and URG are not honoured.
  uint8_t tcpflags = mpRDMAHeader.GetFlags () & ~(MpRDMAHeader::PSH | MpRDMAHeader::URG);

  // Fork a socket if received a SYN. Do nothing otherwise.
  // C.f.: the LISTEN part in tcp_v4_do_rcv() in tcp_ipv4.c in Linux kernel
  if (tcpflags != MpRDMAHeader::SYN)
    {
      return;
    }

  // Call socket's notify function to let the server app know we got a SYN
  // If the server app refuses the connection, do nothing
  if (!NotifyConnectionRequest (fromAddress))
    {
      return;
    }
  // Clone the socket, simulate fork
  Ptr<MpRDMASocketImpl> newSock = Fork ();
  NS_LOG_LOGIC ("Cloned a MpRDMASocketImpl " << newSock);
  Simulator::ScheduleNow (&MpRDMASocketImpl::CompleteFork, newSock,
                          packet, mpRDMAHeader, fromAddress, toAddress);
}

/* Received a packet upon SYN_SENT */
void
MpRDMASocketImpl::ProcessSynSent (Ptr<Packet> packet, const MpRDMAHeader& mpRDMAHeader)
{
  NS_LOG_FUNCTION (this << mpRDMAHeader);

  // Extract the flags. PSH and URG are not honoured.
  uint8_t tcpflags = mpRDMAHeader.GetFlags () & ~(MpRDMAHeader::PSH | MpRDMAHeader::URG);

  if (tcpflags == 0)
    { // Bare data, accept it and move to ESTABLISHED state. This is not a normal behaviour. Remove this?
      NS_LOG_DEBUG ("SYN_SENT -> ESTABLISHED");
      m_state = ESTABLISHED;
      m_connected = true;
      m_retxEvent.Cancel ();
      m_delAckCount = m_delAckMaxCount;
      ReceivedData (packet, mpRDMAHeader);
      Simulator::ScheduleNow (&MpRDMASocketImpl::ConnectionSucceeded, this);
    }
  else if (tcpflags == MpRDMAHeader::ACK)
    { // Ignore ACK in SYN_SENT
    }
  else if (tcpflags == MpRDMAHeader::SYN)
    { // Received SYN, move to SYN_RCVD state and respond with SYN+ACK
      NS_LOG_DEBUG ("SYN_SENT -> SYN_RCVD");
      m_state = SYN_RCVD;
      m_synCount = m_synRetries;
      m_rxBuffer->SetNextRxSequence (mpRDMAHeader.GetSequenceNumber () + SequenceNumber32 (1));
      SendEmptyPacket (MpRDMAHeader::SYN | MpRDMAHeader::ACK);
    }
  else if (tcpflags == (MpRDMAHeader::SYN | MpRDMAHeader::ACK)
           && m_nextTxSequence + SequenceNumber32 (1) == mpRDMAHeader.GetAckNumber ())
    { // Handshake completed
      NS_LOG_DEBUG ("SYN_SENT -> ESTABLISHED");
      m_state = ESTABLISHED;
      m_connected = true;
      m_retxEvent.Cancel ();
      m_rxBuffer->SetNextRxSequence (mpRDMAHeader.GetSequenceNumber () + SequenceNumber32 (1));
      m_highTxMark = ++m_nextTxSequence;
      m_txBuffer->SetHeadSequence (m_nextTxSequence);
      SendEmptyPacket (MpRDMAHeader::ACK);
      //SendPendingData (m_connected);

      //printf("at node %u, ProcessSynSent() call MpRDMASend()\n", GetNode()->GetId());
      MpRDMASend(); 
      Simulator::ScheduleNow (&MpRDMASocketImpl::ConnectionSucceeded, this);
      // Always respond to first data packet to speed up the connection.
      // Remove to get the behaviour of old NS-3 code.
      m_delAckCount = m_delAckMaxCount;
    }
  else
    { // Other in-sequence input
      if (tcpflags != MpRDMAHeader::RST)
        { // When (1) rx of FIN+ACK; (2) rx of FIN; (3) rx of bad flags
          NS_LOG_LOGIC ("Illegal flag " << MpRDMAHeader::FlagsToString (tcpflags) <<
                        " received. Reset packet is sent.");
          SendRST ();
        }
      CloseAndNotify ();
    }
}

/* Received a packet upon SYN_RCVD */
void
MpRDMASocketImpl::ProcessSynRcvd (Ptr<Packet> packet, const MpRDMAHeader& mpRDMAHeader, 
                               const Address& fromAddress, const Address& toAddress)
{
  NS_LOG_FUNCTION (this << mpRDMAHeader);

  // Extract the flags. PSH and URG are not honoured.
  uint8_t tcpflags = mpRDMAHeader.GetFlags () & ~(MpRDMAHeader::PSH | MpRDMAHeader::URG);

  if (tcpflags == 0
      || (tcpflags == MpRDMAHeader::ACK
          && m_nextTxSequence + SequenceNumber32 (1) == mpRDMAHeader.GetAckNumber ()))
    { // If it is bare data, accept it and move to ESTABLISHED state. This is
      // possibly due to ACK lost in 3WHS. If in-sequence ACK is received, the
      // handshake is completed nicely.
      NS_LOG_DEBUG ("SYN_RCVD -> ESTABLISHED");
      m_state = ESTABLISHED;
      m_connected = true;
      m_retxEvent.Cancel ();
      m_highTxMark = ++m_nextTxSequence;
      m_txBuffer->SetHeadSequence (m_nextTxSequence);
      if (m_endPoint)
        {
          m_endPoint->SetPeer (InetSocketAddress::ConvertFrom (fromAddress).GetIpv4 (),
                               InetSocketAddress::ConvertFrom (fromAddress).GetPort ());
        }
      else if (m_endPoint6)
        {
          m_endPoint6->SetPeer (Inet6SocketAddress::ConvertFrom (fromAddress).GetIpv6 (),
                                Inet6SocketAddress::ConvertFrom (fromAddress).GetPort ());
        }
      // Always respond to first data packet to speed up the connection.
      // Remove to get the behaviour of old NS-3 code.
      m_delAckCount = m_delAckMaxCount;
      ReceivedAck (packet, mpRDMAHeader);
      NotifyNewConnectionCreated (this, fromAddress);
      // As this connection is established, the socket is available to send data now
      if (GetTxAvailable () > 0)
        {
          NotifySend (GetTxAvailable ());
        }
    }
  else if (tcpflags == MpRDMAHeader::SYN)
    { // Probably the peer lost my SYN+ACK
      m_rxBuffer->SetNextRxSequence (mpRDMAHeader.GetSequenceNumber () + SequenceNumber32 (1));
      SendEmptyPacket (MpRDMAHeader::SYN | MpRDMAHeader::ACK);
    }
  else if (tcpflags == (MpRDMAHeader::FIN | MpRDMAHeader::ACK))
    {
      if (mpRDMAHeader.GetSequenceNumber () == m_rxBuffer->NextRxSequence ())
        { // In-sequence FIN before connection complete. Set up connection and close.
          m_connected = true;
          m_retxEvent.Cancel ();
          m_highTxMark = ++m_nextTxSequence;
          m_txBuffer->SetHeadSequence (m_nextTxSequence);
          if (m_endPoint)
            {
              m_endPoint->SetPeer (InetSocketAddress::ConvertFrom (fromAddress).GetIpv4 (),
                                   InetSocketAddress::ConvertFrom (fromAddress).GetPort ());
            }
          else if (m_endPoint6)
            {
              m_endPoint6->SetPeer (Inet6SocketAddress::ConvertFrom (fromAddress).GetIpv6 (),
                                    Inet6SocketAddress::ConvertFrom (fromAddress).GetPort ());
            }
          PeerClose (packet, mpRDMAHeader);
        }
    }
  else
    { // Other in-sequence input
      if (tcpflags != MpRDMAHeader::RST)
        { // When (1) rx of SYN+ACK; (2) rx of FIN; (3) rx of bad flags
          NS_LOG_LOGIC ("Illegal flag " << MpRDMAHeader::FlagsToString (tcpflags) <<
                        " received. Reset packet is sent.");
          if (m_endPoint)
            {
              m_endPoint->SetPeer (InetSocketAddress::ConvertFrom (fromAddress).GetIpv4 (),
                                   InetSocketAddress::ConvertFrom (fromAddress).GetPort ());
            }
          else if (m_endPoint6)
            {
              m_endPoint6->SetPeer (Inet6SocketAddress::ConvertFrom (fromAddress).GetIpv6 (),
                                    Inet6SocketAddress::ConvertFrom (fromAddress).GetPort ());
            }
          SendRST ();
        }
      CloseAndNotify ();
    }
}

/* Received a packet upon CLOSE_WAIT, FIN_WAIT_1, or FIN_WAIT_2 states */
void
MpRDMASocketImpl::ProcessWait (Ptr<Packet> packet, const MpRDMAHeader& mpRDMAHeader)
{
  NS_LOG_FUNCTION (this << mpRDMAHeader);

  // Extract the flags. PSH and URG are not honoured.
  uint8_t tcpflags = mpRDMAHeader.GetFlags () & ~(MpRDMAHeader::PSH | MpRDMAHeader::URG);

  if (packet->GetSize () > 0 && tcpflags != MpRDMAHeader::ACK)
    { // Bare data, accept it
      ReceivedData (packet, mpRDMAHeader);
    }
  else if (tcpflags == MpRDMAHeader::ACK)
    { // Process the ACK, and if in FIN_WAIT_1, conditionally move to FIN_WAIT_2
      ReceivedAck (packet, mpRDMAHeader);
      if (m_state == FIN_WAIT_1 && m_txBuffer->Size () == 0
          && mpRDMAHeader.GetAckNumber () == m_highTxMark + SequenceNumber32 (1))
        { // This ACK corresponds to the FIN sent
          NS_LOG_DEBUG ("FIN_WAIT_1 -> FIN_WAIT_2");
          m_state = FIN_WAIT_2;
        }
    }
  else if (tcpflags == MpRDMAHeader::FIN || tcpflags == (MpRDMAHeader::FIN | MpRDMAHeader::ACK))
    { // Got FIN, respond with ACK and move to next state
      if (tcpflags & MpRDMAHeader::ACK)
        { // Process the ACK first
          ReceivedAck (packet, mpRDMAHeader);
        }
      m_rxBuffer->SetFinSequence (mpRDMAHeader.GetSequenceNumber ());
    }
  else if (tcpflags == MpRDMAHeader::SYN || tcpflags == (MpRDMAHeader::SYN | MpRDMAHeader::ACK))
    { // Duplicated SYN or SYN+ACK, possibly due to spurious retransmission
      return;
    }
  else
    { // This is a RST or bad flags
      if (tcpflags != MpRDMAHeader::RST)
        {
          NS_LOG_LOGIC ("Illegal flag " << MpRDMAHeader::FlagsToString (tcpflags) <<
                        " received. Reset packet is sent.");
          SendRST ();
        }
      CloseAndNotify ();
      return;
    }

  // Check if the close responder sent an in-sequence FIN, if so, respond ACK
  if ((m_state == FIN_WAIT_1 || m_state == FIN_WAIT_2) && m_rxBuffer->Finished ())
    {
      if (m_state == FIN_WAIT_1)
        {
          NS_LOG_DEBUG ("FIN_WAIT_1 -> CLOSING");
          m_state = CLOSING;
          if (m_txBuffer->Size () == 0
              && mpRDMAHeader.GetAckNumber () == m_highTxMark + SequenceNumber32 (1))
            { // This ACK corresponds to the FIN sent
              TimeWait ();
            }
        }
      else if (m_state == FIN_WAIT_2)
        {
          TimeWait ();
        }
      SendEmptyPacket (MpRDMAHeader::ACK);
      if (!m_shutdownRecv)
        {
          NotifyDataRecv ();
        }
    }
}

/* Received a packet upon CLOSING */
void
MpRDMASocketImpl::ProcessClosing (Ptr<Packet> packet, const MpRDMAHeader& mpRDMAHeader)
{
  NS_LOG_FUNCTION (this << mpRDMAHeader);

  // Extract the flags. PSH and URG are not honoured.
  uint8_t tcpflags = mpRDMAHeader.GetFlags () & ~(MpRDMAHeader::PSH | MpRDMAHeader::URG);

  if (tcpflags == MpRDMAHeader::ACK)
    {
      if (mpRDMAHeader.GetSequenceNumber () == m_rxBuffer->NextRxSequence ())
        { // This ACK corresponds to the FIN sent
          TimeWait ();
        }
    }
  else
    { // CLOSING state means simultaneous close, i.e. no one is sending data to
      // anyone. If anything other than ACK is received, respond with a reset.
      if (tcpflags == MpRDMAHeader::FIN || tcpflags == (MpRDMAHeader::FIN | MpRDMAHeader::ACK))
        { // FIN from the peer as well. We can close immediately.
          SendEmptyPacket (MpRDMAHeader::ACK);
        }
      else if (tcpflags != MpRDMAHeader::RST)
        { // Receive of SYN or SYN+ACK or bad flags or pure data
          NS_LOG_LOGIC ("Illegal flag " << MpRDMAHeader::FlagsToString (tcpflags) << " received. Reset packet is sent.");
          SendRST ();
        }
      CloseAndNotify ();
    }
}

/* Received a packet upon LAST_ACK */
void
MpRDMASocketImpl::ProcessLastAck (Ptr<Packet> packet, const MpRDMAHeader& mpRDMAHeader)
{
  NS_LOG_FUNCTION (this << mpRDMAHeader);

  // Extract the flags. PSH and URG are not honoured.
  uint8_t tcpflags = mpRDMAHeader.GetFlags () & ~(MpRDMAHeader::PSH | MpRDMAHeader::URG);

  if (tcpflags == 0)
    {
      ReceivedData (packet, mpRDMAHeader);
    }
  else if (tcpflags == MpRDMAHeader::ACK)
    {
        /*Yuanwei_Note: comment this to avoid retransmit a FIN */
      //if (mpRDMAHeader.GetSequenceNumber () == m_rxBuffer->NextRxSequence ())
        { // This ACK corresponds to the FIN sent. This socket closed peacefully.
          CloseAndNotify ();
        }
    }
  else if (tcpflags == MpRDMAHeader::FIN)
    { // Received FIN again, the peer probably lost the FIN+ACK
      SendEmptyPacket (MpRDMAHeader::FIN | MpRDMAHeader::ACK);
    }
  else if (tcpflags == (MpRDMAHeader::FIN | MpRDMAHeader::ACK) || tcpflags == MpRDMAHeader::RST)
    {
      CloseAndNotify ();
    }
  else
    { // Received a SYN or SYN+ACK or bad flags
      NS_LOG_LOGIC ("Illegal flag " << MpRDMAHeader::FlagsToString (tcpflags) << " received. Reset packet is sent.");
      SendRST ();
      CloseAndNotify ();
    }
}

/* Peer sent me a FIN. Remember its sequence in rx buffer. */
void
MpRDMASocketImpl::PeerClose (Ptr<Packet> p, const MpRDMAHeader& mpRDMAHeader)
{
  NS_LOG_FUNCTION (this << mpRDMAHeader);

  // Ignore all out of range packets
  if (mpRDMAHeader.GetSequenceNumber () < m_rxBuffer->NextRxSequence ()
      || mpRDMAHeader.GetSequenceNumber () > m_rxBuffer->MaxRxSequence ())
    {
      return;
    }
  // For any case, remember the FIN position in rx buffer first
  m_rxBuffer->SetFinSequence (mpRDMAHeader.GetSequenceNumber () + SequenceNumber32 (p->GetSize ()));
  NS_LOG_LOGIC ("Accepted FIN at seq " << mpRDMAHeader.GetSequenceNumber () + SequenceNumber32 (p->GetSize ()));
  // If there is any piggybacked data, process it
  if (p->GetSize ())
    {
      ReceivedData (p, mpRDMAHeader);
    }
  // Return if FIN is out of sequence, otherwise move to CLOSE_WAIT state by DoPeerClose
  if (!m_rxBuffer->Finished ())
    {
      return;
    }

  // Simultaneous close: Application invoked Close() when we are processing this FIN packet
  if (m_state == FIN_WAIT_1)
    {
      NS_LOG_DEBUG ("FIN_WAIT_1 -> CLOSING");
      m_state = CLOSING;
      return;
    }

  DoPeerClose (); // Change state, respond with ACK
}

/* Received a in-sequence FIN. Close down this socket. */
void
MpRDMASocketImpl::DoPeerClose (void)
{
  NS_ASSERT (m_state == ESTABLISHED || m_state == SYN_RCVD);

  // Move the state to CLOSE_WAIT
  NS_LOG_DEBUG (MpRDMAStateName[m_state] << " -> CLOSE_WAIT");
  m_state = CLOSE_WAIT;

  if (!m_closeNotified)
    {
      // The normal behaviour for an application is that, when the peer sent a in-sequence
      // FIN, the app should prepare to close. The app has two choices at this point: either
      // respond with ShutdownSend() call to declare that it has nothing more to send and
      // the socket can be closed immediately; or remember the peer's close request, wait
      // until all its existing data are pushed into the TCP socket, then call Close()
      // explicitly.
      NS_LOG_LOGIC ("MpRDMA " << this << " calling NotifyNormalClose");
      NotifyNormalClose ();
      m_closeNotified = true;
    }
  if (m_shutdownSend)
    { // The application declares that it would not sent any more, close this socket
      Close ();
    }
  else
    { // Need to ack, the application will close later
      SendEmptyPacket (MpRDMAHeader::ACK);
//wsq add for test
//printf("wsqtime:%lf \n",Simulator::Now().GetSeconds() );
    }
  if (m_state == LAST_ACK)
    {
      NS_LOG_LOGIC ("MpRDMASocketImpl " << this << " scheduling LATO1");
      Time lastRto = m_rtt->GetEstimate () + Max (m_clockGranularity, m_rtt->GetVariation () * 4);
      m_lastAckEvent = Simulator::Schedule (lastRto, &MpRDMASocketImpl::LastAckTimeout, this);
    }
}

/* Kill this socket. This is a callback function configured to m_endpoint in
   SetupCallback(), invoked when the endpoint is destroyed. */
void
MpRDMASocketImpl::Destroy (void)
{
  NS_LOG_FUNCTION (this);
  m_endPoint = 0;
  if (m_mpRDMA != 0)
    {
      m_mpRDMA->RemoveSocket (this);
    }
  NS_LOG_LOGIC (this << " Cancelled ReTxTimeout event which was set to expire at " <<
                (Simulator::Now () + Simulator::GetDelayLeft (m_retxEvent)).GetSeconds ());
  CancelAllTimers ();
}

/* Kill this socket. This is a callback function configured to m_endpoint in
   SetupCallback(), invoked when the endpoint is destroyed. */
void
MpRDMASocketImpl::Destroy6 (void)
{
  NS_LOG_FUNCTION (this);
  m_endPoint6 = 0;
  if (m_mpRDMA != 0)
    {
      m_mpRDMA->RemoveSocket (this);
    }
  NS_LOG_LOGIC (this << " Cancelled ReTxTimeout event which was set to expire at " <<
                (Simulator::Now () + Simulator::GetDelayLeft (m_retxEvent)).GetSeconds ());
  CancelAllTimers ();
}

/* Send an empty packet with specified TCP flags */
void
MpRDMASocketImpl::SendEmptyPacket (uint8_t flags)
{
  NS_LOG_FUNCTION (this << (uint32_t)flags);
  Ptr<Packet> p = Create<Packet> ();
  MpRDMAHeader header; 
  SequenceNumber32 s = m_nextTxSequence;

  /*
   * Add tags for each socket option.
   * Note that currently the socket adds both IPv4 tag and IPv6 tag
   * if both options are set. Once the packet got to layer three, only
   * the corresponding tags will be read.
   */
  if (IsManualIpTos ())
    {
      SocketIpTosTag ipTosTag;
      ipTosTag.SetTos (GetIpTos ());
      p->AddPacketTag (ipTosTag);
    }

  if (IsManualIpv6Tclass ())
    {
      SocketIpv6TclassTag ipTclassTag;
      ipTclassTag.SetTclass (GetIpv6Tclass ());
      p->AddPacketTag (ipTclassTag);
    }

  if (IsManualIpTtl ())
    {
      SocketIpTtlTag ipTtlTag;
      ipTtlTag.SetTtl (GetIpTtl ());
      p->AddPacketTag (ipTtlTag);
    }

  if (IsManualIpv6HopLimit ())
    {
      SocketIpv6HopLimitTag ipHopLimitTag;
      ipHopLimitTag.SetHopLimit (GetIpv6HopLimit ());
      p->AddPacketTag (ipHopLimitTag);
    }

  if (m_endPoint == 0 && m_endPoint6 == 0)
    {
      NS_LOG_WARN ("Failed to send empty packet due to null endpoint");
      return;
    }
  if (flags & MpRDMAHeader::FIN)
    {
      flags |= MpRDMAHeader::ACK;
    }
  else if (m_state == FIN_WAIT_1 || m_state == LAST_ACK || m_state == CLOSING)
    {
      ++s;
    }

  header.SetFlags (flags);
  header.SetSequenceNumber (s);
  header.SetAckNumber (m_rxBuffer->NextRxSequence ());
  if (m_endPoint != 0)
    {
      header.SetSourcePort (m_endPoint->GetLocalPort ());
      header.SetDestinationPort (m_endPoint->GetPeerPort ());
    }
  else
    {
      header.SetSourcePort (m_endPoint6->GetLocalPort ());
      header.SetDestinationPort (m_endPoint6->GetPeerPort ());
    }
  AddOptions (header);
  header.SetWindowSize (AdvertisedWindowSize ());

  // RFC 6298, clause 2.4
  m_rto = Max (m_rtt->GetEstimate () + Max (m_clockGranularity, m_rtt->GetVariation () * 4), m_minRto);

  bool hasSyn = flags & MpRDMAHeader::SYN;
  bool hasFin = flags & MpRDMAHeader::FIN;
  bool isAck = flags == MpRDMAHeader::ACK;
  if (hasSyn)
    {
      if (m_synCount == 0)
        { // No more connection retries, give up
          NS_LOG_LOGIC ("Connection failed.");
          m_rtt->Reset (); //According to recommendation -> RFC 6298
          CloseAndNotify ();
          return;
        }
      else
        { // Exponential backoff of connection time out
          int backoffCount = 0x1 << (m_synRetries - m_synCount);
          m_rto = m_cnTimeout * backoffCount;
          m_synCount--;
        }

      if (m_synRetries - 1 == m_synCount)
        {
          UpdateRttHistory (s, 0, false);
        }
      else
        { // This is SYN retransmission
          UpdateRttHistory (s, 0, true);
        }
    }

  m_txTrace (p, header, this);

  if (m_endPoint != 0)
    {
      m_mpRDMA->SendPacket (p, header, m_endPoint->GetLocalAddress (),
                         m_endPoint->GetPeerAddress (), m_boundnetdevice);
    }
  else
    {
      m_mpRDMA->SendPacket (p, header, m_endPoint6->GetLocalAddress (),
                         m_endPoint6->GetPeerAddress (), m_boundnetdevice);
    }

  if (flags & MpRDMAHeader::ACK)
    { // If sending an ACK, cancel the delay ACK as well
      m_delAckEvent.Cancel ();
      m_delAckCount = 0;
      if (m_highTxAck < header.GetAckNumber ())
        {
          m_highTxAck = header.GetAckNumber ();
        }
    }
  if (m_retxEvent.IsExpired () && (hasSyn || hasFin) && !isAck )
    { // Retransmit SYN / SYN+ACK / FIN / FIN+ACK to guard against lost
      NS_LOG_LOGIC ("Schedule retransmission timeout at time "
                    << Simulator::Now ().GetSeconds () << " to expire at time "
                    << (Simulator::Now () + m_rto.Get ()).GetSeconds ());
      m_retxEvent = Simulator::Schedule (m_rto, &MpRDMASocketImpl::SendEmptyPacket, this, flags);
    }
}

/* This function closes the endpoint completely. Called upon RST_TX action. */
void
MpRDMASocketImpl::SendRST (void)
{
  NS_LOG_FUNCTION (this);
  SendEmptyPacket (MpRDMAHeader::RST);
  NotifyErrorClose ();
  DeallocateEndPoint ();
}

/* Deallocate the end point and cancel all the timers */
void
MpRDMASocketImpl::DeallocateEndPoint (void)
{
  if (m_endPoint != 0)
    {
      CancelAllTimers ();
      m_endPoint->SetDestroyCallback (MakeNullCallback<void> ());
      m_mpRDMA->DeAllocate (m_endPoint);
      m_endPoint = 0;
      m_mpRDMA->RemoveSocket (this);
    }
  else if (m_endPoint6 != 0)
    {
      CancelAllTimers ();
      m_endPoint6->SetDestroyCallback (MakeNullCallback<void> ());
      m_mpRDMA->DeAllocate (m_endPoint6);
      m_endPoint6 = 0;
      m_mpRDMA->RemoveSocket (this);
    }
}

/* Configure the endpoint to a local address. Called by Connect() if Bind() didn't specify one. */
int
MpRDMASocketImpl::SetupEndpoint ()
{
  NS_LOG_FUNCTION (this);
  Ptr<Ipv4> ipv4 = m_node->GetObject<Ipv4> ();
  NS_ASSERT (ipv4 != 0);
  if (ipv4->GetRoutingProtocol () == 0)
    {
      NS_FATAL_ERROR ("No Ipv4RoutingProtocol in the node");
    }
  // Create a dummy packet, then ask the routing function for the best output
  // interface's address
  Ipv4Header header;
  header.SetDestination (m_endPoint->GetPeerAddress ());
  Socket::SocketErrno errno_;
  Ptr<Ipv4Route> route;
  Ptr<NetDevice> oif = m_boundnetdevice;
  route = ipv4->GetRoutingProtocol ()->RouteOutput (Ptr<Packet> (), header, oif, errno_);
  if (route == 0)
    {
      NS_LOG_LOGIC ("Route to " << m_endPoint->GetPeerAddress () << " does not exist");
      NS_LOG_ERROR (errno_);
      m_errno = errno_;
      return -1;
    }
  NS_LOG_LOGIC ("Route exists");
  m_endPoint->SetLocalAddress (route->GetSource ());
  return 0;
}

int
MpRDMASocketImpl::SetupEndpoint6 ()
{
  NS_LOG_FUNCTION (this);
  Ptr<Ipv6L3Protocol> ipv6 = m_node->GetObject<Ipv6L3Protocol> ();
  NS_ASSERT (ipv6 != 0);
  if (ipv6->GetRoutingProtocol () == 0)
    {
      NS_FATAL_ERROR ("No Ipv6RoutingProtocol in the node");
    }
  // Create a dummy packet, then ask the routing function for the best output
  // interface's address
  Ipv6Header header;
  header.SetDestinationAddress (m_endPoint6->GetPeerAddress ());
  Socket::SocketErrno errno_;
  Ptr<Ipv6Route> route;
  Ptr<NetDevice> oif = m_boundnetdevice;
  route = ipv6->GetRoutingProtocol ()->RouteOutput (Ptr<Packet> (), header, oif, errno_);
  if (route == 0)
    {
      NS_LOG_LOGIC ("Route to " << m_endPoint6->GetPeerAddress () << " does not exist");
      NS_LOG_ERROR (errno_);
      m_errno = errno_;
      return -1;
    }
  NS_LOG_LOGIC ("Route exists");
  m_endPoint6->SetLocalAddress (route->GetSource ());
  return 0;
}

/* This function is called only if a SYN received in LISTEN state. After
   MpRDMASocketImpl cloned, allocate a new end point to handle the incoming
   connection and send a SYN+ACK to complete the handshake. */
void
MpRDMASocketImpl::CompleteFork (Ptr<Packet> p, const MpRDMAHeader& h, 
                             const Address& fromAddress, const Address& toAddress)
{
  // Get port and address from peer (connecting host)
  if (InetSocketAddress::IsMatchingType (toAddress))
    {
      m_endPoint = m_mpRDMA->Allocate (InetSocketAddress::ConvertFrom (toAddress).GetIpv4 (),
                                    InetSocketAddress::ConvertFrom (toAddress).GetPort (),
                                    InetSocketAddress::ConvertFrom (fromAddress).GetIpv4 (),
                                    InetSocketAddress::ConvertFrom (fromAddress).GetPort ());
      m_endPoint6 = 0;
    }
  else if (Inet6SocketAddress::IsMatchingType (toAddress))
    {
      m_endPoint6 = m_mpRDMA->Allocate6 (Inet6SocketAddress::ConvertFrom (toAddress).GetIpv6 (),
                                      Inet6SocketAddress::ConvertFrom (toAddress).GetPort (),
                                      Inet6SocketAddress::ConvertFrom (fromAddress).GetIpv6 (),
                                      Inet6SocketAddress::ConvertFrom (fromAddress).GetPort ());
      m_endPoint = 0;
    }
  m_mpRDMA->AddSocket (this);

  // Change the cloned socket from LISTEN state to SYN_RCVD
  NS_LOG_DEBUG ("LISTEN -> SYN_RCVD");
  m_state = SYN_RCVD;
  m_synCount = m_synRetries;
  m_dataRetrCount = m_dataRetries;
  SetupCallback ();
  // Set the sequence number and send SYN+ACK
  m_rxBuffer->SetNextRxSequence (h.GetSequenceNumber () + SequenceNumber32 (1));

  SendEmptyPacket (MpRDMAHeader::SYN | MpRDMAHeader::ACK);
}

void
MpRDMASocketImpl::ConnectionSucceeded ()
{ // Wrapper to protected function NotifyConnectionSucceeded() so that it can
  // be called as a scheduled event
  NotifyConnectionSucceeded ();
  // The if-block below was moved from ProcessSynSent() to here because we need
  // to invoke the NotifySend() only after NotifyConnectionSucceeded() to
  // reflect the behaviour in the real world.
  if (GetTxAvailable () > 0)
    {
      NotifySend (GetTxAvailable ());
    }
}

/* Extract at most maxSize bytes from the TxBuffer at sequence seq, add the
    MP-RDMA header, and send to MpRDMAL4Protocol */
/* Yuanwei_Note: send a data packet should do the following: 
 * 1. add ECN tag, path ID tag 
 * 2. update m_seqAckedMap
 * 3. start a retransmission timeout 
 */ 
uint32_t
MpRDMASocketImpl::SendDataPacket (SequenceNumber32 seq, uint32_t maxSize, bool withAck, uint32_t pathId)
{
  bool isReTxPacket = withAck;
  
  withAck = false; 
  NS_LOG_FUNCTION (this << seq << maxSize << withAck);

  bool isRetransmission = false;
  if (seq != m_highTxMark)
    {
      isRetransmission = true;
    }
  //lyj debug
  // printf("at node %u, maxSize %u, seq %u, head %u, tail %u, nextTx %u, reTxMark %u, recoveryPoint %u\n", 
  //        GetNode()->GetId(), maxSize, seq.GetValue(), m_txBuffer->HeadSequence().GetValue(), m_txBuffer->TailSequence().GetValue(), 
  //        m_nextTxSequence.Get().GetValue(), m_highReTxMark.GetValue(), m_recoveryPoint.GetValue());

  Ptr<Packet> p = m_txBuffer->CopyFromSequence (maxSize, seq);
  uint32_t sz = p->GetSize (); // Size of packet

  //printf("at node %u, SendDataPacket(), maxSize %u, seq %u, actual packet size %u, txbuffer size %u\n", GetNode()->GetId(), maxSize, seq.GetValue(), sz, m_txBuffer->Size());


  uint8_t flags = withAck ? MpRDMAHeader::ACK : 0;
  uint32_t remainingData = m_txBuffer->SizeFromSequence (seq + SequenceNumber32 (sz));

  if (withAck)
    {
      m_delAckEvent.Cancel ();
      m_delAckCount = 0;
    }

  /*
   * Add tags for each socket option.
   * Note that currently the socket adds both IPv4 tag and IPv6 tag
   * if both options are set. Once the packet got to layer three, only
   * the corresponding tags will be read.
   */
  if (IsManualIpTos ())
    {
      SocketIpTosTag ipTosTag;
      ipTosTag.SetTos (GetIpTos ());
      p->AddPacketTag (ipTosTag);
    }

  if (IsManualIpv6Tclass ())
    {
      SocketIpv6TclassTag ipTclassTag;
      ipTclassTag.SetTclass (GetIpv6Tclass ());
      p->AddPacketTag (ipTclassTag);
    }

  if (IsManualIpTtl ())
    {
      SocketIpTtlTag ipTtlTag;
      ipTtlTag.SetTtl (GetIpTtl ());
      p->AddPacketTag (ipTtlTag);
    }

  if (IsManualIpv6HopLimit ())
    {
      SocketIpv6HopLimitTag ipHopLimitTag;
      ipHopLimitTag.SetHopLimit (GetIpv6HopLimit ());
      p->AddPacketTag (ipHopLimitTag);
    }

  MpRDMAHeader header;
  header.SetFlags (flags);
  header.SetSequenceNumber (seq);
  header.SetAckNumber (m_rxBuffer->NextRxSequence ());
  if (m_endPoint)
    {
      header.SetSourcePort (m_endPoint->GetLocalPort ());
      header.SetDestinationPort (m_endPoint->GetPeerPort ());
    }
  else
    {
      header.SetSourcePort (m_endPoint6->GetLocalPort ());
      header.SetDestinationPort (m_endPoint6->GetPeerPort ());
    }
  
  header.SetWindowSize (AdvertisedWindowSize ());
  AddOptions (header);
  
  // add ECN, packetId tag
  PathIdTag pathTag; 
  if(m_senderState == InitState)
  {
      //printf("at node %u, init send out a packet to path %u, nextTx %u, pipe %u\n", GetNode()->GetId(), m_maxPathId, m_nextTxSequence.Get().GetValue(), m_pipe); 
      pathTag.pid = m_maxPathId + GetNode()->GetId(); 
      //m_maxPathId++;
  }
  else
  {
      pathTag.pid = pathId; 
  } 
  p->AddPacketTag(pathTag); 

  if(GetNode()->GetId()<=10)
  {
    // fprintf(stderr, "node %u MpRDMASend--->: local port %u, peer port %u\n", GetNode()->GetId(), header.GetSourcePort(), header.GetDestinationPort());
  
    if ( m_path_cwnd_log.find(pathTag.pid) == m_path_cwnd_log.end() )
      m_path_cwnd_log[pathTag.pid]=1;
    else
      m_path_cwnd_log[pathTag.pid]++;
    // fprintf(stderr,"%u,%u(+): %d\n",GetNode()->GetId(),pathTag.pid,m_path_cwnd_log[pathTag.pid]);
  }


  EcnTag ecnTag;  
  ecnTag.ip_bit_1 = true; 
  ecnTag.ip_bit_2 = false;  
  p->AddPacketTag(ecnTag);

  /* add a timestamp tag */
  double timeNow = Simulator::Now().GetSeconds(); 
  TimeStampTag timeTag; 
  timeTag.timeStamp = timeNow;
  timeTag.rWndTS = 0; 
  p->AddPacketTag(timeTag);

  ReTxTag retxTag;
  if(isReTxPacket)
  {
      retxTag.isReTx = 1;
  }
  else
  {
      retxTag.isReTx = 0;
  }
  p->AddPacketTag(retxTag);




#if (TEST_DELTA_T == 1)
  if(m_markFence)
  {
      FenceTag fenceT;
      fenceT.fence = 1;
      p->AddPacketTag(fenceT);
      m_markFence = false;
  }

#endif



  //update m_seqAckedMap and setup rto timer
  if(m_seqAckedMap.find(header.GetSequenceNumber()) == m_seqAckedMap.end())
  {
     AckedBlock block; 
     block.packetSize = p->GetSize(); 
     //lyj add 
    //  if(header.GetSequenceNumber().GetValue() == 814213){
    //    printf("pkt is %u\n",block.packetSize);
    //  }
     block.acked = false;
     m_seqAckedMap[header.GetSequenceNumber()] = block;
  }

  /* a macro RTO event to put every thing into init mode */
  macroRTOEvent.Cancel(); 
  if(macroRTOEvent.IsExpired())
  {
      //macroRTOEvent = Simulator::Schedule(Seconds(1.0), &MpRDMASocketImpl::MacroTimeout, this);   
      macroRTOEvent = Simulator::Schedule(m_mpRdmaRto, &MpRDMASocketImpl::MacroTimeout, this);   
  }

  m_txTrace (p, header, this);

  m_pktTotalQuota += PKT_QUOTA;

  if (m_endPoint)
    {
      m_mpRDMA->SendPacket (p, header, m_endPoint->GetLocalAddress (),
                         m_endPoint->GetPeerAddress (), m_boundnetdevice);
      NS_LOG_DEBUG ("Send segment of size " << sz << " with remaining data " <<
                    remainingData << " via MpRDMAL4Protocol to " <<  m_endPoint->GetPeerAddress () <<
                    ". Header " << header);
      //LYJ debug
      //printf("at node %u, time %lf, send a packet with size %u\n", GetNode()->GetId(), Simulator::Now().GetSeconds(), sz);
    }
  else
    {
      m_mpRDMA->SendPacket (p, header, m_endPoint6->GetLocalAddress (),
                         m_endPoint6->GetPeerAddress (), m_boundnetdevice);
      NS_LOG_DEBUG ("Send segment of size " << sz << " with remaining data " <<
                    remainingData << " via MpRDMAL4Protocol to " <<  m_endPoint6->GetPeerAddress () <<
                    ". Header " << header);
    }

  UpdateRttHistory (seq, sz, isRetransmission);

  // Notify the application of the data being sent unless this is a retransmit
  if (seq + sz > m_highTxMark)
    {
      Simulator::ScheduleNow (&MpRDMASocketImpl::NotifyDataSent, this, (seq + sz - m_highTxMark.Get ()));
    }
  // Update highTxMark
  m_highTxMark = std::max (seq + sz, m_highTxMark.Get ());
  return sz;
}

void
MpRDMASocketImpl::UpdateRttHistory (const SequenceNumber32 &seq, uint32_t sz,
                                 bool isRetransmission)
{
  NS_LOG_FUNCTION (this);

  // update the history of sequence numbers used to calculate the RTT
  if (isRetransmission == false)
    { // This is the next expected one, just log at end
      m_history.push_back (RttHistory (seq, sz, Simulator::Now ()));
    }
  else
    { // This is a retransmit, find in list and mark as re-tx
      for (RttHistory_t::iterator i = m_history.begin (); i != m_history.end (); ++i)
        {
          if ((seq >= i->seq) && (seq < (i->seq + SequenceNumber32 (i->count))))
            { // Found it
              i->retx = true;
              i->count = ((seq + SequenceNumber32 (sz)) - i->seq); // And update count in hist
              break;
            }
        }
    }
}

/* Send as much pending data as possible according to the Tx window. Note that
 *  this function did not implement the PSH flag
 */
bool
MpRDMASocketImpl::SendPendingData (bool withAck)
{
  NS_LOG_FUNCTION (this << withAck); 

  if (m_txBuffer->Size () == 0)
    { 
      return false;                           // Nothing to send
    } 

  if (m_endPoint == 0 && m_endPoint6 == 0)
    {
      NS_LOG_INFO ("MpRDMASocketImpl::SendPendingData: No endpoint; m_shutdownSend=" << m_shutdownSend);
      return false; // Is this the right way to handle this condition?
    }
  uint32_t nPacketsSent = 0;
  while (m_txBuffer->SizeFromSequence (m_nextTxSequence))
    {
      uint32_t w = AvailableWindow (); // Get available window size
      // Stop sending if we need to wait for a larger Tx window (prevent silly window syndrome)
      if (w < m_tcb->m_segmentSize && m_txBuffer->SizeFromSequence (m_nextTxSequence) > w)
        {
          NS_LOG_LOGIC ("Preventing Silly Window Syndrome. Wait to send.");
          //printf ("Preventing Silly Window Syndrome. Wait to send., w is %u \n", w); 
          break; // No more
        }
      // Nagle's algorithm (RFC896): Hold off sending if there is unacked data
      // in the buffer and the amount of data to send is less than one segment
      if (!m_noDelay && UnAckDataCount () > 0
          && m_txBuffer->SizeFromSequence (m_nextTxSequence) < m_tcb->m_segmentSize)
        {
          NS_LOG_LOGIC ("Invoking Nagle's algorithm. Wait to send.");
          break;
        }
      NS_LOG_LOGIC ("MpRDMASocketImpl " << this << " SendPendingData" <<
                    " w " << w <<
                    " rxwin " << m_rWnd <<
                    " segsize " << m_tcb->m_segmentSize <<
                    " nextTxSeq " << m_nextTxSequence <<
                    " highestRxAck " << m_txBuffer->HeadSequence () <<
                    " pd->Size " << m_txBuffer->Size () <<
                    " pd->SFS " << m_txBuffer->SizeFromSequence (m_nextTxSequence));

      NS_LOG_DEBUG ("Window: " << w <<
                    " cWnd: " << m_tcb->m_cWnd <<
                    " unAck: " << UnAckDataCount ());

      uint32_t s = std::min (w, m_tcb->m_segmentSize);  // Send no more than window 
      //lyj debug
          // if(s == 0){
          //           printf("%s %d\n",__FUNCTION__,__LINE__);
          //         }
      uint32_t sz = SendDataPacket (m_nextTxSequence, s, false, m_maxPathId);
      nPacketsSent++;                             // Count sent this loop
      m_nextTxSequence += sz;                     // Advance next tx sequence 
      m_pipe += sz; 
    }
  if (nPacketsSent > 0)
    {
      NS_LOG_DEBUG ("SendPendingData sent " << nPacketsSent << " segments");
    }

  return (nPacketsSent > 0);
}

/* Yuanwei: change this to reflect out-of-order arrival */ 
uint32_t
MpRDMASocketImpl::UnAckDataCount ()  
{
  NS_LOG_FUNCTION (this);
  
  /* unackdata should take into account acked data in m_seAckedMap */
  int count = 0;

  for (std::map<SequenceNumber32, AckedBlock>::iterator it = m_seqAckedMap.begin(); it != m_seqAckedMap.end(); it++)
  {
      if (it->second.acked == false) //only count unACK data 
      {
          count += it->second.packetSize;
          if(it->first <= m_highReTxMark) //also count retransmission data
          {
              count += it->second.packetSize; 
          } 
      } 
  }

  return count; 
}

uint32_t
MpRDMASocketImpl::UnAckDataCountwithoutReTx ()  
{
  NS_LOG_FUNCTION (this);
  
  /* unackdata should take into account acked data in m_seAckedMap */
  int count = 0;

  for (std::map<SequenceNumber32, AckedBlock>::iterator it = m_seqAckedMap.begin(); it != m_seqAckedMap.end(); it++)
  {
      if (it->second.acked == false) //only count unACK data 
      {
          count += it->second.packetSize;
      } 
  }

  return count; 
}


uint32_t
MpRDMASocketImpl::BytesInFlight ()
{
  NS_LOG_FUNCTION (this);
  // Previous (see bug 1783):
  // uint32_t bytesInFlight = m_highTxMark.Get () - m_txBuffer->HeadSequence ();
  // RFC 4898 page 23
  // PipeSize=SND.NXT-SND.UNA+(retransmits-dupacks)*CurMSS

  // flightSize == UnAckDataCount (), but we avoid the call to save log lines
  uint32_t flightSize = m_nextTxSequence.Get () - m_txBuffer->HeadSequence ();
  uint32_t duplicatedSize;
  uint32_t bytesInFlight;

  if (m_retransOut > m_dupAckCount)
    {
      duplicatedSize = (m_retransOut - m_dupAckCount)*m_tcb->m_segmentSize;
      bytesInFlight = flightSize + duplicatedSize;
    }
  else
    {
      duplicatedSize = (m_dupAckCount - m_retransOut)*m_tcb->m_segmentSize;
      bytesInFlight = duplicatedSize > flightSize ? 0 : flightSize - duplicatedSize;
    }

  // m_bytesInFlight is traced; avoid useless assignments which would fire
  // fruitlessly the callback
  if (m_bytesInFlight != bytesInFlight)
    {
      m_bytesInFlight = bytesInFlight;
    }

  return bytesInFlight;
}

uint32_t
MpRDMASocketImpl::Window (void) 
{
  NS_LOG_FUNCTION (this);
  return std::min (m_rWnd.Get (), m_tcb->m_cWnd.Get ());
}

uint32_t
MpRDMASocketImpl::AvailableWindow () 
{
  NS_LOG_FUNCTION_NOARGS ();
  uint32_t unack = UnAckDataCount (); // Number of outstanding bytes
  uint32_t win = Window ();           // Number of bytes allowed to be outstanding

  NS_LOG_DEBUG ("UnAckCount=" << unack << ", Win=" << win);
  //printf ("UnAckCount= %u, Win= %u, avai= %u\n", unack, win, (win < unack) ? 0 : (win - unack)); 
  return (win < unack) ? 0 : (win - unack); 

  uint32_t w1 = (m_rWnd.Get() < UnAckDataCountwithoutReTx()) ? 0 : (m_rWnd.Get() - UnAckDataCountwithoutReTx()); 
  uint32_t w2 = (m_tcb->m_cWnd.Get() < UnAckDataCount()) ? 0 : (m_tcb->m_cWnd.Get() - UnAckDataCount());
  return std::min (w1, w2); 
}

uint16_t
MpRDMASocketImpl::AdvertisedWindowSize () const
{
  //uint32_t w = m_rxBuffer->MaxBufferSize ();
  uint32_t w; 
  if(m_rxBuffer->MaxBufferSize () > m_rxBuffer->Size()) 
  {
      w = m_rxBuffer->MaxBufferSize () - m_rxBuffer->Size(); 
  }
  else 
      w = 0; 

  w >>= m_rcvWindShift;

  if (w > m_maxWinSize)
    {
      NS_LOG_WARN ("There is a loss in the adv win size, wrt buffer size");
      w = m_maxWinSize;
    }

  return (uint16_t) w;
}

// Receipt of new packet, put into Rx buffer
// Yuanwei_Note: Data ACK should do the following: 
// 1. ACK sequence = Data sequence 
// 2. Carry ECN back to sender 
// 3. Carry PathID back to sender 
void
MpRDMASocketImpl::ReceivedData (Ptr<Packet> p, const MpRDMAHeader& mpRDMAHeader)
{


  NS_LOG_FUNCTION (this << mpRDMAHeader);
  NS_LOG_DEBUG ("Data segment, seq=" << mpRDMAHeader.GetSequenceNumber () <<
                " pkt size=" << p->GetSize () );

  // Put into Rx buffer
  SequenceNumber32 expectedSeq = m_rxBuffer->NextRxSequence ();
  /*lyj debug*/
  //printf("at node %u except seq is %u\n",GetNode()->GetId(),expectedSeq.GetValue());
  uint32_t curNode = GetNode()->GetId();
  m_rxBuffer->SetNodeId(curNode);
  m_rxBuffer->SetSndL(m_sndL);
  m_rxBuffer->SetRcvL(m_rcvL);

  //printf("at node %u received data \n", GetNode()->GetId());
  /* insert to buffer only when incoming packet is within range of rcvL (out-of-order control)*/
  if(expectedSeq + m_rcvL * m_tcb->m_segmentSize <= mpRDMAHeader.GetSequenceNumber())  /* packet is out of range */
  {
      //lyj add
      //printf("at node %u, out of range, expect %u, m_rcvL %u, incoming %u\n", GetNode()->GetId(), expectedSeq.GetValue(), m_rcvL, mpRDMAHeader.GetSequenceNumber().GetValue()); 

      /* NACK */
      SendDataAckPacket(p, mpRDMAHeader);
      return;
  }



#if (TEST_DELTA_T == 1)
  if(m_endPoint->GetLocalPort () == 1001)
  {
    FenceTag fenceTag;
    p->PeekPacketTag(fenceTag);

    if(fenceTag.fence == 1)
    {
        m_totalFenceCount ++;
        //if(mpRDMAHeader.GetSequenceNumber() < m_rxBuffer->MaxSeqInBuf())
        if(mpRDMAHeader.GetSequenceNumber() != m_rxBuffer->NextRxSequence())
        {
            m_violateFenceCount++;
        }

        LogManager::WriteLog(20160920, "%u %u\n", m_violateFenceCount, m_totalFenceCount);
        printf("message size %u, delta T %lf\n", m_messageSize, m_deltaT);
    }
  }
#endif




  if(mpRDMAHeader.GetSequenceNumber() < expectedSeq)
  {
    ReTxTag retxTag;
    p->PeekPacketTag(retxTag);

    if(!retxTag.isReTx)
    {
        return;
    }
  }

  if (!m_rxBuffer->Add (p, mpRDMAHeader)) 
    { // Insert failed: No data or RX buffer full
      SendDataAckPacket(p, mpRDMAHeader); 
      return;
    } 

  // Notify app to receive if necessary
  if (expectedSeq < m_rxBuffer->NextRxSequence ())
    { // NextRxSeq advanced, we have something to send to the app
      if (!m_shutdownRecv)
        {
          NotifyDataRecv ();
        }
      // Handle exceptions
      if (m_closeNotified)
        {
          NS_LOG_WARN ("Why TCP " << this << " got data after close notification?");
        }
      
      SendDataAckPacket(p, mpRDMAHeader); 
      // If we received FIN before and now completed all "holes" in rx buffer,
      // invoke peer close procedure
      if (m_rxBuffer->Finished () && (mpRDMAHeader.GetFlags () & MpRDMAHeader::FIN) == 0)
        {
          DoPeerClose ();
        }
    }
  else
  { 
      SendDataAckPacket(p, mpRDMAHeader);
  }
  
    /* ========================== for out of order recording ============================== */
    //if(m_logOOO)
    //{
    //    char file_name[1024];
    //    sprintf(file_name, "Node_%u_OOO.txt", GetNode()->GetId());
    //    LogManager::RegisterLog(9160 + GetNode()->GetId(), file_name);

    //    m_logOOO = false;  
    //}

    //uint32_t ooo_size = 0;
    //if(m_rxBuffer->MaxSeqInBuf().GetValue() >= m_rxBuffer->NextRxSequence().GetValue())
    //{
    //    ooo_size = m_rxBuffer->MaxSeqInBuf().GetValue() - m_rxBuffer->NextRxSequence().GetValue();
    //    if(mpRDMAHeader.GetDestinationPort() <= 1008)
    //    {
    //        LogManager::WriteLog(9160 + GetNode()->GetId(), "%u\n", ooo_size);
    //    }
    //}

    /* ========================== END out of order recording ============================== */

}

/**
 * \brief Estimate the RTT
 *
 * Called by ForwardUp() to estimate RTT.
 *
 * \param mpRDMAHeader MP-RDMA header for the incoming packet
 */
/* Yuanwei_Note: because in MP-RDMA, ACK sequence is the same as the original data packet, 
 * so we should modify this function accordingly 
 */
void
MpRDMASocketImpl::EstimateRtt (const MpRDMAHeader& mpRDMAHeader)
{
  SequenceNumber32 ackSeq = mpRDMAHeader.GetAckNumber ();
  Time m = Time (0.0);

  // An ack has been received, calculate rtt and log this measurement
  // Note we use a linear search (O(n)) for this since for the common
  // case the ack'ed packet will be at the head of the list
  // Yuanwei: in MP-RDMA, this is not the case, because packets are most likely out-of-order arrival 
  if (!m_history.empty ())
    {
      RttHistory& h = m_history.front();
      
      //std::deque<RttHistory>::iterator it;  
      //for(it = m_history.begin(); it != m_history.end(); it++)
      //{
      //    if(it->seq == ackSeq)
      //    {
      //        h = *it; 
      //        break; 
      //    } 
      //}
      //if(it == m_history.end())
      //{
      //    printf("not found ackSeq in RTT history\n"); 
      //    return; 
      //}

      if (!h.retx && ackSeq >= (h.seq + SequenceNumber32 (h.count)))
        { // Ok to use this sample
          if (m_timestampEnabled && mpRDMAHeader.HasOption (TcpOption::TS))
            {
              Ptr<TcpOptionTS> ts;
              ts = DynamicCast<TcpOptionTS> (mpRDMAHeader.GetOption (TcpOption::TS));
              m = TcpOptionTS::ElapsedTimeFromTsValue (ts->GetEcho ());
            }
          else
            {
              m = Simulator::Now () - h.time; // Elapsed time
            }
        }
    }

  // Now delete all ack history with seq <= ack
  while (!m_history.empty ())
    {
      RttHistory& h = m_history.front ();
      if ((h.seq + SequenceNumber32 (h.count)) > ackSeq)
        {
          break;                                                              // Done removing
        }
      m_history.pop_front (); // Remove
    }

  if (!m.IsZero ())
    {
      m_rtt->Measurement (m);                // Log the measurement
      // RFC 6298, clause 2.4
      m_rto = Max (m_rtt->GetEstimate () + Max (m_clockGranularity, m_rtt->GetVariation () * 4), m_minRto);
      m_lastRtt = m_rtt->GetEstimate ();
      NS_LOG_FUNCTION (this << m_lastRtt);
    }
}

// Called by the ReceivedAck() when new ACK received and by ProcessSynRcvd()
// when the three-way handshake completed. This cancels retransmission timer
// and advances Tx window
void
MpRDMASocketImpl::NewAck (SequenceNumber32 const& ack, bool resetRTO)
{
  NS_LOG_FUNCTION (this << ack);

  if (m_state != SYN_RCVD && resetRTO)
    { // Set RTO unless the ACK is received in SYN_RCVD state
      NS_LOG_LOGIC (this << " Cancelled ReTxTimeout event which was set to expire at " <<
                    (Simulator::Now () + Simulator::GetDelayLeft (m_retxEvent)).GetSeconds ());
      m_retxEvent.Cancel ();
      // On receiving a "New" ack we restart retransmission timer .. RFC 6298
      // RFC 6298, clause 2.4
      m_rto = Max (m_rtt->GetEstimate () + Max (m_clockGranularity, m_rtt->GetVariation () * 4), m_minRto);

      NS_LOG_LOGIC (this << " Schedule ReTxTimeout at time " <<
                    Simulator::Now ().GetSeconds () << " to expire at time " <<
                    (Simulator::Now () + m_rto.Get ()).GetSeconds ());
      m_retxEvent = Simulator::Schedule (m_rto, &MpRDMASocketImpl::ReTxTimeout, this);
    }

  // Note the highest ACK and tell app to send more
  NS_LOG_LOGIC ("TCP " << this << " NewAck " << ack <<
                " numberAck " << (ack - m_txBuffer->HeadSequence ())); // Number bytes ack'ed
  m_txBuffer->DiscardUpTo (ack);
  if (GetTxAvailable () > 0)
    {
      NotifySend (GetTxAvailable ());
    }
  if (ack > m_nextTxSequence)
    {
      m_nextTxSequence = ack; // If advanced
    }
  if (m_txBuffer->Size () == 0 && m_state != FIN_WAIT_1 && m_state != CLOSING)
    { // No retransmit timer if no data to retransmit
      NS_LOG_LOGIC (this << " Cancelled ReTxTimeout event which was set to expire at " <<
                    (Simulator::Now () + Simulator::GetDelayLeft (m_retxEvent)).GetSeconds ());
      m_retxEvent.Cancel ();
    }
}

// Retransmit timeout
void
MpRDMASocketImpl::ReTxTimeout ()
{
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC (this << " ReTxTimeout Expired at time " << Simulator::Now ().GetSeconds ());
  // If erroneous timeout in closed/timed-wait state, just return
  if (m_state == CLOSED || m_state == TIME_WAIT)
    {
      return;
    }
  // If all data are received (non-closing socket and nothing to send), just return
  if (m_state <= ESTABLISHED && m_txBuffer->HeadSequence () >= m_highTxMark)
    {
      return;
    }

  m_recover = m_highTxMark;
  Retransmit ();
}

void
MpRDMASocketImpl::DelAckTimeout (void)
{
  m_delAckCount = 0;
  SendEmptyPacket (MpRDMAHeader::ACK);
}

void
MpRDMASocketImpl::LastAckTimeout (void)
{
  NS_LOG_FUNCTION (this);

  m_lastAckEvent.Cancel ();
  if (m_state == LAST_ACK)
    {
      CloseAndNotify ();
    }
  if (!m_closeNotified)
    {
      m_closeNotified = true;
    }
}

// Send 1-byte data to probe for the window size at the receiver when
// the local knowledge tells that the receiver has zero window size
// C.f.: RFC793 p.42, RFC1112 sec.4.2.2.17
void
MpRDMASocketImpl::PersistTimeout ()
{
  NS_LOG_LOGIC ("PersistTimeout expired at " << Simulator::Now ().GetSeconds ());
  m_persistTimeout = std::min (Seconds (60), Time (2 * m_persistTimeout)); // max persist timeout = 60s
  Ptr<Packet> p = m_txBuffer->CopyFromSequence (1, m_nextTxSequence);
  MpRDMAHeader mpRDMAHeader; 
  mpRDMAHeader.SetSequenceNumber (m_nextTxSequence);
  mpRDMAHeader.SetAckNumber (m_rxBuffer->NextRxSequence ());
  mpRDMAHeader.SetWindowSize (AdvertisedWindowSize ());
  if (m_endPoint != 0)
    {
      mpRDMAHeader.SetSourcePort (m_endPoint->GetLocalPort ());
      mpRDMAHeader.SetDestinationPort (m_endPoint->GetPeerPort ());
    }
  else
    {
      mpRDMAHeader.SetSourcePort (m_endPoint6->GetLocalPort ());
      mpRDMAHeader.SetDestinationPort (m_endPoint6->GetPeerPort ());
    }
  AddOptions (mpRDMAHeader);

  m_txTrace (p, mpRDMAHeader, this);

  if (m_endPoint != 0)
    {
      m_mpRDMA->SendPacket (p, mpRDMAHeader, m_endPoint->GetLocalAddress (),
                         m_endPoint->GetPeerAddress (), m_boundnetdevice);
    }
  else
    {
      m_mpRDMA->SendPacket (p, mpRDMAHeader, m_endPoint6->GetLocalAddress (),
                         m_endPoint6->GetPeerAddress (), m_boundnetdevice);
    }

  NS_LOG_LOGIC ("Schedule persist timeout at time "
                << Simulator::Now ().GetSeconds () << " to expire at time "
                << (Simulator::Now () + m_persistTimeout).GetSeconds ());
  m_persistEvent = Simulator::Schedule (m_persistTimeout, &MpRDMASocketImpl::PersistTimeout, this);
}

void
MpRDMASocketImpl::Retransmit ()
{
  // If erroneous timeout in closed/timed-wait state, just return
  if (m_state == CLOSED || m_state == TIME_WAIT)
    {
      return;
    }
  // If all data are received (non-closing socket and nothing to send), just return
  if (m_state <= ESTABLISHED && m_txBuffer->HeadSequence () >= m_highTxMark)
    {
      return;
    }

  /*
   * When a TCP sender detects segment loss using the retransmission timer
   * and the given segment has not yet been resent by way of the
   * retransmission timer, the value of ssthresh MUST be set to no more
   * than the value given in equation (4):
   *
   *   ssthresh = max (FlightSize / 2, 2*SMSS)            (4)
   *
   * where, as discussed above, FlightSize is the amount of outstanding
   * data in the network.
   *
   * On the other hand, when a TCP sender detects segment loss using the
   * retransmission timer and the given segment has already been
   * retransmitted by way of the retransmission timer at least once, the
   * value of ssthresh is held constant.
   *
   * Conditions to decrement slow - start threshold are as follows:
   *
   * *) The TCP state should be less than disorder, which is nothing but open.
   * If we are entering into the loss state from the open state, we have not yet
   * reduced the slow - start threshold for the window of data. (Nat: Recovery?)
   * *) If we have entered the loss state with all the data pointed to by high_seq
   * acknowledged. Once again it means that in whatever state we are (other than
   * open state), all the data from the window that got us into the state, prior to
   * retransmission timer expiry, has been acknowledged. (Nat: How this can happen?)
   * *) If the above two conditions fail, we still have one more condition that can
   * demand reducing the slow - start threshold: If we are already in the loss state
   * and have not yet retransmitted anything. The condition may arise in case we
   * are not able to retransmit anything because of local congestion.
   */

  //if (m_tcb->m_congState != MpRDMASocketState::CA_LOSS)
  if (m_tcb->m_congState != TcpSocketState::CA_LOSS) 
    {
      m_tcb->m_congState = MpRDMASocketState::CA_LOSS;
      m_tcb->m_ssThresh = m_congestionControl->GetSsThresh (m_tcb, BytesInFlight ());
      m_tcb->m_cWnd = m_tcb->m_segmentSize;
    }

  m_nextTxSequence = m_txBuffer->HeadSequence (); // Restart from highest Ack
  //LYJ debug
  //printf("in retransmit :update snd.nxt to %u\n",m_nextTxSequence.Get().GetValue());
  m_dupAckCount = 0;

  NS_LOG_DEBUG ("RTO. Reset cwnd to " <<  m_tcb->m_cWnd << ", ssthresh to " <<
                m_tcb->m_ssThresh << ", restart from seqnum " << m_nextTxSequence);
  DoRetransmit ();                          // Retransmit the packet
}

void
MpRDMASocketImpl::DoRetransmit ()
{
  NS_LOG_FUNCTION (this);
  // Retransmit SYN packet
  if (m_state == SYN_SENT)
    {
      if (m_synCount > 0)
        {
          SendEmptyPacket (MpRDMAHeader::SYN);
        }
      else
        {
          NotifyConnectionFailed ();
        }
      return;
    }

  if (m_dataRetrCount == 0)
    {
      NS_LOG_INFO ("No more data retries available. Dropping connection");
      NotifyErrorClose ();
      DeallocateEndPoint ();
      return;
    }
  else
    {
      --m_dataRetrCount;
    }

  // Retransmit non-data packet: Only if in FIN_WAIT_1 or CLOSING state
  if (m_txBuffer->Size () == 0)
    {
      if (m_state == FIN_WAIT_1 || m_state == CLOSING)
        { // Must have lost FIN, re-send
          SendEmptyPacket (MpRDMAHeader::FIN);
        }
      return;
    }

  // Retransmit a data packet: Call SendDataPacket
  //lyj debug
          // if(m_tcb->m_segmentSize == 0){
          //           printf("%s %d\n",__FUNCTION__,__LINE__);
          //         }
  uint32_t sz = SendDataPacket (m_txBuffer->HeadSequence (), m_tcb->m_segmentSize, false, m_maxPathId);
  ++m_retransOut;

  // In case of RTO, advance m_nextTxSequence
  m_nextTxSequence = std::max (m_nextTxSequence.Get (), m_txBuffer->HeadSequence () + sz);

  NS_LOG_DEBUG ("retxing seq " << m_txBuffer->HeadSequence ());
}

void
MpRDMASocketImpl::CancelAllTimers ()
{
  m_retxEvent.Cancel ();
  m_persistEvent.Cancel ();
  m_delAckEvent.Cancel ();
  m_lastAckEvent.Cancel ();
  m_timewaitEvent.Cancel ();
  m_sendPendingDataEvent.Cancel ();
}

/* Move TCP to Time_Wait state and schedule a transition to Closed state */
void
MpRDMASocketImpl::TimeWait ()
{
  NS_LOG_DEBUG (MpRDMAStateName[m_state] << " -> TIME_WAIT");
  m_state = TIME_WAIT;
  CancelAllTimers ();
  // Move from TIME_WAIT to CLOSED after 2*MSL. Max segment lifetime is 2 min
  // according to RFC793, p.28
  m_timewaitEvent = Simulator::Schedule (Seconds (2 * m_msl),
                                         &MpRDMASocketImpl::CloseAndNotify, this);
}

/* Below are the attribute get/set functions */

void
MpRDMASocketImpl::SetSndBufSize (uint32_t size)
{
  NS_LOG_FUNCTION (this << size);
  m_txBuffer->SetMaxBufferSize (size);
}

uint32_t
MpRDMASocketImpl::GetSndBufSize (void) const
{
  return m_txBuffer->MaxBufferSize ();
}

void
MpRDMASocketImpl::SetRcvBufSize (uint32_t size)
{
  NS_LOG_FUNCTION (this << size);
  uint32_t oldSize = GetRcvBufSize ();

  m_rxBuffer->SetMaxBufferSize (size);

  /* The size has (manually) increased. Actively inform the other end to prevent
   * stale zero-window states.
   */
  if (oldSize < size && m_connected)
    {
      SendEmptyPacket (MpRDMAHeader::ACK);
    }
}

uint32_t
MpRDMASocketImpl::GetRcvBufSize (void) const
{
  return m_rxBuffer->MaxBufferSize ();
}

void
MpRDMASocketImpl::SetSegSize (uint32_t size)
{
  NS_LOG_FUNCTION (this << size);
  m_tcb->m_segmentSize = size;

  NS_ABORT_MSG_UNLESS (m_state == CLOSED, "Cannot change segment size dynamically.");
}

uint32_t
MpRDMASocketImpl::GetSegSize (void) const
{
  return m_tcb->m_segmentSize;
}

void
MpRDMASocketImpl::SetConnTimeout (Time timeout)
{
  NS_LOG_FUNCTION (this << timeout);
  m_cnTimeout = timeout;
}

Time
MpRDMASocketImpl::GetConnTimeout (void) const
{
  return m_cnTimeout;
}

void
MpRDMASocketImpl::SetSynRetries (uint32_t count)
{
  NS_LOG_FUNCTION (this << count);
  m_synRetries = count;
}

uint32_t
MpRDMASocketImpl::GetSynRetries (void) const
{
  return m_synRetries;
}

void
MpRDMASocketImpl::SetDataRetries (uint32_t retries)
{
  NS_LOG_FUNCTION (this << retries);
  m_dataRetries = retries;
}

uint32_t
MpRDMASocketImpl::GetDataRetries (void) const
{
  NS_LOG_FUNCTION (this);
  return m_dataRetries;
}

void
MpRDMASocketImpl::SetDelAckTimeout (Time timeout)
{
  NS_LOG_FUNCTION (this << timeout);
  m_delAckTimeout = timeout;
}

Time
MpRDMASocketImpl::GetDelAckTimeout (void) const
{
  return m_delAckTimeout;
}

void
MpRDMASocketImpl::SetDelAckMaxCount (uint32_t count)
{
  NS_LOG_FUNCTION (this << count);
  m_delAckMaxCount = count;
}

uint32_t
MpRDMASocketImpl::GetDelAckMaxCount (void) const
{
  return m_delAckMaxCount;
}

void
MpRDMASocketImpl::SetMpRDMANoDelay (bool noDelay)
{
  NS_LOG_FUNCTION (this << noDelay);
  m_noDelay = noDelay;
}

bool
MpRDMASocketImpl::GetMpRDMANoDelay (void) const
{
  return m_noDelay;
}

void
MpRDMASocketImpl::SetPersistTimeout (Time timeout)
{
  NS_LOG_FUNCTION (this << timeout);
  m_persistTimeout = timeout;
}

Time
MpRDMASocketImpl::GetPersistTimeout (void) const
{
  return m_persistTimeout;
}

bool
MpRDMASocketImpl::SetAllowBroadcast (bool allowBroadcast)
{
  // Broadcast is not implemented. Return true only if allowBroadcast==false
  return (!allowBroadcast);
}

bool
MpRDMASocketImpl::GetAllowBroadcast (void) const
{
  return false;
}

void
MpRDMASocketImpl::AddOptions (MpRDMAHeader& header)
{
  NS_LOG_FUNCTION (this << header);

  // The window scaling option is set only on SYN packets
  if (m_winScalingEnabled && (header.GetFlags () & MpRDMAHeader::SYN))
    {
      AddOptionWScale (header);
    }

  if (m_timestampEnabled)
    {
      AddOptionTimestamp (header);
    }
}

void
MpRDMASocketImpl::ProcessOptionWScale (const Ptr<const TcpOption> option)
{
  NS_LOG_FUNCTION (this << option);

  Ptr<const TcpOptionWinScale> ws = DynamicCast<const TcpOptionWinScale> (option);

  // In naming, we do the contrary of RFC 1323. The received scaling factor
  // is Rcv.Wind.Scale (and not Snd.Wind.Scale)
  m_sndWindShift = ws->GetScale ();

  if (m_sndWindShift > 14)
    {
      NS_LOG_WARN ("Possible error; m_sndWindShift exceeds 14: " << m_sndWindShift);
      m_sndWindShift = 14;
    }

  NS_LOG_INFO (m_node->GetId () << " Received a scale factor of " <<
               static_cast<int> (m_sndWindShift));
}

uint8_t
MpRDMASocketImpl::CalculateWScale () const
{
  NS_LOG_FUNCTION (this);
  uint32_t maxSpace = m_rxBuffer->MaxBufferSize ();
  uint8_t scale = 0;

  while (maxSpace > m_maxWinSize)
    {
      maxSpace = maxSpace >> 1;
      ++scale;
    }

  if (scale > 14)
    {
      NS_LOG_WARN ("Possible error; scale exceeds 14: " << scale);
      scale = 14;
    }

  NS_LOG_INFO ("Node " << m_node->GetId () << " calculated wscale factor of " <<
               static_cast<int> (scale) << " for buffer size " << m_rxBuffer->MaxBufferSize ());
  return scale;
}

void
MpRDMASocketImpl::AddOptionWScale (MpRDMAHeader& header)
{
  NS_LOG_FUNCTION (this << header);
  NS_ASSERT (header.GetFlags () & MpRDMAHeader::SYN);

  Ptr<TcpOptionWinScale> option = CreateObject<TcpOptionWinScale> ();

  // In naming, we do the contrary of RFC 1323. The sended scaling factor
  // is Snd.Wind.Scale (and not Rcv.Wind.Scale)

  m_rcvWindShift = CalculateWScale ();
  option->SetScale (m_rcvWindShift);

  header.AppendOption (option);

  NS_LOG_INFO (m_node->GetId () << " Send a scaling factor of " <<
               static_cast<int> (m_rcvWindShift));
}

void
MpRDMASocketImpl::ProcessOptionTimestamp (const Ptr<const TcpOption> option,
                                       const SequenceNumber32 &seq)
{
  NS_LOG_FUNCTION (this << option);

  Ptr<const TcpOptionTS> ts = DynamicCast<const TcpOptionTS> (option);

  if (seq == m_rxBuffer->NextRxSequence () && seq <= m_highTxAck)
    {
      m_timestampToEcho = ts->GetTimestamp ();
    }

  NS_LOG_INFO (m_node->GetId () << " Got timestamp=" <<
               m_timestampToEcho << " and Echo="     << ts->GetEcho ());
}

void
MpRDMASocketImpl::AddOptionTimestamp (MpRDMAHeader& header)
{
  NS_LOG_FUNCTION (this << header);

  Ptr<TcpOptionTS> option = CreateObject<TcpOptionTS> ();

  option->SetTimestamp (TcpOptionTS::NowToTsValue ());
  option->SetEcho (m_timestampToEcho);

  header.AppendOption (option);
  NS_LOG_INFO (m_node->GetId () << " Add option TS, ts=" <<
               option->GetTimestamp () << " echo=" << m_timestampToEcho);
}

//Yuanwei_note: how to update rWnd according to ACK window 
void MpRDMASocketImpl::UpdateWindowSize (const MpRDMAHeader& header) 
{
  NS_LOG_FUNCTION (this << header);
  //  If the connection is not established, the window size is always
  //  updated
  uint32_t receivedWindow = header.GetWindowSize ();
  receivedWindow <<= m_sndWindShift;
  NS_LOG_INFO ("Received (scaled) window is " << receivedWindow << " bytes");
  if (m_state < ESTABLISHED)
    {
      m_rWnd = receivedWindow;
      NS_LOG_LOGIC ("State less than ESTABLISHED; updating rWnd to " << m_rWnd);
      return;
    }

  m_rWnd = receivedWindow;
  NS_LOG_LOGIC ("updating rWnd to " << m_rWnd);
}

void
MpRDMASocketImpl::SetMinRto (Time minRto)
{
  NS_LOG_FUNCTION (this << minRto);
  m_minRto = minRto;
}

Time
MpRDMASocketImpl::GetMinRto (void) const
{
  return m_minRto;
}

void
MpRDMASocketImpl::SetClockGranularity (Time clockGranularity)
{
  NS_LOG_FUNCTION (this << clockGranularity);
  m_clockGranularity = clockGranularity;
}

Time
MpRDMASocketImpl::GetClockGranularity (void) const
{
  return m_clockGranularity;
}

Ptr<TcpTxBuffer>
MpRDMASocketImpl::GetTxBuffer (void) const
{
  return m_txBuffer;
}

Ptr<TcpRxBuffer>
MpRDMASocketImpl::GetRxBuffer (void) const
{
  return m_rxBuffer;
}

void
MpRDMASocketImpl::UpdateCwnd (uint32_t oldValue, uint32_t newValue)
{
  m_cWndTrace (oldValue, newValue);
}

void
MpRDMASocketImpl::UpdateSsThresh (uint32_t oldValue, uint32_t newValue)
{
  m_ssThTrace (oldValue, newValue);
}

void
MpRDMASocketImpl::SetCongestionControlAlgorithm (Ptr<TcpCongestionOps> algo) 
{
  NS_LOG_FUNCTION (this << algo);
  m_congestionControl = algo;
}

Ptr<MpRDMASocketImpl>
MpRDMASocketImpl::Fork (void)
{
  return CopyObject<MpRDMASocketImpl> (this);
}

uint32_t
MpRDMASocketImpl::SafeSubtraction (uint32_t a, uint32_t b)
{
  if (a > b)
    {
      return a-b;
    }

  return 0;
}

/* Yuanwei Note: Send a data ACK packet */ 
void
MpRDMASocketImpl::SendDataAckPacket (Ptr<Packet> packet, const MpRDMAHeader& mpRDMAHeader) 
{
  NS_LOG_FUNCTION (this);

  Ptr<Packet> p = Create<Packet> ();
  MpRDMAHeader header; 
  SequenceNumber32 ackSequenceNumber = mpRDMAHeader.GetSequenceNumber ();

  /*
   * Add tags for each socket option.
   * Note that currently the socket adds both IPv4 tag and IPv6 tag
   * if both options are set. Once the packet got to layer three, only
   * the corresponding tags will be read.
   */
  if (IsManualIpTos ())
    {
      SocketIpTosTag ipTosTag;
      ipTosTag.SetTos (GetIpTos ());
      p->AddPacketTag (ipTosTag);
    }

  if (IsManualIpv6Tclass ())
    {
      SocketIpv6TclassTag ipTclassTag;
      ipTclassTag.SetTclass (GetIpv6Tclass ());
      p->AddPacketTag (ipTclassTag);
    }

  if (IsManualIpTtl ())
    {
      SocketIpTtlTag ipTtlTag;
      ipTtlTag.SetTtl (GetIpTtl ());
      p->AddPacketTag (ipTtlTag);
    }

  if (IsManualIpv6HopLimit ())
    {
      SocketIpv6HopLimitTag ipHopLimitTag;
      ipHopLimitTag.SetHopLimit (GetIpv6HopLimit ());
      p->AddPacketTag (ipHopLimitTag);
    }

  if (m_endPoint == 0 && m_endPoint6 == 0)
    {
      NS_LOG_WARN ("Failed to send empty packet due to null endpoint");
      return;
    }

  header.SetFlags (MpRDMAHeader::ACK);
  header.SetAckNumber (ackSequenceNumber); 
  if (m_endPoint != 0)
    {
      header.SetSourcePort (m_endPoint->GetLocalPort ());
      header.SetDestinationPort (m_endPoint->GetPeerPort ());
    }
  else
    {
      header.SetSourcePort (m_endPoint6->GetLocalPort ());
      header.SetDestinationPort (m_endPoint6->GetPeerPort ());
    }
  AddOptions (header);
  header.SetWindowSize (AdvertisedWindowSize ());

  PathIdTag pathTag; 
  packet->PeekPacketTag(pathTag);
  EcnTag ecnTag;
  packet->PeekPacketTag(ecnTag);

  TimeStampTag timeTag; 
  packet->PeekPacketTag(timeTag);

  /* set rWndTS */
  timeTag.rWndTS = Simulator::Now().GetSeconds(); 

  //printf("at node %u, now %lf, tag %lf, half-RTT %lf\n", 
  //        GetNode()->GetId(), timeTag.rWndTS, timeTag.timeStamp, timeTag.rWndTS - timeTag.timeStamp); 

  /* add accumulative ACK tag */ 
  AAckTag aackTag; 
  aackTag.aackSeq = m_rxBuffer->NextRxSequence ().GetValue(); 
  aackTag.maxSeq = m_rxBuffer->NextRxSequence ().GetValue() + m_rcvL * m_tcb->m_segmentSize;  /* sender should not send over this seq */
  aackTag.pathId = pathTag.pid;

  SequenceNumber32 expectedSeq = m_rxBuffer->NextRxSequence ();
  if(expectedSeq + m_rcvL * m_tcb->m_segmentSize <= mpRDMAHeader.GetSequenceNumber())  /* packet is out of range */
  {
      aackTag.nack = 1;
  }
  else
  {
      aackTag.nack = 0;
  }
  //printf("at node %u, maxSeq to ACK %u, m_rcvL %u, m_sndL %u, fastAlhpa %lf\n", GetNode()->GetId(), aackTag.maxSeq, m_rcvL, m_sndL, m_fastAlpha);


#if (SINGLE_PATH_ACK == 1)
  pathTag.pid = GetNode()->GetId(); /* ensure ACK goes single-path */
#endif 

  ReTxTag retxTag;
  packet->PeekPacketTag(retxTag);

  p->AddPacketTag(timeTag); 
  p->AddPacketTag(pathTag); 
  p->AddPacketTag(ecnTag); 
  p->AddPacketTag(aackTag); 
  p->AddPacketTag(retxTag);


#if (TEST_DELTA_T == 1)
  FenceTag fenceTag;
  p->PeekPacketTag(fenceTag);

  if(fenceTag.fence == 1)
  {
      p->AddPacketTag(fenceTag);
  }
#endif

  m_txTrace (p, header, this);

  if (m_endPoint != 0)
    {
      m_mpRDMA->SendPacket (p, header, m_endPoint->GetLocalAddress (),
                         m_endPoint->GetPeerAddress (), m_boundnetdevice);
    }
  else
    {
      m_mpRDMA->SendPacket (p, header, m_endPoint6->GetLocalAddress (),
                         m_endPoint6->GetPeerAddress (), m_boundnetdevice);
    }

  if (m_highTxAck < header.GetAckNumber ())
    {
      m_highTxAck = header.GetAckNumber ();
    }
}

void MpRDMASocketImpl::LogECNRatio()
{
    uint32_t count = 0;

    for(uint32_t i = 0; i < m_pathNum; i++) 
    {
        count += normal_ACK[i]; 
    }

    for(uint32_t i = 0; i < m_pathNum; i++) 
    {
        LogManager::WriteLog(2000 + GetNode()->GetId(), "%lf ", 1.0 * normal_ACK[i] / count);
//wsq
     //   printf("%lf ", 1.0 * normal_ACK[i] / count); 
    }

    LogManager::WriteLog(2000 + GetNode()->GetId(), "\n");
//wsq
    //printf("time %lf\n", Simulator::Now().GetSeconds()); 
   
    Simulator::Schedule(Time::FromDouble(0.001, Time::S), &MpRDMASocketImpl::LogECNRatio, this); 
}

void
MpRDMASocketImpl::MacroTimeout ()  
{
  NS_LOG_FUNCTION (this);
//wsq
  //printf("at node %u, time %lf, Macro TimeOut happened, sndL %u, rcvL %u\n", GetNode()->GetId(), Simulator::Now().GetSeconds(), m_sndL, m_rcvL);
  
  //printf("dump the sack map, cWnd %u, pipe %u, highreTx %u, AACK %u, fastthresh %u\n", 
         // m_tcb->m_cWnd.Get(), m_pipe, m_highReTxMark.GetValue(), m_aackSeq, m_fastRecoveryThreshold); 

  for(std::map<SequenceNumber32, AckedBlock>::iterator it = m_seqAckedMap.begin(); it != m_seqAckedMap.end(); it++)
    {
//wsq
     //   printf("block %u received %u\n", it->first.GetValue(), it->second.acked); 
    }

  /* a Macro timeout fires, everything should go back to init*/ 
  NS_LOG_FUNCTION (this);
  NS_LOG_LOGIC (this << " MacroTimeout Expired at time " << Simulator::Now ().GetSeconds ());
  // If erroneous timeout in closed/timed-wait state, just return
  if (m_state == CLOSED || m_state == TIME_WAIT)
    {
      return;
    }
  // If all data are received (non-closing socket and nothing to send), just return
  if (m_state <= ESTABLISHED && m_txBuffer->HeadSequence () >= m_highTxMark)
    {
      return;
    }

  m_senderState = InitState; 
  m_tcb->m_cWnd = m_tcb->m_initialCWnd * m_tcb->m_segmentSize; 
  m_seqAckedMap.clear();
  m_nextTxSequence = m_txBuffer->HeadSequence (); 
   //LYJ debug
  //printf("in MARCOTIME :update snd.nxt to %u\n",m_nextTxSequence.Get().GetValue());
  m_pipe = 0;

  m_highReTxMark = SequenceNumber32(0); 

  //if (m_sendPendingDataEvent.IsRunning ())
  //{
  //    m_sendPendingDataEvent.Cancel(); 
  //}
  //else
  //{
  //    m_sendPendingDataEvent = Simulator::Schedule (TimeStep (1),
  //                                                  &MpRDMASocketImpl::SendPendingData,
  //                                                  this, m_connected);
  //}
//wsq
  //printf("at node %u, TimeOut() call MpRDMASend()\n", GetNode()->GetId());
  MpRDMASend(); 

}

bool MpRDMASocketImpl::ShouldReTx(SequenceNumber32 &seq) 
{
    if(m_highReTxMark >= m_txBuffer->HeadSequence() && m_seqAckedMap.find(m_highReTxMark) != m_seqAckedMap.end()) 
    {
        seq = m_highReTxMark + m_seqAckedMap[m_highReTxMark].packetSize; 
    }
    else
    {
        seq = m_txBuffer->HeadSequence(); 
    } 

    while(m_seqAckedMap.find(seq) != m_seqAckedMap.end() && seq < m_nextTxSequence.Get())
    {

        if(m_seqAckedMap[seq].acked == false && 
           m_tcb->m_cWnd >= m_pipe + m_seqAckedMap[seq].packetSize && 
           m_nextTxSequence.Get().GetValue() > seq.GetValue() + m_fastRecoveryThreshold) 
        {
            printf("cwnd is %u, pipe is %u, SND.NXT is %u, seq %u, fastthreshold %u, diff %u\n", 
                    m_tcb->m_cWnd.Get(), m_pipe, m_nextTxSequence.Get().GetValue(), seq.GetValue(), m_fastRecoveryThreshold, 
                    m_nextTxSequence.Get().GetValue() - seq.GetValue() - m_fastRecoveryThreshold); 
            printf("SRTT: %lu, DRTT: %lu, thresholdCal: %u\n", 
                    m_allPathRTT->GetEstimate().GetMicroSeconds(), 
                    m_allPathRTT->GetVariation().GetMicroSeconds(),  
                    uint32_t(m_fastAlpha * (m_allPathRTT->GetEstimate().GetMicroSeconds() + 3 * m_allPathRTT->GetVariation().GetMicroSeconds()) 
                           * m_tcb->m_cWnd.Get() / m_allPathRTT->GetEstimate().GetMicroSeconds())); 

            m_highReTxMark = seq; 
            return true; 
        }
        else
        {
            //printf("at node %u, packet size is %u, highreTx is %u, head is %u, seq is %u\n", 
            //        GetNode()->GetId(), m_seqAckedMap[seq].packetSize, 
            //        m_highReTxMark.GetValue(), m_txBuffer->HeadSequence().GetValue(), seq.GetValue()); 
            seq += m_seqAckedMap[seq].packetSize;
        }
    } 

    return false; 
}

bool MpRDMASocketImpl::ShouldTxNewData(SequenceNumber32 &seq)
{
      //if(m_endPoint->GetPeerPort () == 1435)
      //{
      //    printf("1435 txbuffer size %u\n", m_txBuffer->Size()); 
      //}
       
    if (m_txBuffer->Size () == 0)
    { 
      return false;                           // Nothing to send
    } 

    if (m_endPoint == 0 && m_endPoint6 == 0)
    {
      NS_LOG_INFO ("MpRDMASocketImpl::MpRDMASend(): No endpoint; m_shutdownSend=" << m_shutdownSend);
      return false; 
    } 

    if (m_tcb->m_cWnd.Get() < m_pipe + m_tcb->m_segmentSize && 
        m_txBuffer->SizeFromSequence (m_nextTxSequence) + m_pipe > m_tcb->m_cWnd.Get() && 
        m_pipe > 0) 
    {
      NS_LOG_LOGIC ("Preventing Silly Window Syndrome. Wait to send.");
      return false; 
    } 

    if (m_pipe > 0 && m_txBuffer->SizeFromSequence (m_nextTxSequence) < m_tcb->m_segmentSize)
    {
        NS_LOG_LOGIC ("Invoking Nagle's algorithm. Wait to send.");
        return false; 
    } 

    seq = m_nextTxSequence; 
    return true; 
}

void MpRDMASocketImpl::MpRDMASend()
{
    if(m_startRecordProbe)
    {
        char file_name[1024];
        sprintf(file_name, "Node_%u_probe_rate.txt", GetNode()->GetId());
        LogManager::RegisterLog(100000 + GetNode()->GetId(), file_name);

        //RecordProbeRate();
        m_startRecordProbe = false;
    }
    //if(Simulator::Now().GetSeconds() >= 0.15 - GetNode()->GetId() * 0.01)
    //{
    //    return; 
    //}


    //printf("at node %u, call mpRDMAsend\n", GetNode()->GetId());

    NS_LOG_FUNCTION(this);



    if (!(m_bSendPkt || m_senderState == InitState))
    {
        //printf("at node %u, only when m_bSendPkt or InitState can send()\n", GetNode()->GetId());
        return;
    }




    if (m_txBuffer->Size () == 0)
    {
//wsq
     // printf("node %u, no new data\n", GetNode()->GetId()); 
      return; 
    }

    uint32_t count = 0; 
    uint32_t pathId = 0; 

    uint32_t burst_control_max = 2;
    uint32_t burst_control_count = 0;

    uint32_t temp_pipe = m_nextTxSequence.Get().GetValue() - m_txBuffer->HeadSequence().GetValue();
    uint32_t temp_cWnd = m_tcb->m_cWnd.Get() + m_inflate;
    
    if (burst_control_max && temp_cWnd)
    {
        ;
    }

    //if(temp_cWnd > temp_pipe + 2 * m_tcb->m_segmentSize && m_maxTxNum == 1 && m_probe == false)
    //{
    //    m_maxTxNum = 2;
    //}

    while ((m_senderState != InitState && (int)m_tcb->m_cWnd.Get() + m_inflate >= (int)temp_pipe && burst_control_count < burst_control_max) || 
           (m_senderState == InitState && (int)m_tcb->m_cWnd.Get() + m_inflate >= (int)temp_pipe))
    //while ((m_senderState != InitState && m_tcb->m_cWnd.Get() + m_inflate > temp_pipe) || 
    //       (m_senderState == InitState && m_tcb->m_cWnd.Get() + m_inflate > temp_pipe))
    {



#if (TEST_DELTA_T == 1)
    if(m_wait)
    {
        return;
    }
#endif




        if (m_txBuffer->Size () == 0)
        {
            break;                           // Nothing to send
        }
 
        if (m_endPoint == 0 && m_endPoint6 == 0)
        {
              NS_LOG_INFO ("MpRDMASocketImpl::MpRDMASend(): No endpoint; m_shutdownSend=" << m_shutdownSend);
              break;
        }
 
        if ((int)(m_tcb->m_cWnd.Get()) + m_inflate < (int)temp_pipe + (int)m_tcb->m_segmentSize && 
            (int)(m_txBuffer->SizeFromSequence (m_nextTxSequence)) + (int)temp_pipe > (int)m_tcb->m_cWnd.Get() && 
            temp_pipe > 0)
        {
        //      printf("silly window, cWnd %u, inflate %d, pipe %u, next %u, size %u\n", 
        //              m_tcb->m_cWnd.Get(), m_inflate, temp_pipe, m_nextTxSequence.Get().GetValue(), m_txBuffer->SizeFromSequence (m_nextTxSequence));
              NS_LOG_LOGIC ("Preventing Silly Window Syndrome. Wait to send.");
              break;
        }
 
        if (temp_pipe > 0 && m_txBuffer->SizeFromSequence (m_nextTxSequence) < m_tcb->m_segmentSize)
        {
                //printf("Nagle\n");
                NS_LOG_LOGIC ("Invoking Nagle's algorithm. Wait to send.");
                break;
        }

#if (ENABLE_PROBING == 0)
        //if(m_maxPathId >= 1000)
        {
            m_probe = false;
        }
#endif
        //if(Simulator::Now().GetSeconds() >= 0.02 && Simulator::Now().GetSeconds() <= 0.03)
        //{
        //    m_probe = false;
        //}

        if((m_probe) || m_senderState == InitState)
        //if( (count >= m_maxTxNum) || m_senderState == InitState)
        {
//wsq
     //       printf("node %u, probe path %u, m_probe %u, sender_state %u, init_cwnd %u, cwnd %u, inflate %u, m_maxTxNum %u\n", 
                   // GetNode()->GetId(), m_maxPathId, m_probe, m_senderState, m_tcb->m_initialCWnd, m_tcb->m_cWnd.Get(), m_inflate, m_maxTxNum);

            pathId = m_maxPathId; 
            m_maxPathId++;
            m_probe = false;
            m_probingPackets ++;
        }
        else
        {
            pathId = m_lastAckPathId;
        }
        count++;
        m_totalSendPackets ++;

        SequenceNumber32 toTxSeq; 

        toTxSeq = m_nextTxSequence.Get();

        uint32_t sz;
        uint32_t diff = ((int)m_tcb->m_cWnd.Get() + m_inflate < (int)temp_pipe) ? 0 : ((int)m_tcb->m_cWnd.Get() + m_inflate - (int)temp_pipe);  
        uint32_t s = std::min (diff, m_tcb->m_segmentSize);  // Send no more than window 
        //lyj debug
          // if(s == 0){
          //           printf("%s %d\n",__FUNCTION__,__LINE__);
          //         }
        sz = SendDataPacket(toTxSeq, s, false, pathId);
        
        burst_control_count++;

        //printf("node %u should send a new packet, seq is %u, size %u, cWnd %u, pipe %u, s %u, inflate %d, path %u, count %u, max %u\n",
        //        GetNode()->GetId(),  toTxSeq.GetValue(), sz, m_tcb->m_cWnd.Get(), temp_pipe, s, m_inflate, pathId, burst_control_count, burst_control_max);

        m_nextTxSequence += sz;
        
        temp_pipe = m_nextTxSequence.Get().GetValue() - m_txBuffer->HeadSequence().GetValue();





#if (TEST_DELTA_T == 1)
        //if(Simulator::Now().GetMicroSeconds() > 1000 && m_endPoint->GetPeerPort () == 1001)
        if(Simulator::Now().GetMicroSeconds() > 1000)
        {
            //printf("at node %u, peer port %u, m_nextTx %u, %u\n", 
            //        GetNode()->GetId(), m_endPoint->GetPeerPort(), m_nextTxSequence.Get().GetValue(),
            //        (m_nextTxSequence.Get().GetValue() - 1) / m_tcb->m_segmentSize);

            //if((m_nextTxSequence.Get().GetValue() - 1) / m_tcb->m_segmentSize % m_messageSize == 0)
            if(m_pktTotalQuota >= m_messageSize)
            {
                m_pktTotalQuota -= m_messageSize;

                m_wait = true;

                double wait_time = m_deltaT * (1.0 * m_sndL * m_tcb->m_segmentSize / (m_tcb->m_cWnd.Get() / m_allPathRTT->GetEstimate().GetSeconds()));
                printf("at time %lf, start wait for %lf\n", Simulator::Now().GetSeconds(), wait_time);

                Simulator::Schedule(Seconds(wait_time), &MpRDMASocketImpl::StopWaiting, this);
            }
        }
#endif




        //printf("at node %u, mpRDMASend available window %d, nexTx %u, head %u\n", 
        //        GetNode()->GetId(), m_tcb->m_cWnd.Get() + m_inflate - temp_pipe, m_nextTxSequence.Get().GetValue(), m_txBuffer->HeadSequence().GetValue());
    }


    m_bSendPkt = false;
}

void MpRDMASocketImpl::MpRDMAreTx(uint32_t pathId)
{
    NS_LOG_FUNCTION(this);

    SequenceNumber32 toTxSeq; 

    toTxSeq = m_highReTxMark;
    m_highReTxMark += m_tcb->m_segmentSize;
    //lyj debug
    //printf("at mprdmaretx is %u\n",m_highReTxMark.GetValue());
    
    m_reTxSuspCount++;  
    //printf("node %u should send a reTX packet %u, head is %u, ooP is %u, ooL is %u, m_rcvL %u, m_sndL is %u, reTxMark is %u, nxtTx %u, inflate %d, cWnd %u, pipe %u\n", 
    //        GetNode()->GetId(), m_reTxSuspCount, m_txBuffer->HeadSequence().GetValue(), m_ooP.GetValue(), m_ooL.GetValue(), m_rcvL, m_sndL, m_highReTxMark.GetValue(), 
    //        m_nextTxSequence.Get().GetValue(), m_inflate, m_tcb->m_cWnd.Get(), m_nextTxSequence.Get().GetValue() - m_txBuffer->HeadSequence().GetValue());

    //if(pathId % 4 == 0)
    //{
    //    pathId++;
    //}
    //lyj debug
          // if(m_seqAckedMap[toTxSeq].packetSize == 0){
          //           printf("%s %d\n",__FUNCTION__,__LINE__);
          //         }
    SendDataPacket(toTxSeq, m_seqAckedMap[toTxSeq].packetSize, true, pathId);

    m_senderState = CA;
}

void MpRDMASocketImpl::StopWaiting()
{
    printf("at time %lf, stop wait\n", Simulator::Now().GetSeconds());
    m_markFence = true;
        
    SequenceNumber32 toTxSeq; 
    toTxSeq = m_nextTxSequence.Get();

    uint32_t sz;
    //lyj debug
          // if(m_tcb->m_segmentSize == 0){
          //           printf("%s %d\n",__FUNCTION__,__LINE__);
          //         }
    sz = SendDataPacket(toTxSeq, m_tcb->m_segmentSize, false, m_lastAckPathId);

    m_nextTxSequence += sz;
    
    m_wait = false;
}

void MpRDMASocketImpl::RecordProbeRate()
{
    double probe_rate = m_probingPackets * 1.0 / m_totalSendPackets;
    LogManager::WriteLog(100000 + GetNode()->GetId(), "%lu, %lf\n", Simulator::Now().GetMicroSeconds() / 1000, probe_rate); //in ms

    m_totalSendPackets = 0;
    m_probingPackets = 0;

    Simulator::Schedule(Time::FromDouble(0.001, Time::S), &MpRDMASocketImpl::RecordProbeRate, this);
}

} // namespace ns3
