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

#ifndef MP_RDMA_SOCKET_FACTORY_H
#define MP_RDMA_SOCKET_FACTORY_H

#include "ns3/socket-factory.h"

namespace ns3 {

class Socket;

/**
 * \ingroup socket
 *
 * \brief API to create MP-RDMA socket instances 
 *
 * This abstract class defines the API for MP-RDMA socket factory.
 * All MP-RDMA implementations must provide an implementation of CreateSocket
 * below.
 * 
 * \see MpRDMASocketFactoryImpl
 */
class MpRDMASocketFactory : public SocketFactory 
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);

};

} // namespace ns3

#endif /* MP_RDMA_SOCKET_FACTORY_H */ 
