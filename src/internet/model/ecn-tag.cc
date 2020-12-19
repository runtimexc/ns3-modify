#include "ecn-tag.h"
#include "ns3/uinteger.h"

namespace ns3 {

    NS_OBJECT_ENSURE_REGISTERED (EcnTag);

    TypeId
    EcnTag::GetTypeId (void)
    {
        static TypeId tid = TypeId ("ns3::EcnTag")
            .SetParent<Tag> ()
            .AddConstructor<EcnTag> ()
            ;
        return tid;
    }

    TypeId
    EcnTag::GetInstanceTypeId (void) const
    {
        return GetTypeId ();
    }

    EcnTag::EcnTag ()
        : ip_bit_1 (false),
          ip_bit_2 (false),
          tcp_bit_1 (false),
          tcp_bit_2 (false)
    {
    }

    uint32_t
    EcnTag::GetSerializedSize (void) const
    {
        // 4 bits as 1 byte
        return 1;
    }

    void
    EcnTag::Serialize (TagBuffer i) const
    {
        uint8_t total = 0;
        if( ip_bit_1 )
            total += 8;
        if( ip_bit_2 )
            total += 4;
        if( tcp_bit_1 )
            total += 2;
        if( tcp_bit_2 )
            total += 1;
            
        i.WriteU8 (total);
    }

    void
    EcnTag::Deserialize (TagBuffer i)
    {
        uint8_t total = i.ReadU8 ();
        if( total / 8 > 0 )
        {
            ip_bit_1 = true;
            total = total % 8;
        }
        if( total / 4 > 0 )
        {
            ip_bit_2 = true;
            total = total % 4;
        }
        if( total / 2 > 0 )
        {
            tcp_bit_1 = true;
            total = total % 2;
        }
        if( total > 0 )
            tcp_bit_2 = true;
    }

    void
    EcnTag::Print (std::ostream &os) const
    {
    }

} //namespace ns3
