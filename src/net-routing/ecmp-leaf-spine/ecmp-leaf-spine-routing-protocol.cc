#include "ns3/ecmp-leaf-spine-routing-protocol.h"
#include "ns3/ipv4-interface.h"
#include "ns3/uinteger.h"
#include "ns3/node.h"
#include "ns3/ecmp-leaf-spine-header.h"
#include "ns3/ip-l4-protocol.h"
#include "ns3/mp-rdma-header.h"
#include "ns3/pathid-tag.h"
#include "ns3/retx-tag.h" 
#include "ns3/log-manager.h"
#include "ns3/point-to-point-net-device.h" 

#include <stdio.h>

#define TEST_FAILURE 0

#define TEST_DROP 1 //wsq modify 1, origin:0
#define DROP_RATE 0.01

namespace ns3
{
    NS_OBJECT_ENSURE_REGISTERED (ECMPLeafSpineRoutingProtocol);

    TypeId ECMPLeafSpineRoutingProtocol::GetTypeId(void)
    {
        static TypeId tid = TypeId ("ns3::ECMPLeafSpineRoutingProtocol")
            .SetParent<NetRoutingProtocol> ()
            .AddConstructor<ECMPLeafSpineRoutingProtocol> ()
            .AddAttribute("ServerPerLeafNum", "Servers per leaf switch.",
                          UintegerValue(4), MakeUintegerAccessor(&ECMPLeafSpineRoutingProtocol::m_serverPerLeafNum), MakeUintegerChecker<uint32_t>()) 
            .AddAttribute("LeafSwitchNum", "Leaf Switch number.",
                          UintegerValue(2), MakeUintegerAccessor(&ECMPLeafSpineRoutingProtocol::m_leafSwitchNum), MakeUintegerChecker<uint32_t>()) 
            .AddAttribute("SpineSwitchNum", "Spine Switch number.",
                          UintegerValue(4), MakeUintegerAccessor(&ECMPLeafSpineRoutingProtocol::m_spineSwitchNum), MakeUintegerChecker<uint32_t>())
            .AddAttribute("SNDL", "Sender L.",
                          UintegerValue(32), MakeUintegerAccessor(&ECMPLeafSpineRoutingProtocol::m_sndL), MakeUintegerChecker<uint32_t>())
            .AddAttribute("RCVL", "Receiver L.",
                          UintegerValue(64), MakeUintegerAccessor(&ECMPLeafSpineRoutingProtocol::m_rcvL), MakeUintegerChecker<uint32_t>())
            .AddAttribute("type", "Type.",
                          UintegerValue(ECMP_LeafSpine_FLOW_LEVEL), MakeUintegerAccessor(&ECMPLeafSpineRoutingProtocol::m_type), MakeUintegerChecker<uint32_t>()); 

        return tid;
    }

    ECMPLeafSpineRoutingProtocol::ECMPLeafSpineRoutingProtocol()
    {
        m_logThroughputInterval = 0.001; //1ms 
        m_drop = false;
        m_change_1 = false; 
        m_change_2 = false;
        m_logStart = true; 
        
        for(uint32_t i = 0; i < 10; i++) 
        {
            perPathBytesCount[i] = 0; 
            flow2PerPathBytesCount[i] = 0; 
        }

        drop_count = 0; 

        m_sendingBytes = 0;

        failure_devid = 1000; 
        recover_failure_devid = 1000; 
    }
    
    ECMPLeafSpineRoutingProtocol::~ECMPLeafSpineRoutingProtocol()
    {
    }

    uint32_t ECMPLeafSpineRoutingProtocol::GetNAddresses(uint32_t interface) const
    {
        return 1;
    }

    Ipv4InterfaceAddress ECMPLeafSpineRoutingProtocol::GetAddress(uint32_t interface, uint32_t addressIndex) const
    {
        return Ipv4InterfaceAddress(Ipv4Address(GetNode()->GetId()), Ipv4Mask::GetZero());
    }

    void ECMPLeafSpineRoutingProtocol::OnSend(Ptr<Packet> packet, Ipv4Address source, Ipv4Address destination, uint8_t protocol, Ptr<Ipv4Route> route)
    {
        ECMPLeafSpineHeader header;
        header.src = source.Get();
        header.dest = destination.Get();
        header.protocol = protocol;
        header.ltime = 0;

        packet->AddHeader(header);

        SendPacket(GetNode()->GetDevice(0), packet);

        //printf("at node %u, size %u\n", GetNode()->GetId(), packet->GetSize());

        if(m_logStart && GetNode()->GetId() < 144)
        {
            char file_name[1024]; 
            sprintf(file_name, "Node_%u_sendingRate.txt", GetNode()->GetId()); 
            LogManager::RegisterLog(50000 + GetNode()->GetId(), file_name);

            //LogSendingRate();
            m_logStart = false; 
        }

        m_sendingBytes += packet->GetSize(); 
    }

    void ECMPLeafSpineRoutingProtocol::OnReceive(Ptr<NetDevice> device, Ptr<const Packet> p, uint16_t protocol, const Address &from, const Address &to, NetDevice::PacketType packetType)
    {
        //printf("at node %u\n", GetNode()->GetId());

#if (TEST_FAILURE == 1)
        if(m_logStart && GetNode()->GetId() == m_serverPerLeafNum * m_leafSwitchNum) 
        {
            Simulator::Schedule(Time::FromDouble(0.05, Time::S), &ECMPLeafSpineRoutingProtocol::ChangeFailureDevId, this); 
            m_logStart = false; 
        }
#endif 
        //if(m_logStart && GetNode()->GetId() == m_serverPerLeafNum * m_leafSwitchNum + m_leafSwitchNum - 1) 
        // if(m_logStart && GetNode()->GetId() == m_serverPerLeafNum * m_leafSwitchNum) 
        // {
        //     //LogQueue(); 
        //     //m_logStart = false; 
        // }

        if(m_logStart && GetNode()->GetId() == m_serverPerLeafNum * m_leafSwitchNum) 
        {
            char file_name[1024]; 
            sprintf(file_name, "totalPath_%u_queue.txt", m_spineSwitchNum - 1); 
            LogManager::RegisterLog(1002, file_name);
            LogQueue(); 

            // sprintf(file_name, "totalPath_%u_pkt_drop.txt", m_spineSwitchNum - 1);  
            // LogManager::RegisterLog(1005, file_name);
            // LogPacketDrop(); 

            m_logStart = false; 
        }

        // //if(m_logStart && GetNode()->GetId() == m_serverPerLeafNum * m_leafSwitchNum + m_leafSwitchNum - 1) 
        // if(m_logStart && GetNode()->GetId() == m_serverPerLeafNum * m_leafSwitchNum)
        // {
        //     char file_name[1024];
        //     sprintf(file_name, "totalPath_%u_thruput_sndL_%u_rcvL_%u.txt", m_spineSwitchNum - 1, m_sndL, m_rcvL);
        //     LogManager::RegisterLog(1000, file_name);
            
        //     //LogThroughput();

        //     sprintf(file_name, "totalPath_%u_thruput_log_f2_probability_0.0_algo3_12us_thresh_10_fix_0.5_%uflows.txt", m_spineSwitchNum - 1, m_serverPerLeafNum);   
        //     LogManager::RegisterLog(1001, file_name);
        //     //LogThroughput2(); 

        //     m_logStart = false; 
        // }
        /* NOTICE: every packet will be bounced to Spine*/ 

        Ptr<Packet> packet = p->Copy();
        ECMPLeafSpineHeader header;
        packet->RemoveHeader(header);

        if (header.dest != GetNode()->GetId())
        {
            uint32_t devid = 0;
            // if (IsChild(header.dest) == true)
            if (header.ltime == 1)  //at spine switch, send down direction 
            {
                devid = header.dest / m_serverPerLeafNum; 
//printf("wsq1 spine down\n"); //wsq
            }
            else if (header.ltime == 2) //at leaf switch, send down direction 
            {
                devid = header.dest % m_serverPerLeafNum; 
//printf("wsq2 leaf down\n"); //wsq
            }
            else  //at leaf switch, up direction, choose according to flow header hash
            {
                if (m_type == ECMP_LeafSpine_TCP_FLOW) 
                {
                    devid = hash(header, packet); 
//printf("wsq3 leaf up\n"); //wsq
                }
                else
                { 
                    PathIdTag pidTag; 
                    if(!packet->PeekPacketTag(pidTag))  //at connection setup and teardown procedure, packets has no tag 
                    {
                        devid = m_serverPerLeafNum; //sent to the smallest path 
//printf("wsq4 connection\n"); //wsq
                    }
                    else
                    {
                        devid = hash_pathId(pidTag.pid, packet->GetSize());
//printf("wsq5 else\n"); //wsq
                        /* bursty flow all goes to one path */
                        //MpRDMAHeader l4H;
                        //packet->PeekHeader(l4H);
                        //if(packet->GetSize() > 1000 && l4H.GetDestinationPort() > 1001)
                        //{
                        //    devid = m_serverPerLeafNum + 1;
                        //}
                    } 
                } 

                //if(header.dest == 6) //flow 2 uses one path
                //{
                //    devid = m_serverPerLeafNum + m_spineSwitchNum - 1; 
                //}
            
                //devid = hash(header, packet); 
            }
#if (TEST_FAILURE == 1)
        if(failure_devid != 1000 && GetNode()->GetId() == m_serverPerLeafNum * m_leafSwitchNum && devid <= failure_devid && devid > m_serverPerLeafNum) 
        {
            printf("drop ======= \n"); 
            return; 
        }
#endif
            //if(GetNode()->GetId())
            //{
            //    MpRDMAHeader l4H; 
            //    packet->PeekHeader(l4H);

            //    if(l4H.GetDestinationPort() == 1435)
            //        printf("at node %u, dest port 1435, size %u\n", GetNode()->GetId(), p->GetSize()); 
            //} 

 /* wsq annotation  drop      
#if (TEST_DROP == 1)
            // drop packet randomly 
       if(GetNode()->GetId() == m_serverPerLeafNum * m_leafSwitchNum &&
             devid == m_serverPerLeafNum + 1)
         {
                MpRDMAHeader l4H; 
                packet->PeekHeader(l4H);

                double randNum = rand() % 1000000 / 1000000.0; //wsq annotation
                //double randNum = rand() % 1/ 1.0; //wsq add
                if( drop_seq.find(l4H.GetSequenceNumber().GetValue()) == drop_seq.end() && 
                    //drop_seq.find(l4H.GetAckNumber().GetValue()) == drop_seq.end() && 
                    randNum < DROP_RATE) //drop rate  
                {
                    drop_count++; 

                    printf(" ============= at node %u, drop %u packet, size %u, seq %u, IsReTx %u, dest %u\n", 
                            //GetNode()->GetId(), drop_count, p->GetSize(), l4H.GetSequenceNumber().GetValue()); 
                            GetNode()->GetId(), drop_count, p->GetSize(), l4H.GetSequenceNumber().GetValue(),
                            (drop_seq.find(l4H.GetSequenceNumber().GetValue()) != drop_seq.end()), 
                            header.dest); 

                    drop_seq[l4H.GetSequenceNumber().GetValue()] = 1; 
                    return; 
                }
          }
#endif 
 wsq annotation end*/

       
#if (TEST_DROP == 1)

           /* drop packet randomly */
//wsq       if(GetNode()->GetId() == m_serverPerLeafNum * m_leafSwitchNum &&
//wsq             devid == m_serverPerLeafNum + 1)
//wsq         {
      if(GetNode()->GetId()>= m_serverPerLeafNum*m_leafSwitchNum && GetNode()->GetId()< m_serverPerLeafNum*m_leafSwitchNum+0.5*m_leafSwitchNum
         && devid>m_serverPerLeafNum+1 )//wsq modify if

         {
                MpRDMAHeader l4H; 
                packet->PeekHeader(l4H);

                double randNum = rand() % 1000000 / 1000000.0; //wsq annotation
                //Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable> ();//wsq modify for random
                //double randNum=uv->GetInteger(0, 1000)/1000;//wsq modify for random
                //double randNum = rand() % 1000000/ 1000000.0; //wsq add
                if( drop_seq.find(l4H.GetSequenceNumber().GetValue()) == drop_seq.end() && 
                    //drop_seq.find(l4H.GetAckNumber().GetValue()) == drop_seq.end() && 
                    randNum < DROP_RATE) //drop rate  

                {
                    drop_count++; 

                    printf(" ============= at node %u, drop %u packet, size %u, seq %u, IsReTx %u, dest %u\n", 
                            //GetNode()->GetId(), drop_count, p->GetSize(), l4H.GetSequenceNumber().GetValue()); 
                            GetNode()->GetId(), drop_count, p->GetSize(), l4H.GetSequenceNumber().GetValue(),
                            (drop_seq.find(l4H.GetSequenceNumber().GetValue()) != drop_seq.end()), 
                            header.dest); 

                    drop_seq[l4H.GetSequenceNumber().GetValue()] = 1; 

                    return; 
                }

          }//wsq modify if end

#endif 




            if(GetNode()->GetId() == m_serverPerLeafNum * m_leafSwitchNum && header.ltime == 0 && devid > m_serverPerLeafNum)  
            //if(GetNode()->GetId() == m_serverPerLeafNum * m_leafSwitchNum + m_leafSwitchNum - 1 && header.ltime == 2 && device->GetIfIndex() > m_serverPerLeafNum)  
            {
                MpRDMAHeader l4H; 
                packet->PeekHeader(l4H); 

                if(l4H.GetDestinationPort() == 1001)  /* only record long flows */
                {
                    //perPathBytesCount[device->GetIfIndex() - m_serverPerLeafNum - 1] += packet->GetSize() - l4H.GetSerializedSize(); 
                    perPathBytesCount[devid - m_serverPerLeafNum - 1] += packet->GetSize() - l4H.GetSerializedSize(); 
                }
            }

            header.ltime = header.ltime + 1;
            packet->AddHeader(header);
            SendPacket(GetNode()->GetDevice(devid), packet);
        }
        else
        {
            //printf("at node %u, received \n", GetNode()->GetId()); 

            Ptr<IpL4Protocol> protocol = GetProtocol (header.protocol);

            Ipv4Header ipv4Header;
            ipv4Header.SetSource(Ipv4Address(header.src));
            ipv4Header.SetDestination(Ipv4Address(header.dest));

            Ptr<Ipv4Interface> ipv4Interface = Create<Ipv4Interface>();
            protocol->Receive(packet, ipv4Header, ipv4Interface);
        }
    }

    // bool ECMPLeafSpineRoutingProtocol::IsChild(uint32_t dest)
    // {
    //  uint32_t srv_num = m_n * m_n * m_n / 4;
    //  uint32_t pot_switch_num = m_n * m_n;

    //  uint32_t id = GetNode()->GetId();
    //  if (id < srv_num)
    //      return false;
    //  else if (id < srv_num + pot_switch_num / 2)
    //      return (id - srv_num) * m_n / 2 <= dest && dest < (id - srv_num + 1) * m_n / 2;
    //  else if (id < srv_num + pot_switch_num)
    //      return (id - srv_num - pot_switch_num / 2) / (m_n / 2) * m_n * m_n / 4 <= dest
    //          && dest < ((id - srv_num - pot_switch_num / 2) / (m_n / 2) + 1) * m_n * m_n / 4;
    //  else
    //      return true;
    // }

    uint16_t ECMPLeafSpineRoutingProtocol::hash(ECMPLeafSpineHeader& header, Ptr<Packet> packet)  
    {
        if (m_type == ECMP_LeafSpine_PACKET_LEVEL)
        {
            return rand();
        }

        FlowFiveTuple fiveTuple; 
        fiveTuple.srcAddr = header.src; 
        fiveTuple.dstAddr = header.dest; 
        fiveTuple.protocol = header.protocol; 

        uint8_t data[4];
        packet->CopyData (data, 4);
        
        fiveTuple.srcPort = 0;
        fiveTuple.srcPort |= data[0];
        fiveTuple.srcPort <<= 8;
        fiveTuple.srcPort |= data[1];
        
        fiveTuple.dstPort = 0;
        fiveTuple.dstPort |= data[2];
        fiveTuple.dstPort <<= 8;
        fiveTuple.dstPort |= data[3];

        if(m_hashTable.find(fiveTuple) == m_hashTable.end()) //seen a new flow 
        {
            m_hashTable[fiveTuple] = rand() % (m_spineSwitchNum - 1) + m_serverPerLeafNum + 1; 
        } 

        return m_hashTable[fiveTuple]; 
    }
    
    uint16_t ECMPLeafSpineRoutingProtocol::hash_pathId(uint32_t pathId, uint32_t pkt_size)   
    {
        if (m_type == ECMP_LeafSpine_PACKET_LEVEL)
        {
            return rand();
        }

        if(m_pathIdHashTable.find(pathId) == m_pathIdHashTable.end()) //seen a new pathId 
        {
            //m_pathIdHashTable[pathId] = rand() % m_spineSwitchNum + m_serverPerLeafNum; 
            
            // m_pathIdHashTable[pathId] =  m_serverPerLeafNum + 1;
    //wsq   //m_pathIdHashTable[pathId] = rand() % (m_spineSwitchNum - 1) + m_serverPerLeafNum + 1;  //the first link is for connection setup/teardown packets
//wsq
             m_pathIdHashTable[pathId] = pathId % (m_spineSwitchNum - 1) + m_serverPerLeafNum + 1;  //the first link is for connection setup/teardown packets
            // fprintf(stderr,"at node %u, ECMP: %u-->%u\n",GetNode()->GetId(),pathId,m_pathIdHashTable[pathId]);
            
            ///* random */
            //if(pkt_size > 100)
            //{
            //    m_pathIdHashTable[pathId] = rand() % (m_spineSwitchNum - 1) + m_serverPerLeafNum + 1;  //the first link is for connection setup/teardown packets
            //    //m_pathIdHashTable[pathId] = pathId % (m_spineSwitchNum - 1) + m_serverPerLeafNum + 1;  //the first link is for connection setup/teardown packets
            //}
            ///* round-robin */
            //else
            //{
            //    m_pathIdHashTable[pathId] = pathId % (m_spineSwitchNum - 1) + m_serverPerLeafNum + 1;  //the first link is for connection setup/teardown packets

            //    while(m_pathIdHashTable[pathId] == m_serverPerLeafNum + 1)
            //    {
            //        m_pathIdHashTable[pathId] = random() % (m_spineSwitchNum - 1) + m_serverPerLeafNum + 1;  //the first link is for connection setup/teardown packets
            //    }
            //}

        } 

#if (TEST_FAILURE == 1)
        //m_pathIdHashTable[pathId] = rand() % (m_spineSwitchNum - 1) + m_serverPerLeafNum + 1; 
        //while(m_pathIdHashTable[pathId] <= failure_devid && failure_devid < 1000)
        while(m_pathIdHashTable[pathId] <= recover_failure_devid && recover_failure_devid < 1000) 
        {
            m_pathIdHashTable[pathId] = rand() % (m_spineSwitchNum - 1) + m_serverPerLeafNum + 1;  
        }
#endif

        return m_pathIdHashTable[pathId]; 
    }
    
    void ECMPLeafSpineRoutingProtocol::LogThroughput() 
    {
        LogManager::WriteLog(1000, "%lu ", Simulator::Now().GetMicroSeconds() / 1000);  //in ms

        for(uint32_t i = 0; i < m_spineSwitchNum - 1; i++)
        {
            LogManager::WriteLog(1000, "%lf ", (double)perPathBytesCount[i] * 8.0 / m_logThroughputInterval / 1000000.0); //in Mbps 
//wsq 
     //       printf("%lf ", (double)perPathBytesCount[i] * 8.0 / m_logThroughputInterval / 1000000.0); //in Mbps 
        }
        LogManager::WriteLog(1000, "\n");  
        printf("time %lu \n", Simulator::Now().GetMicroSeconds());  

        for(uint32_t i = 0; i < m_spineSwitchNum - 1; i++)
        {
            perPathBytesCount[i] = 0; 
        }

        Simulator::Schedule(Time::FromDouble(m_logThroughputInterval, Time::S), &ECMPLeafSpineRoutingProtocol::LogThroughput, this); 
    } 
    
    void ECMPLeafSpineRoutingProtocol::LogThroughput2() 
    {
        for(uint32_t i = 0; i < m_spineSwitchNum - 1; i++)
        {
            LogManager::WriteLog(1001, "%lf ", (double)flow2PerPathBytesCount[i] * 8.0 / m_logThroughputInterval / 1000000.0); //in Mbps  
            //printf("%lf ", (double)flow2PerPathBytesCount[i] * 8.0 / m_logThroughputInterval / 1000000.0); //in Mbps 
        }
        LogManager::WriteLog(1001, "\n");  
        //printf("time %lu \n", Simulator::Now().GetMicroSeconds());  

        for(uint32_t i = 0; i < m_spineSwitchNum - 1; i++)
        {
            flow2PerPathBytesCount[i] = 0; 
        }

        Simulator::Schedule(Time::FromDouble(m_logThroughputInterval, Time::S), &ECMPLeafSpineRoutingProtocol::LogThroughput2, this); 
    }

    void ECMPLeafSpineRoutingProtocol::LogQueue()
    {
        /* log queue size */

        for(uint32_t i = 1; i <= m_spineSwitchNum - 1; i++)
        {
           uint32_t qSize = DynamicCast<PointToPointNetDevice> (GetNode()->GetDevice(i + m_serverPerLeafNum))->GetQueue()->GetNPackets();  
           LogManager::WriteLog(1002, "%u ", qSize); 

           //printf("%u ", qSize); 
        } 
//wsq
     //   printf("time %lu \n", Simulator::Now().GetMicroSeconds());  

        LogManager::WriteLog(1002, "\n"); 
        
        // uint32_t qSize = DynamicCast<PointToPointNetDevice> (GetNode()->GetDevice(m_serverPerLeafNum + 1))->GetQueue()->GetNPackets(); 
        // uint32_t maxQueue = DynamicCast<PointToPointNetDevice> (GetNode()->GetDevice(m_serverPerLeafNum + 1))->GetQueue()->GetMaxPackets(); 
        // LogManager::WriteLog(3000, "%u, %u\n", qSize, maxQueue); 

        Simulator::Schedule(Time::FromDouble(0.0001, Time::S), &ECMPLeafSpineRoutingProtocol::LogQueue, this); 
    }
    
    void ECMPLeafSpineRoutingProtocol::LogPacketDrop()
    {
        /* log packet drops */

        for(uint32_t i = 0; i <= m_spineSwitchNum - 1; i++)
        {
            uint32_t pkt_drop = DynamicCast<PointToPointNetDevice> (GetNode()->GetDevice(i + m_serverPerLeafNum))->GetQueue()->GetTotalDroppedPackets();  
            LogManager::WriteLog(1005, "%u ", pkt_drop); 

            printf("%u ", pkt_drop); 
        } 
//wsq
     //   printf("time %lu \n", Simulator::Now().GetMicroSeconds());  

        LogManager::WriteLog(1005, "\n"); 

        Simulator::Schedule(Time::FromDouble(0.0001, Time::S), &ECMPLeafSpineRoutingProtocol::LogPacketDrop, this); 
    }

    void ECMPLeafSpineRoutingProtocol::LogSendingRate() 
    {
        uint32_t qSize = DynamicCast<PointToPointNetDevice> (GetNode()->GetDevice(0))->GetQueue()->GetNPackets(); 
        LogManager::WriteLog(50000 + GetNode()->GetId(), "%u\n", qSize); 

        //LogManager::WriteLog(50000 + GetNode()->GetId(), "%lf\n", (double)m_sendingBytes * 8.0 / m_logThroughputInterval / 1000000.0); //in Mbps  

        m_sendingBytes = 0; 

        //Simulator::Schedule(Time::FromDouble(m_logThroughputInterval, Time::S), &ECMPLeafSpineRoutingProtocol::LogSendingRate, this); 
        Simulator::Schedule(Time::FromDouble(0.0001, Time::S), &ECMPLeafSpineRoutingProtocol::LogSendingRate, this); 
    }
    
    void ECMPLeafSpineRoutingProtocol::ChangeFailureDevId() 
    {
        printf("at node %u, change failure, time %lf\n", GetNode()->GetId(), Simulator::Now().GetSeconds());

        if(Simulator::Now().GetSeconds() < 0.055)
            failure_devid = 9;
        else if(Simulator::Now().GetSeconds() < 0.105)
            failure_devid = 10; 
        else if(Simulator::Now().GetSeconds() < 0.155)
            failure_devid = 11; 
        else if(Simulator::Now().GetSeconds() < 0.205)
            failure_devid = 10; 
        else if(Simulator::Now().GetSeconds() < 0.255)
            failure_devid = 9;
        else if(Simulator::Now().GetSeconds() < 0.305)
            failure_devid = 1000; 
        
        Simulator::Schedule(MicroSeconds(10), &ECMPLeafSpineRoutingProtocol::RecoverFailureDevId, this);
        //failure_devid = (failure_devid + 1) % (m_spineSwitchNum - 2) + m_serverPerLeafNum; 
        Simulator::Schedule(Time::FromDouble(0.05, Time::S), &ECMPLeafSpineRoutingProtocol::ChangeFailureDevId, this); 

        //RecoverFailureDevId(); 
    }
    void ECMPLeafSpineRoutingProtocol::RecoverFailureDevId() 
    {
        recover_failure_devid = failure_devid; 
    }
}
