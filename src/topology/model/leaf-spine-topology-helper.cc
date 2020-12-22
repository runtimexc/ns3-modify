#include "leaf-spine-topology-helper.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/string.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-list-routing-helper.h"
#include "ns3/ipv4-nix-vector-helper.h"
#include "ns3/net-routing.h"
#include "ns3/uinteger.h"
#include "ns3/double.h" 
#include "ns3/drop-tail-queue.h"
#include "ns3/data-rate.h"
#include "ns3/enum.h"

namespace ns3
{
    TypeId LeafSpineTopologyHelper::GetTypeId (void)
    {
        static TypeId tid = TypeId ("ns3::LeafSpineTopologyHelper")
            .SetParent<TopologyHelper>()
            .AddConstructor<LeafSpineTopologyHelper>()
            .AddAttribute("ServerPerLeaf", "The number of servers directly connected by a leaf switch. ",
                          UintegerValue(4), MakeUintegerAccessor(&LeafSpineTopologyHelper::m_serverPerLeaf), MakeUintegerChecker<uint32_t>())
            .AddAttribute("LeafSwitchNum", "The number of leaf switches. ",
                          UintegerValue(2), MakeUintegerAccessor(&LeafSpineTopologyHelper::m_leafSwitchNum), MakeUintegerChecker<uint32_t>())
            .AddAttribute("SpineSwitchNum", "The number of spine switches. ", 
                          UintegerValue(4), MakeUintegerAccessor(&LeafSpineTopologyHelper::m_spineSwitchNum), MakeUintegerChecker<uint32_t>())  
            .AddAttribute("ServerToLeafRate", "The rate of server to leaf link. ", 
                          StringValue("10Gbps"), MakeStringAccessor(&LeafSpineTopologyHelper::m_serverToLeafRate), MakeStringChecker())  
            .AddAttribute("LeafToSpineRate1", "The 1 rate of leaf to spine link. ", 
                          StringValue("10Gbps"), MakeStringAccessor(&LeafSpineTopologyHelper::m_leafToSpineRate1), MakeStringChecker())  
            .AddAttribute("LeafToSpineRate2", "The 2 rate of leaf to spine link. ", 
                          StringValue("5Gbps"), MakeStringAccessor(&LeafSpineTopologyHelper::m_leafToSpineRate2), MakeStringChecker())  
            .AddAttribute("ServerToLeafECNThreshold", "The ECN threshold of server to leaf link. ", 
                          UintegerValue(10), MakeUintegerAccessor(&LeafSpineTopologyHelper::m_serverToLeafECNThresh), MakeUintegerChecker<uint32_t>())  
            .AddAttribute("LeafToSpineECNThreshold1", "The 1 ECN threshold of leaf to spine link. ", 
                          UintegerValue(10), MakeUintegerAccessor(&LeafSpineTopologyHelper::m_leafToSpineECNThresh1), MakeUintegerChecker<uint32_t>())  
            .AddAttribute("LeafToSpineECNThreshold2", "The 2 ECN threshold of leaf to spine link. ",  
                          UintegerValue(5), MakeUintegerAccessor(&LeafSpineTopologyHelper::m_leafToSpineECNThresh2), MakeUintegerChecker<uint32_t>())  
            .AddAttribute("ServerToLeafDelay", "The delay of server to leaf link. ", 
                          DoubleValue(1.5), MakeDoubleAccessor(&LeafSpineTopologyHelper::m_serverToLeafDelay), MakeDoubleChecker<double>())  
            .AddAttribute("LeafToSpineDelay", "The delay of leaf to spine link. ",  
                          DoubleValue(1.5), MakeDoubleAccessor(&LeafSpineTopologyHelper::m_leafToSpineDelay), MakeDoubleChecker<double>())
            .AddAttribute("Diff", "The diff between path",
                          UintegerValue(1),MakeUintegerAccessor(&LeafSpineTopologyHelper::m_diff), MakeUintegerChecker<uint32_t>())  
            .AddAttribute("VaryCapacity", "To vary the capacity of leaf to spine link or not. ", 
                          BooleanValue(false), MakeBooleanAccessor(&LeafSpineTopologyHelper::m_varyCapacity), MakeBooleanChecker());   
            
        return tid;
    }

    TypeId LeafSpineTopologyHelper::GetInstanceTypeId (void) const
    {
        return GetTypeId();
    }
    
    LeafSpineTopologyHelper::LeafSpineTopologyHelper ()
    {}

    LeafSpineTopologyHelper::~LeafSpineTopologyHelper ()
    {}

    void LeafSpineTopologyHelper::CreateTopology()
    {
        //printf("topology: ServerPerLeaf: %u, LeafSwitch: %u, SpineSwitch: %u\n", 
        //        m_serverPerLeaf, m_leafSwitchNum, m_spineSwitchNum); 

        int srv_num = m_serverPerLeaf * m_leafSwitchNum; 

        terminalNodes.Create(srv_num);
        leafSwitchNodes.Create(m_leafSwitchNum);
        spineSwitchNodes.Create(m_spineSwitchNum); 

        PointToPointHelper serverToLeafLinkHelper; 
        serverToLeafLinkHelper.SetDeviceAttribute ("DataRate", StringValue (m_serverToLeafRate)); 
        serverToLeafLinkHelper.SetChannelAttribute ("Delay", TimeValue (MicroSeconds (m_serverToLeafDelay))); 
        serverToLeafLinkHelper.SetDeviceAttribute ("ECNThresh", UintegerValue (m_serverToLeafECNThresh)); 
        serverToLeafLinkHelper.SetDeviceAttribute ("TerminalNum", UintegerValue (srv_num)); 
        
        PointToPointHelper leafToSpineLinkHelper1;
        leafToSpineLinkHelper1.SetDeviceAttribute ("DataRate", StringValue (m_leafToSpineRate1)); 
        leafToSpineLinkHelper1.SetChannelAttribute ("Delay", TimeValue (MicroSeconds (m_leafToSpineDelay))); 
        leafToSpineLinkHelper1.SetDeviceAttribute ("ECNThresh", UintegerValue (m_leafToSpineECNThresh1)); 
        leafToSpineLinkHelper1.SetDeviceAttribute ("TerminalNum", UintegerValue (srv_num)); 
        
        PointToPointHelper leafToSpineLinkHelper2;
        leafToSpineLinkHelper2.SetDeviceAttribute ("DataRate", StringValue (m_leafToSpineRate2)); 
        leafToSpineLinkHelper2.SetChannelAttribute ("Delay", TimeValue (MicroSeconds (m_leafToSpineDelay*m_diff))); 
        leafToSpineLinkHelper2.SetDeviceAttribute ("ECNThresh", UintegerValue (m_leafToSpineECNThresh2)); 
        leafToSpineLinkHelper2.SetDeviceAttribute ("TerminalNum", UintegerValue (srv_num));

        // create topology, link server to leaf 
        for (int i = 0; i < srv_num; i++)
        {
            int from = i;
            int to = i / m_serverPerLeaf; 

            NetDeviceContainer link = serverToLeafLinkHelper.Install(NodeContainer(terminalNodes.Get(from), leafSwitchNodes.Get(to)));
            terminalDevices.Add(link.Get(0));
            switchDevices.Add(link.Get(1));
//wsq
//std::cout<<"p2p: " << terminalNodes.Get(from)->GetId()<<" --- "<< leafSwitchNodes.Get(to)->GetId() <<" ChannelId: "<<link.Get(0)->GetChannel()->GetId()<<" "<<link.Get(1)->GetChannel()->GetId()<<std::endl;
        }

        // create topology, link leaf to spine 
        for (uint32_t i = 0; i < m_leafSwitchNum; i++) 
        {
            for (uint32_t j = 0; j < m_spineSwitchNum; j++)
            {
                int from = i;
                int to = j; 

                NetDeviceContainer link;

                if(!m_varyCapacity)
                {
                    link = leafToSpineLinkHelper1.Install(NodeContainer(leafSwitchNodes.Get(from), spineSwitchNodes.Get(to))); 
                }
                else
                {
                    if (j % 2 == 0) 
                        link = leafToSpineLinkHelper1.Install(NodeContainer(leafSwitchNodes.Get(from), spineSwitchNodes.Get(to))); 
                    else if (j % 2 == 1) 
                        link = leafToSpineLinkHelper2.Install(NodeContainer(leafSwitchNodes.Get(from), spineSwitchNodes.Get(to))); 
                } 
                switchDevices.Add(link.Get(0));
                switchDevices.Add(link.Get(1));
//wsq
//std::cout<<"p2p: " << leafSwitchNodes.Get(from)->GetId()<<" --- "<< spineSwitchNodes.Get(to)->GetId() <<" ChannelId: "<<link.Get(0)->GetChannel()->GetId()<<" "<<link.Get(1)->GetChannel()->GetId()<<std::endl;
            }
        }
    }

    Ptr<Node> LeafSpineTopologyHelper::GetNode(uint32_t i) const
    {
        if (i < terminalNodes.GetN())
            return terminalNodes.Get(i);
        i -= terminalNodes.GetN();

        if (i < leafSwitchNodes.GetN())
            return leafSwitchNodes.Get(i);
        i -= leafSwitchNodes.GetN();

        if (i < spineSwitchNodes.GetN())
            return spineSwitchNodes.Get(i); 

        return NULL;
    }

    uint32_t LeafSpineTopologyHelper::GetNTerminalNodes() const
    {
        return terminalNodes.GetN();
    }

    Ptr<Node> LeafSpineTopologyHelper::GetTerminalNode(int i) const
    {
        return terminalNodes.Get(i);
    }

    Ipv4Address LeafSpineTopologyHelper::GetTerminalInterface(int i) const
    {
        return Ipv4Address(i);
    }

    NodeContainer LeafSpineTopologyHelper::GetNodes() const
    {
        return NodeContainer(terminalNodes, leafSwitchNodes, spineSwitchNodes);
    }

    NetDeviceContainer LeafSpineTopologyHelper::GetNetDevices() const
    {
        return NetDeviceContainer(terminalDevices, switchDevices); 
    }
}
