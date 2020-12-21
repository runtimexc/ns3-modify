#ifndef __LEAF_SPINE_TOPOLOGY_HELPER__
#define __LEAF_SPINE_TOPOLOGY_HELPER__

#include "ns3/node-container.h"
#include "ns3/net-device-container.h"
#include "ns3/topology-helper.h"

namespace ns3
{
    class LeafSpineTopologyHelper : public TopologyHelper
    {
        NodeContainer terminalNodes;
        NodeContainer leafSwitchNodes;
        NodeContainer spineSwitchNodes;

        NetDeviceContainer terminalDevices;
        NetDeviceContainer switchDevices;

        uint32_t m_serverPerLeaf; 
        uint32_t m_leafSwitchNum;
        uint32_t m_spineSwitchNum;

        std::string m_serverToLeafRate; 
        std::string m_leafToSpineRate1; 
        std::string m_leafToSpineRate2; 

        uint32_t m_serverToLeafECNThresh; 
        uint32_t m_leafToSpineECNThresh1; 
        uint32_t m_leafToSpineECNThresh2; 

        double m_serverToLeafDelay; 
        double m_leafToSpineDelay; 

        bool m_varyCapacity; 

    public:
        static TypeId GetTypeId (void);
        virtual TypeId GetInstanceTypeId (void) const;
        
        LeafSpineTopologyHelper ();
        ~LeafSpineTopologyHelper ();

        virtual void CreateTopology();

        uint32_t GetNTerminalNodes() const;
        Ptr<Node> GetTerminalNode(int i) const;
        Ptr<Node> GetNode(uint32_t i) const;
        Ipv4Address GetTerminalInterface(int i) const;      

        virtual NodeContainer GetNodes() const;
        virtual NetDeviceContainer GetNetDevices() const;

        virtual NodeContainer GetTerminalNodes() const { return terminalNodes; }
        virtual NodeContainer GetNonTerminalNodes() const { return NodeContainer(leafSwitchNodes, spineSwitchNodes); }

        NodeContainer GetLeafSwitchNodes() const { return leafSwitchNodes; }
        NodeContainer GetSpineSwitchNodes() const { return spineSwitchNodes; }
    };
}

#endif
