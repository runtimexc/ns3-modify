#ifndef __ECN_TAG_H__
#define __ECN_TAG_H__

#include "ns3/packet.h"
#include "ns3/tag.h"

namespace ns3 {

    class Tag;

    class EcnTag : public Tag
    {
      public:
        static TypeId GetTypeId (void);
        virtual TypeId GetInstanceTypeId (void) const;

        EcnTag ();

        uint32_t GetSerializedSize (void) const;
        void Serialize (TagBuffer i) const;
        void Deserialize (TagBuffer i);
        void Print (std::ostream &os) const;
        
        // Ipv4 Tos bits 6~7
        bool ip_bit_1;
        bool ip_bit_2;

        // TCP ECN bits
        bool tcp_bit_1;    // CWR bit
        bool tcp_bit_2;    // ECE bit
    };

}

#endif
