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
 * 2016-04-09
 */ 

#include <stdint.h>
#include <iostream>
#include "mp-rdma-header.h" 
#include "tcp-option.h"
#include "ns3/buffer.h"
#include "ns3/address-utils.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("MpRDMAHeader");

NS_OBJECT_ENSURE_REGISTERED (MpRDMAHeader);

MpRDMAHeader::MpRDMAHeader () 
  : m_sourcePort (0),
    m_destinationPort (0),
    m_sequenceNumber (0),
    m_ackNumber (0),
    m_length (5),
    m_flags (0),
    m_windowSize (0xffff),
    m_urgentPointer (0),
    m_calcChecksum (false),
    m_goodChecksum (true),
    m_optionsLen (0)
{
}

MpRDMAHeader::~MpRDMAHeader ()
{
}

std::string
MpRDMAHeader::FlagsToString (uint8_t flags, const std::string& delimiter)
{
  static const char* flagNames[8] = {
    "FIN",
    "SYN",
    "RST",
    "PSH",
    "ACK",
    "URG",
    "ECE",
    "CWR"
  };
  std::string flagsDescription = "";
  for (uint8_t i = 0; i < 8; ++i)
    {
      if (flags & (1 << i))
        {
          if (flagsDescription.length () > 0)
            {
              flagsDescription += delimiter;
            }
          flagsDescription.append (flagNames[i]);
        }
    }
  return flagsDescription;
}

void
MpRDMAHeader::EnableChecksums (void)
{
  m_calcChecksum = true;
}

void
MpRDMAHeader::SetSourcePort (uint16_t port)
{
  m_sourcePort = port;
}

void
MpRDMAHeader::SetDestinationPort (uint16_t port)
{
  m_destinationPort = port;
}

void
MpRDMAHeader::SetSequenceNumber (SequenceNumber32 sequenceNumber)
{
  m_sequenceNumber = sequenceNumber;
}

void
MpRDMAHeader::SetAckNumber (SequenceNumber32 ackNumber)
{
  m_ackNumber = ackNumber;
}

void
MpRDMAHeader::SetFlags (uint8_t flags)
{
  m_flags = flags;
}

void
MpRDMAHeader::SetWindowSize (uint16_t windowSize)
{
  m_windowSize = windowSize;
}

void
MpRDMAHeader::SetUrgentPointer (uint16_t urgentPointer)
{
  m_urgentPointer = urgentPointer;
}

uint16_t
MpRDMAHeader::GetSourcePort () const
{
  return m_sourcePort;
}

uint16_t
MpRDMAHeader::GetDestinationPort () const
{
  return m_destinationPort;
}

SequenceNumber32
MpRDMAHeader::GetSequenceNumber () const
{
  return m_sequenceNumber;
}

SequenceNumber32
MpRDMAHeader::GetAckNumber () const
{
  return m_ackNumber;
}

uint8_t
MpRDMAHeader::GetLength () const
{
  return m_length;
}

uint8_t
MpRDMAHeader::GetOptionLength () const
{
  return m_optionsLen;
}

uint8_t
MpRDMAHeader::GetMaxOptionLength () const
{
  return m_maxOptionsLen;
}

uint8_t
MpRDMAHeader::GetFlags () const
{
  return m_flags;
}

uint16_t
MpRDMAHeader::GetWindowSize () const
{
  return m_windowSize;
}

uint16_t
MpRDMAHeader::GetUrgentPointer () const
{
  return m_urgentPointer;
}

void
MpRDMAHeader::InitializeChecksum (const Ipv4Address &source,
                               const Ipv4Address &destination,
                               uint8_t protocol)
{
  m_source = source;
  m_destination = destination;
  m_protocol = protocol;
}

void
MpRDMAHeader::InitializeChecksum (const Ipv6Address &source,
                               const Ipv6Address &destination,
                               uint8_t protocol)
{
  m_source = source;
  m_destination = destination;
  m_protocol = protocol;
}

void
MpRDMAHeader::InitializeChecksum (const Address &source,
                               const Address &destination,
                               uint8_t protocol)
{
  m_source = source;
  m_destination = destination;
  m_protocol = protocol;
}

uint16_t
MpRDMAHeader::CalculateHeaderChecksum (uint16_t size) const
{
  /* Buffer size must be at least as large as the largest IP pseudo-header */
  /* [per RFC2460, but without consideration for IPv6 extension hdrs]      */
  /* Src address            16 bytes (more generally, Address::MAX_SIZE)   */
  /* Dst address            16 bytes (more generally, Address::MAX_SIZE)   */
  /* Upper layer pkt len    4 bytes                                        */
  /* Zero                   3 bytes                                        */
  /* Next header            1 byte                                         */

  uint32_t maxHdrSz = (2 * Address::MAX_SIZE) + 8;
  Buffer buf = Buffer (maxHdrSz);
  buf.AddAtStart (maxHdrSz);
  Buffer::Iterator it = buf.Begin ();
  uint32_t hdrSize = 0;

  WriteTo (it, m_source);
  WriteTo (it, m_destination);
  if (Ipv4Address::IsMatchingType (m_source))
    {
      it.WriteU8 (0); /* protocol */
      it.WriteU8 (m_protocol); /* protocol */
      it.WriteU8 (size >> 8); /* length */
      it.WriteU8 (size & 0xff); /* length */
      hdrSize = 12;
    }
  else
    {
      it.WriteU16 (0);
      it.WriteU8 (size >> 8); /* length */
      it.WriteU8 (size & 0xff); /* length */
      it.WriteU16 (0);
      it.WriteU8 (0);
      it.WriteU8 (m_protocol); /* protocol */
      hdrSize = 40;
    }

  it = buf.Begin ();
  /* we don't CompleteChecksum ( ~ ) now */
  return ~(it.CalculateIpChecksum (hdrSize));
}

bool
MpRDMAHeader::IsChecksumOk (void) const
{
  return m_goodChecksum;
}

TypeId
MpRDMAHeader::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::MpRDMAHeader")
    .SetParent<Header> ()
    .SetGroupName ("Internet")
    .AddConstructor<MpRDMAHeader> ()
  ;
  return tid;
}

TypeId
MpRDMAHeader::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}

void
MpRDMAHeader::Print (std::ostream &os)  const
{
  os << m_sourcePort << " > " << m_destinationPort;

  if (m_flags != 0)
    {
      os << " [" << FlagsToString (m_flags) << "]";
    }

  os << " Seq=" << m_sequenceNumber << " Ack=" << m_ackNumber << " Win=" << m_windowSize;

  MpRDMAOptionList::const_iterator op;

  for (op = m_options.begin (); op != m_options.end (); ++op)
    {
      os << " " << (*op)->GetInstanceTypeId ().GetName () << "(";
      (*op)->Print (os);
      os << ")";
    }
}

uint32_t
MpRDMAHeader::GetSerializedSize (void)  const
{
  return CalculateHeaderLength () * 4;
}

void
MpRDMAHeader::Serialize (Buffer::Iterator start)  const
{
  Buffer::Iterator i = start;
  i.WriteHtonU16 (m_sourcePort);
  i.WriteHtonU16 (m_destinationPort);
  i.WriteHtonU32 (m_sequenceNumber.GetValue ());
  i.WriteHtonU32 (m_ackNumber.GetValue ());
  i.WriteHtonU16 (GetLength () << 12 | m_flags); //reserved bits are all zero
  i.WriteHtonU16 (m_windowSize);
  i.WriteHtonU16 (0);
  i.WriteHtonU16 (m_urgentPointer);

  // Serialize options if they exist
  // This implementation does not presently try to align options on word
  // boundaries using NOP options
  uint32_t optionLen = 0;
  MpRDMAOptionList::const_iterator op;
  for (op = m_options.begin (); op != m_options.end (); ++op)
    {
      optionLen += (*op)->GetSerializedSize ();
      (*op)->Serialize (i);
      i.Next ((*op)->GetSerializedSize ());
    }

  // padding to word alignment; add ENDs and/or pad values (they are the same)
  while (optionLen % 4)
    {
      i.WriteU8 (TcpOption::END);
      ++optionLen;
    }

  // Make checksum
  if (m_calcChecksum)
    {
      uint16_t headerChecksum = CalculateHeaderChecksum (start.GetSize ());
      i = start;
      uint16_t checksum = i.CalculateIpChecksum (start.GetSize (), headerChecksum);

      i = start;
      i.Next (16);
      i.WriteU16 (checksum);
    }
}

uint32_t
MpRDMAHeader::Deserialize (Buffer::Iterator start)
{
  Buffer::Iterator i = start;
  m_sourcePort = i.ReadNtohU16 ();
  m_destinationPort = i.ReadNtohU16 ();
  m_sequenceNumber = i.ReadNtohU32 ();
  m_ackNumber = i.ReadNtohU32 ();
  uint16_t field = i.ReadNtohU16 ();
  m_flags = field & 0x3F;
  m_length = field >> 12;
  m_windowSize = i.ReadNtohU16 ();
  i.Next (2);
  m_urgentPointer = i.ReadNtohU16 ();

  // Deserialize options if they exist
  m_options.clear ();
  uint32_t optionLen = (m_length - 5) * 4;
  if (optionLen > m_maxOptionsLen)
    {
      NS_LOG_ERROR ("Illegal TCP option length " << optionLen << "; options discarded");
      return 20;
    }
  while (optionLen)
    {
      uint8_t kind = i.PeekU8 ();
      Ptr<TcpOption> op;
      uint32_t optionSize;
      if (TcpOption::IsKindKnown (kind))
        {
          op = TcpOption::CreateOption (kind);
        }
      else
        {
          op = TcpOption::CreateOption (TcpOption::UNKNOWN);
          NS_LOG_WARN ("Option kind " << static_cast<int> (kind) << " unknown, skipping.");
        }
      optionSize = op->Deserialize (i);
      if (optionSize != op->GetSerializedSize ())
        {
          NS_LOG_ERROR ("Option did not deserialize correctly");
          break;
        }
      if (optionLen >= optionSize)
        {
          optionLen -= optionSize;
          i.Next (optionSize);
          m_options.push_back (op);
        }
      else
        {
          NS_LOG_ERROR ("Option exceeds TCP option space; option discarded");
          break;
        }
      if (op->GetKind () == TcpOption::END)
        {
          while (optionLen)
            {
              // Discard padding bytes without adding to option list
              i.Next (1);
              --optionLen;
            }
        }
    }

  if (m_length != CalculateHeaderLength ())
    {
      NS_LOG_ERROR ("Mismatch between calculated length and in-header value");
    }

  // Do checksum
  if (m_calcChecksum)
    {
      uint16_t headerChecksum = CalculateHeaderChecksum (start.GetSize ());
      i = start;
      uint16_t checksum = i.CalculateIpChecksum (start.GetSize (), headerChecksum);
      m_goodChecksum = (checksum == 0);
    }

  return GetSerializedSize ();
}

uint8_t
MpRDMAHeader::CalculateHeaderLength () const
{
  uint32_t len = 20;
  MpRDMAOptionList::const_iterator i;

  for (i = m_options.begin (); i != m_options.end (); ++i)
    {
      len += (*i)->GetSerializedSize ();
    }
  // Option list may not include padding; need to pad up to word boundary
  if (len % 4)
    {
      len += 4 - (len % 4);
    }
  return len >> 2;
}

bool
MpRDMAHeader::AppendOption (Ptr<TcpOption> option)
{
  if (m_optionsLen + option->GetSerializedSize () <= m_maxOptionsLen)
    {
      if (!TcpOption::IsKindKnown (option->GetKind ()))
        {
          NS_LOG_WARN ("The option kind " << static_cast<int> (option->GetKind ()) << " is unknown");
          return false;
        }

      if (option->GetKind () != TcpOption::END)
        {
          m_options.push_back (option);
          m_optionsLen += option->GetSerializedSize ();

          uint32_t totalLen = 20 + 3 + m_optionsLen;
          m_length = totalLen >> 2;
        }

      return true;
    }

  return false;
}

Ptr<TcpOption>
MpRDMAHeader::GetOption (uint8_t kind) const
{
  MpRDMAOptionList::const_iterator i;

  for (i = m_options.begin (); i != m_options.end (); ++i)
    {
      if ((*i)->GetKind () == kind)
        {
          return (*i);
        }
    }

  return 0;
}

bool
MpRDMAHeader::HasOption (uint8_t kind) const
{
  MpRDMAOptionList::const_iterator i;

  for (i = m_options.begin (); i != m_options.end (); ++i)
    {
      if ((*i)->GetKind () == kind)
        {
          return true;
        }
    }

  return false;
}

bool
operator== (const MpRDMAHeader &lhs, const MpRDMAHeader &rhs)
{
  return (
           lhs.m_sourcePort      == rhs.m_sourcePort
           && lhs.m_destinationPort == rhs.m_destinationPort
           && lhs.m_sequenceNumber  == rhs.m_sequenceNumber
           && lhs.m_ackNumber       == rhs.m_ackNumber
           && lhs.m_flags           == rhs.m_flags
           && lhs.m_windowSize      == rhs.m_windowSize
           && lhs.m_urgentPointer   == rhs.m_urgentPointer
           );
}

std::ostream&
operator<< (std::ostream& os, MpRDMAHeader const & tc) 
{
  tc.Print (os);
  return os;
}

} // namespace ns3
