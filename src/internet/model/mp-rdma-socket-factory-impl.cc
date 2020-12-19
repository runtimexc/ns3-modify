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

#include "mp-rdma-socket-factory-impl.h"
#include "mp-rdma-l4-protocol.h"
#include "ns3/socket.h"
#include "ns3/assert.h"

namespace ns3 {

MpRDMASocketFactoryImpl::MpRDMASocketFactoryImpl ()
  : m_mpRDMA (0) 
{
}
MpRDMASocketFactoryImpl::~MpRDMASocketFactoryImpl ()
{
  NS_ASSERT (m_mpRDMA == 0);
}

void
MpRDMASocketFactoryImpl::SetMpRDMA (Ptr<MpRDMAL4Protocol> mpRDMA)
{
  m_mpRDMA = mpRDMA;
}

Ptr<Socket>
MpRDMASocketFactoryImpl::CreateSocket (void)
{
  return m_mpRDMA->CreateSocket ();
}

void 
MpRDMASocketFactoryImpl::DoDispose (void)
{
  m_mpRDMA = 0;
  MpRDMASocketFactory::DoDispose (); 
}

} // namespace ns3
