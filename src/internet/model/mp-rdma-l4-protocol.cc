/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2007 Georgia Tech Research Corporation
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
 * Author: Raj Bhattacharjea <raj.b@gatech.edu>
 */
/* Created by Yuanwei Lu <ywlu@mail.ustc.edu.cn> 
 * 2016-04-10
 */ 

#include "ns3/assert.h"
#include "ns3/log.h"
#include "ns3/nstime.h"
#include "ns3/boolean.h"
#include "ns3/object-vector.h"

#include "ns3/packet.h"
#include "ns3/node.h"
#include "ns3/simulator.h"
#include "ns3/ipv4-route.h"
#include "ns3/ipv6-route.h"

#include "mp-rdma-l4-protocol.h"
#include "mp-rdma-header.h"
#include "ipv4-end-point-demux.h"
#include "ipv6-end-point-demux.h"
#include "ipv4-end-point.h"
#include "ipv6-end-point.h"
#include "ipv4-l3-protocol.h"
#include "ipv6-l3-protocol.h"
#include "ipv6-routing-protocol.h"
#include "mp-rdma-socket-factory-impl.h"
#include "mp-rdma-socket-impl.h"
#include "rtt-estimator.h"

#include <vector>
#include <sstream>
#include <iomanip>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("MpRDMAL4Protocol");

NS_OBJECT_ENSURE_REGISTERED (MpRDMAL4Protocol);

//MpRDMAL4Protocol stuff----------------------------------------------------------

#undef NS_LOG_APPEND_CONTEXT
#define NS_LOG_APPEND_CONTEXT                                   \
  if (m_node) { std::clog << " [node " << m_node->GetId () << "] "; }

/* see http://www.iana.org/assignments/protocol-numbers */
const uint8_t MpRDMAL4Protocol::PROT_NUMBER = 6;

TypeId
MpRDMAL4Protocol::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::MpRDMAL4Protocol")
    .SetParent<IpL4Protocol> ()
    .SetGroupName ("Internet")
    .AddConstructor<MpRDMAL4Protocol> ()
    .AddAttribute ("RttEstimatorType",
                   "Type of RttEstimator objects.",
                   TypeIdValue (RttMeanDeviation::GetTypeId ()),
                   MakeTypeIdAccessor (&MpRDMAL4Protocol::m_rttTypeId),
                   MakeTypeIdChecker ())
    .AddAttribute ("SocketType",
                   "Socket type of MpRDMA objects.",
                   TypeIdValue (TcpNewReno::GetTypeId ()),
                   MakeTypeIdAccessor (&MpRDMAL4Protocol::m_congestionTypeId),
                   MakeTypeIdChecker ())
    .AddAttribute ("SocketList", "The list of sockets associated to this protocol.",
                   ObjectVectorValue (),
                   MakeObjectVectorAccessor (&MpRDMAL4Protocol::m_sockets),
                   MakeObjectVectorChecker<MpRDMASocketImpl> ())
  ;
  return tid;
}

MpRDMAL4Protocol::MpRDMAL4Protocol ()
  : m_endPoints (new Ipv4EndPointDemux ()), m_endPoints6 (new Ipv6EndPointDemux ())
{
  NS_LOG_FUNCTION_NOARGS ();
  NS_LOG_LOGIC ("Made a MpRDMAL4Protocol " << this);
}

MpRDMAL4Protocol::~MpRDMAL4Protocol ()
{
  NS_LOG_FUNCTION (this);
}

void
MpRDMAL4Protocol::SetNode (Ptr<Node> node)
{
  NS_LOG_FUNCTION (this);
  m_node = node;
}

void
MpRDMAL4Protocol::NotifyNewAggregate ()
{
  NS_LOG_FUNCTION (this);
  Ptr<Node> node = this->GetObject<Node> ();
  Ptr<Ipv4> ipv4 = this->GetObject<Ipv4> ();
  Ptr<Ipv6> ipv6 = node->GetObject<Ipv6> ();

  if (m_node == 0)
    {
      if ((node != 0) && (ipv4 != 0 || ipv6 != 0))
        {
          this->SetNode (node);
          Ptr<MpRDMASocketFactoryImpl> mpRDMAFactory = CreateObject<MpRDMASocketFactoryImpl> ();
          mpRDMAFactory->SetMpRDMA (this);
          node->AggregateObject (mpRDMAFactory);
        }
    }

  // We set at least one of our 2 down targets to the IPv4/IPv6 send
  // functions.  Since these functions have different prototypes, we
  // need to keep track of whether we are connected to an IPv4 or
  // IPv6 lower layer and call the appropriate one.

  if (ipv4 != 0 && m_downTarget.IsNull ())
    {
      ipv4->Insert (this);
      this->SetDownTarget (MakeCallback (&Ipv4::Send, ipv4));
    }
  if (ipv6 != 0 && m_downTarget6.IsNull ())
    {
      ipv6->Insert (this);
      this->SetDownTarget6 (MakeCallback (&Ipv6::Send, ipv6));
    }
  IpL4Protocol::NotifyNewAggregate ();
}

int
MpRDMAL4Protocol::GetProtocolNumber (void) const
{
  return PROT_NUMBER;
}

void
MpRDMAL4Protocol::DoDispose (void)
{
  NS_LOG_FUNCTION (this);
  m_sockets.clear ();

  if (m_endPoints != 0)
    {
      delete m_endPoints;
      m_endPoints = 0;
    }

  if (m_endPoints6 != 0)
    {
      delete m_endPoints6;
      m_endPoints6 = 0;
    }

  m_node = 0;
  m_downTarget.Nullify ();
  m_downTarget6.Nullify ();
  IpL4Protocol::DoDispose ();
}

Ptr<Socket>
MpRDMAL4Protocol::CreateSocket (TypeId congestionTypeId)
{
  NS_LOG_FUNCTION (this << congestionTypeId.GetName ());
  ObjectFactory rttFactory;
  ObjectFactory congestionAlgorithmFactory;
  rttFactory.SetTypeId (m_rttTypeId);
  congestionAlgorithmFactory.SetTypeId (congestionTypeId);

  Ptr<RttEstimator> rtt = rttFactory.Create<RttEstimator> ();
  Ptr<MpRDMASocketImpl> socket = CreateObject<MpRDMASocketImpl> ();
  Ptr<TcpCongestionOps> algo = congestionAlgorithmFactory.Create<TcpCongestionOps> (); 

  socket->SetNode (m_node);
  socket->SetMpRDMA (this);
  socket->SetRtt (rtt);
  socket->SetCongestionControlAlgorithm (algo);

  m_sockets.push_back (socket);
  return socket;
}

Ptr<Socket>
MpRDMAL4Protocol::CreateSocket (void)
{
  return CreateSocket (m_congestionTypeId);
}

Ipv4EndPoint *
MpRDMAL4Protocol::Allocate (void)
{
  NS_LOG_FUNCTION_NOARGS ();
  return m_endPoints->Allocate ();
}

Ipv4EndPoint *
MpRDMAL4Protocol::Allocate (Ipv4Address address)
{
  NS_LOG_FUNCTION (this << address);
  return m_endPoints->Allocate (address);
}

Ipv4EndPoint *
MpRDMAL4Protocol::Allocate (uint16_t port)
{
  NS_LOG_FUNCTION (this << port);
  return m_endPoints->Allocate (port);
}

Ipv4EndPoint *
MpRDMAL4Protocol::Allocate (Ipv4Address address, uint16_t port)
{
  NS_LOG_FUNCTION (this << address << port);
  return m_endPoints->Allocate (address, port);
}

Ipv4EndPoint *
MpRDMAL4Protocol::Allocate (Ipv4Address localAddress, uint16_t localPort,
                         Ipv4Address peerAddress, uint16_t peerPort)
{
  NS_LOG_FUNCTION (this << localAddress << localPort << peerAddress << peerPort);
  return m_endPoints->Allocate (localAddress, localPort,
                                peerAddress, peerPort);
}

void
MpRDMAL4Protocol::DeAllocate (Ipv4EndPoint *endPoint)
{
  NS_LOG_FUNCTION (this << endPoint);
  m_endPoints->DeAllocate (endPoint);
}

Ipv6EndPoint *
MpRDMAL4Protocol::Allocate6 (void)
{
  NS_LOG_FUNCTION_NOARGS ();
  return m_endPoints6->Allocate ();
}

Ipv6EndPoint *
MpRDMAL4Protocol::Allocate6 (Ipv6Address address)
{
  NS_LOG_FUNCTION (this << address);
  return m_endPoints6->Allocate (address);
}

Ipv6EndPoint *
MpRDMAL4Protocol::Allocate6 (uint16_t port)
{
  NS_LOG_FUNCTION (this << port);
  return m_endPoints6->Allocate (port);
}

Ipv6EndPoint *
MpRDMAL4Protocol::Allocate6 (Ipv6Address address, uint16_t port)
{
  NS_LOG_FUNCTION (this << address << port);
  return m_endPoints6->Allocate (address, port);
}

Ipv6EndPoint *
MpRDMAL4Protocol::Allocate6 (Ipv6Address localAddress, uint16_t localPort,
                          Ipv6Address peerAddress, uint16_t peerPort)
{
  NS_LOG_FUNCTION (this << localAddress << localPort << peerAddress << peerPort);
  return m_endPoints6->Allocate (localAddress, localPort,
                                 peerAddress, peerPort);
}

void
MpRDMAL4Protocol::DeAllocate (Ipv6EndPoint *endPoint)
{
  NS_LOG_FUNCTION (this << endPoint);
  m_endPoints6->DeAllocate (endPoint);
}

void
MpRDMAL4Protocol::ReceiveIcmp (Ipv4Address icmpSource, uint8_t icmpTtl,
                            uint8_t icmpType, uint8_t icmpCode, uint32_t icmpInfo,
                            Ipv4Address payloadSource,Ipv4Address payloadDestination,
                            const uint8_t payload[8])
{
  NS_LOG_FUNCTION (this << icmpSource << icmpTtl << icmpType << icmpCode << icmpInfo
                        << payloadSource << payloadDestination);
  uint16_t src, dst;
  src = payload[0] << 8;
  src |= payload[1];
  dst = payload[2] << 8;
  dst |= payload[3];

  Ipv4EndPoint *endPoint = m_endPoints->SimpleLookup (payloadSource, src, payloadDestination, dst);
  if (endPoint != 0)
    {
      endPoint->ForwardIcmp (icmpSource, icmpTtl, icmpType, icmpCode, icmpInfo);
    }
  else
    {
      NS_LOG_DEBUG ("no endpoint found source=" << payloadSource <<
                    ", destination=" << payloadDestination <<
                    ", src=" << src << ", dst=" << dst);
    }
}

void
MpRDMAL4Protocol::ReceiveIcmp (Ipv6Address icmpSource, uint8_t icmpTtl,
                            uint8_t icmpType, uint8_t icmpCode, uint32_t icmpInfo,
                            Ipv6Address payloadSource,Ipv6Address payloadDestination,
                            const uint8_t payload[8])
{
  NS_LOG_FUNCTION (this << icmpSource << icmpTtl << icmpType << icmpCode << icmpInfo
                        << payloadSource << payloadDestination);
  uint16_t src, dst;
  src = payload[0] << 8;
  src |= payload[1];
  dst = payload[2] << 8;
  dst |= payload[3];

  Ipv6EndPoint *endPoint = m_endPoints6->SimpleLookup (payloadSource, src, payloadDestination, dst);
  if (endPoint != 0)
    {
      endPoint->ForwardIcmp (icmpSource, icmpTtl, icmpType, icmpCode, icmpInfo);
    }
  else
    {
      NS_LOG_DEBUG ("no endpoint found source=" << payloadSource <<
                    ", destination=" << payloadDestination <<
                    ", src=" << src << ", dst=" << dst);
    }
}

enum IpL4Protocol::RxStatus
MpRDMAL4Protocol::PacketReceived (Ptr<Packet> packet, MpRDMAHeader &incomingMpRDMAHeader,
                               const Address &source, const Address &destination)
{
  NS_LOG_FUNCTION (this << packet << incomingMpRDMAHeader << source << destination);

  if (Node::ChecksumEnabled ())
    {
      incomingMpRDMAHeader.EnableChecksums ();
      incomingMpRDMAHeader.InitializeChecksum (source, destination, PROT_NUMBER);
    }

  packet->PeekHeader (incomingMpRDMAHeader);

  NS_LOG_LOGIC ("MpRDMAL4Protocol " << this
                                 << " receiving seq " << incomingMpRDMAHeader.GetSequenceNumber ()
                                 << " ack " << incomingMpRDMAHeader.GetAckNumber ()
                                 << " flags "<< MpRDMAHeader::FlagsToString (incomingMpRDMAHeader.GetFlags ())
                                 << " data size " << packet->GetSize ());

  if (!incomingMpRDMAHeader.IsChecksumOk ())
    {
      NS_LOG_INFO ("Bad checksum, dropping packet!");
      return IpL4Protocol::RX_CSUM_FAILED;
    }

  return IpL4Protocol::RX_OK;
}

void
MpRDMAL4Protocol::NoEndPointsFound (const MpRDMAHeader &incomingHeader,
                                 const Address &incomingSAddr,
                                 const Address &incomingDAddr)
{
  NS_LOG_FUNCTION (this << incomingHeader << incomingSAddr << incomingDAddr);

  if (!(incomingHeader.GetFlags () & MpRDMAHeader::RST))
    {
      // build a RST packet and send
      Ptr<Packet> rstPacket = Create<Packet> ();
      MpRDMAHeader outgoingMpRDMAHeader;

      if (incomingHeader.GetFlags () & MpRDMAHeader::ACK)
        {
          // ACK bit was set
          outgoingMpRDMAHeader.SetFlags (MpRDMAHeader::RST);
          outgoingMpRDMAHeader.SetSequenceNumber (incomingHeader.GetAckNumber ());
        }
      else
        {
          outgoingMpRDMAHeader.SetFlags (MpRDMAHeader::RST | MpRDMAHeader::ACK);
          outgoingMpRDMAHeader.SetSequenceNumber (SequenceNumber32 (0));
          outgoingMpRDMAHeader.SetAckNumber (incomingHeader.GetSequenceNumber () +
                                          SequenceNumber32 (1));
        }

      // Remember that parameters refer to the incoming packet; in reply,
      // we need to swap src/dst

      outgoingMpRDMAHeader.SetSourcePort (incomingHeader.GetDestinationPort ());
      outgoingMpRDMAHeader.SetDestinationPort (incomingHeader.GetSourcePort ());

      SendPacket (rstPacket, outgoingMpRDMAHeader, incomingDAddr, incomingSAddr);
    }
}

enum IpL4Protocol::RxStatus
MpRDMAL4Protocol::Receive (Ptr<Packet> packet,
                        Ipv4Header const &incomingIpHeader,
                        Ptr<Ipv4Interface> incomingInterface)
{
  NS_LOG_FUNCTION (this << packet << incomingIpHeader << incomingInterface);

  MpRDMAHeader incomingMpRDMAHeader;
  IpL4Protocol::RxStatus checksumControl;

  checksumControl = PacketReceived (packet, incomingMpRDMAHeader,
                                    incomingIpHeader.GetSource (),
                                    incomingIpHeader.GetDestination ());

  if (checksumControl != IpL4Protocol::RX_OK)
    {
      return checksumControl;
    }

  Ipv4EndPointDemux::EndPoints endPoints;
  endPoints = m_endPoints->Lookup (incomingIpHeader.GetDestination (),
                                   incomingMpRDMAHeader.GetDestinationPort (),
                                   incomingIpHeader.GetSource (),
                                   incomingMpRDMAHeader.GetSourcePort (),
                                   incomingInterface);

  if (endPoints.empty ())
    {
      if (this->GetObject<Ipv6L3Protocol> () != 0)
        {
          NS_LOG_LOGIC ("  No Ipv4 endpoints matched on MpRDMAL4Protocol, trying Ipv6 " << this);
          Ptr<Ipv6Interface> fakeInterface;
          Ipv6Header ipv6Header;
          Ipv6Address src, dst;

          src = Ipv6Address::MakeIpv4MappedAddress (incomingIpHeader.GetSource ());
          dst = Ipv6Address::MakeIpv4MappedAddress (incomingIpHeader.GetDestination ());
          ipv6Header.SetSourceAddress (src);
          ipv6Header.SetDestinationAddress (dst);
          return (this->Receive (packet, ipv6Header, fakeInterface));
        }

      NS_LOG_LOGIC ("MpRDMAL4Protocol " << this << " received a packet but"
                    " no endpoints matched." <<
                    " destination IP: " << incomingIpHeader.GetDestination () <<
                    " destination port: "<< incomingMpRDMAHeader.GetDestinationPort () <<
                    " source IP: " << incomingIpHeader.GetSource () <<
                    " source port: "<< incomingMpRDMAHeader.GetSourcePort ());

      NoEndPointsFound (incomingMpRDMAHeader, incomingIpHeader.GetSource (),
                        incomingIpHeader.GetDestination ());

      return IpL4Protocol::RX_ENDPOINT_CLOSED;

    }

  NS_ASSERT_MSG (endPoints.size () == 1, "Demux returned more than one endpoint");
  NS_LOG_LOGIC ("MpRDMAL4Protocol " << this << " received a packet and"
                " now forwarding it up to endpoint/socket");

  (*endPoints.begin ())->ForwardUp (packet, incomingIpHeader,
                                    incomingMpRDMAHeader.GetSourcePort (),
                                    incomingInterface);

  return IpL4Protocol::RX_OK;
}

enum IpL4Protocol::RxStatus
MpRDMAL4Protocol::Receive (Ptr<Packet> packet,
                        Ipv6Header const &incomingIpHeader,
                        Ptr<Ipv6Interface> interface)
{
  NS_LOG_FUNCTION (this << packet << incomingIpHeader.GetSourceAddress () <<
                   incomingIpHeader.GetDestinationAddress ());

  MpRDMAHeader incomingMpRDMAHeader;
  IpL4Protocol::RxStatus checksumControl;

  // If we are receving a v4-mapped packet, we will re-calculate the MpRDMA checksum
  // Is it worth checking every received "v6" packet to see if it is v4-mapped in
  // order to avoid re-calculating MpRDMA checksums for v4-mapped packets?

  checksumControl = PacketReceived (packet, incomingMpRDMAHeader,
                                    incomingIpHeader.GetSourceAddress (),
                                    incomingIpHeader.GetDestinationAddress ());

  if (checksumControl != IpL4Protocol::RX_OK)
    {
      return checksumControl;
    }

  Ipv6EndPointDemux::EndPoints endPoints =
    m_endPoints6->Lookup (incomingIpHeader.GetDestinationAddress (),
                          incomingMpRDMAHeader.GetDestinationPort (),
                          incomingIpHeader.GetSourceAddress (),
                          incomingMpRDMAHeader.GetSourcePort (), interface);
  if (endPoints.empty ())
    {
      NS_LOG_LOGIC ("MpRDMAL4Protocol " << this << " received a packet but"
                    " no endpoints matched." <<
                    " destination IP: " << incomingIpHeader.GetDestinationAddress () <<
                    " destination port: "<< incomingMpRDMAHeader.GetDestinationPort () <<
                    " source IP: " << incomingIpHeader.GetSourceAddress () <<
                    " source port: "<< incomingMpRDMAHeader.GetSourcePort ());

      NoEndPointsFound (incomingMpRDMAHeader, incomingIpHeader.GetSourceAddress (),
                        incomingIpHeader.GetDestinationAddress ());

      return IpL4Protocol::RX_ENDPOINT_CLOSED;
    }

  NS_ASSERT_MSG (endPoints.size () == 1, "Demux returned more than one endpoint");
  NS_LOG_LOGIC ("MpRDMAL4Protocol " << this << " received a packet and"
                " now forwarding it up to endpoint/socket");

  (*endPoints.begin ())->ForwardUp (packet, incomingIpHeader,
                                    incomingMpRDMAHeader.GetSourcePort (), interface);

  return IpL4Protocol::RX_OK;
}

void
MpRDMAL4Protocol::SendPacketV4 (Ptr<Packet> packet, const MpRDMAHeader &outgoing,
                             const Ipv4Address &saddr, const Ipv4Address &daddr,
                             Ptr<NetDevice> oif) const
{
  NS_LOG_FUNCTION (this << packet << saddr << daddr << oif);
  NS_LOG_LOGIC ("MpRDMAL4Protocol " << this
                                 << " sending seq " << outgoing.GetSequenceNumber ()
                                 << " ack " << outgoing.GetAckNumber ()
                                 << " flags " << MpRDMAHeader::FlagsToString (outgoing.GetFlags ())
                                 << " data size " << packet->GetSize ());
  // XXX outgoingHeader cannot be logged

  MpRDMAHeader outgoingHeader = outgoing;
  /** \todo UrgentPointer */
  /* outgoingHeader.SetUrgentPointer (0); */
  if (Node::ChecksumEnabled ())
    {
      outgoingHeader.EnableChecksums ();
    }
  outgoingHeader.InitializeChecksum (saddr, daddr, PROT_NUMBER);

  packet->AddHeader (outgoingHeader);

  Ptr<Ipv4> ipv4 =
    m_node->GetObject<Ipv4> ();
  if (ipv4 != 0)
    {
      Ipv4Header header;
      header.SetSource (saddr);
      header.SetDestination (daddr);
      header.SetProtocol (PROT_NUMBER);
      Socket::SocketErrno errno_;
      Ptr<Ipv4Route> route;
      if (ipv4->GetRoutingProtocol () != 0)
        {
          route = ipv4->GetRoutingProtocol ()->RouteOutput (packet, header, oif, errno_);
        }
      else
        {
          NS_LOG_ERROR ("No IPV4 Routing Protocol");
          route = 0;
        }
      m_downTarget (packet, saddr, daddr, PROT_NUMBER, route);
    }
  else
    {
      NS_FATAL_ERROR ("Trying to use MpRDMA on a node without an Ipv4 interface");
    }
}

void
MpRDMAL4Protocol::SendPacketV6 (Ptr<Packet> packet, const MpRDMAHeader &outgoing,
                             const Ipv6Address &saddr, const Ipv6Address &daddr,
                             Ptr<NetDevice> oif) const
{
  NS_LOG_FUNCTION (this << packet << saddr << daddr << oif);
  NS_LOG_LOGIC ("MpRDMAL4Protocol " << this
                                 << " sending seq " << outgoing.GetSequenceNumber ()
                                 << " ack " << outgoing.GetAckNumber ()
                                 << " flags " << MpRDMAHeader::FlagsToString (outgoing.GetFlags ())
                                 << " data size " << packet->GetSize ());
  // XXX outgoingHeader cannot be logged

  if (daddr.IsIpv4MappedAddress ())
    {
      return (SendPacket (packet, outgoing, saddr.GetIpv4MappedAddress (), daddr.GetIpv4MappedAddress (), oif));
    }
  MpRDMAHeader outgoingHeader = outgoing;
  /** \todo UrgentPointer */
  /* outgoingHeader.SetUrgentPointer (0); */
  if (Node::ChecksumEnabled ())
    {
      outgoingHeader.EnableChecksums ();
    }
  outgoingHeader.InitializeChecksum (saddr, daddr, PROT_NUMBER);

  packet->AddHeader (outgoingHeader);

  Ptr<Ipv6L3Protocol> ipv6 = m_node->GetObject<Ipv6L3Protocol> ();
  if (ipv6 != 0)
    {
      Ipv6Header header;
      header.SetSourceAddress (saddr);
      header.SetDestinationAddress (daddr);
      header.SetNextHeader (PROT_NUMBER);
      Socket::SocketErrno errno_;
      Ptr<Ipv6Route> route;
      if (ipv6->GetRoutingProtocol () != 0)
        {
          route = ipv6->GetRoutingProtocol ()->RouteOutput (packet, header, oif, errno_);
        }
      else
        {
          NS_LOG_ERROR ("No IPV6 Routing Protocol");
          route = 0;
        }
      m_downTarget6 (packet, saddr, daddr, PROT_NUMBER, route);
    }
  else
    {
      NS_FATAL_ERROR ("Trying to use MpRDMA on a node without an Ipv6 interface");
    }
}

void
MpRDMAL4Protocol::SendPacket (Ptr<Packet> pkt, const MpRDMAHeader &outgoing,
                           const Address &saddr, const Address &daddr,
                           Ptr<NetDevice> oif) const
{
  NS_LOG_FUNCTION (this << pkt << outgoing << saddr << daddr << oif);
  if (Ipv4Address::IsMatchingType (saddr))
    {
      NS_ASSERT (Ipv4Address::IsMatchingType (daddr));

      SendPacketV4 (pkt, outgoing, Ipv4Address::ConvertFrom (saddr),
                    Ipv4Address::ConvertFrom (daddr), oif);

      return;
    }
  else if (Ipv6Address::IsMatchingType (saddr))
    {
      NS_ASSERT (Ipv6Address::IsMatchingType (daddr));

      SendPacketV6 (pkt, outgoing, Ipv6Address::ConvertFrom (saddr),
                    Ipv6Address::ConvertFrom (daddr), oif);

      return;
    }
  else if (InetSocketAddress::IsMatchingType (saddr))
    {
      InetSocketAddress s = InetSocketAddress::ConvertFrom (saddr);
      InetSocketAddress d = InetSocketAddress::ConvertFrom (daddr);

      SendPacketV4 (pkt, outgoing, s.GetIpv4 (), d.GetIpv4 (), oif);

      return;
    }
  else if (Inet6SocketAddress::IsMatchingType (saddr))
    {
      Inet6SocketAddress s = Inet6SocketAddress::ConvertFrom (saddr);
      Inet6SocketAddress d = Inet6SocketAddress::ConvertFrom (daddr);

      SendPacketV6 (pkt, outgoing, s.GetIpv6 (), d.GetIpv6 (), oif);

      return;
    }

  NS_FATAL_ERROR ("Trying to send a packet without IP addresses");
}

void
MpRDMAL4Protocol::AddSocket (Ptr<MpRDMASocketImpl> socket)
{
  NS_LOG_FUNCTION (this << socket);
  std::vector<Ptr<MpRDMASocketImpl> >::iterator it = m_sockets.begin ();

  while (it != m_sockets.end ())
    {
      if (*it == socket)
        {
          return;
        }

      ++it;
    }

  m_sockets.push_back (socket);
}

bool
MpRDMAL4Protocol::RemoveSocket (Ptr<MpRDMASocketImpl> socket)
{
  NS_LOG_FUNCTION (this << socket);
  std::vector<Ptr<MpRDMASocketImpl> >::iterator it = m_sockets.begin ();

  while (it != m_sockets.end ())
    {
      if (*it == socket)
        {
          m_sockets.erase (it);
          return true;
        }

      ++it;
    }

  return false;
}

void
MpRDMAL4Protocol::SetDownTarget (IpL4Protocol::DownTargetCallback callback)
{
  m_downTarget = callback;
}

IpL4Protocol::DownTargetCallback
MpRDMAL4Protocol::GetDownTarget (void) const
{
  return m_downTarget;
}

void
MpRDMAL4Protocol::SetDownTarget6 (IpL4Protocol::DownTargetCallback6 callback)
{
  m_downTarget6 = callback;
}

IpL4Protocol::DownTargetCallback6
MpRDMAL4Protocol::GetDownTarget6 (void) const
{
  return m_downTarget6;
}

} // namespace ns3

