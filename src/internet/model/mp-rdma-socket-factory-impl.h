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

#ifndef MP_RDMA_SOCKET_FACTORY_IMPL_H
#define MP_RDMA_SOCKET_FACTORY_IMPL_H

#include "ns3/mp-rdma-socket-factory.h" 
#include "ns3/ptr.h"

namespace ns3 {

class MpRDMAL4Protocol;

/**
 * \ingroup internet
 * \defgroup mp-rdma MP-RDMA 
 */

/**
 * \ingroup mp-rdma
 * \brief Object to create MP-RDMA socket instances 
 *
 * This class implements the API for creating MP-RDMA sockets.
 * It is a socket factory (deriving from class SocketFactory).
 */
class MpRDMASocketFactoryImpl : public MpRDMASocketFactory
{
public:
  MpRDMASocketFactoryImpl ();
  virtual ~MpRDMASocketFactoryImpl ();

  /**
   * \brief Set the associated MP-RDMA L4 protocol.
   * \param mp-rdma the MP-RDMA L4 protocol
   */
  void SetMpRDMA (Ptr<MpRDMAL4Protocol> mpRDMA);  

  /**
   * \brief Implements a method to create a MpRDMA-based socket and return
   * a base class smart pointer to the socket.
   *
   * \return smart pointer to Socket
   */
  virtual Ptr<Socket> CreateSocket (void);

protected:
  virtual void DoDispose (void);
private:
  Ptr<MpRDMAL4Protocol> m_mpRDMA; //!< the associated MP-RDMA L4 protocol 
};

} // namespace ns3

#endif /* MP_RDMA_SOCKET_FACTORY_IMPL_H */ 
