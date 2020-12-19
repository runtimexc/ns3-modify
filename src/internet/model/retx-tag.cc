#include "retx-tag.h" 
#include "ns3/uinteger.h"

namespace ns3 {

    NS_OBJECT_ENSURE_REGISTERED (ReTxTag);

    TypeId
    ReTxTag::GetTypeId (void)
    {
        static TypeId tid = TypeId ("ns3::ReTxTag")
            .SetParent<Tag> ()
            .AddConstructor<ReTxTag> ()
            ;
        return tid;
    }

    TypeId
    ReTxTag::GetInstanceTypeId (void) const
    {
        return GetTypeId ();
    }

    ReTxTag::ReTxTag ()
        : isReTx (0) 
    {
    }

    uint32_t
    ReTxTag::GetSerializedSize (void) const
    {
        return 4; 
    }

    void
    ReTxTag::Serialize (TagBuffer i) const
    {
        i.WriteU32 (isReTx); 
    }

    void
    ReTxTag::Deserialize (TagBuffer i) 
    {
        isReTx = i.ReadU32 ();
    }

    void
    ReTxTag::Print (std::ostream &os) const
    {
    }

} //namespace ns3
