#include "fence-tag.h" 
#include "ns3/uinteger.h"

namespace ns3 {

    NS_OBJECT_ENSURE_REGISTERED (FenceTag);

    TypeId
    FenceTag::GetTypeId (void)
    {
        static TypeId tid = TypeId ("ns3::FenceTag")
            .SetParent<Tag> ()
            .AddConstructor<FenceTag> ()
            ;
        return tid;
    }

    TypeId
    FenceTag::GetInstanceTypeId (void) const
    {
        return GetTypeId ();
    }

    FenceTag::FenceTag ()
        : fence (0) 
    {
    }

    uint32_t
    FenceTag::GetSerializedSize (void) const
    {
        return 4; 
    }

    void
    FenceTag::Serialize (TagBuffer i) const
    {
        i.WriteU32 (fence); 
    }

    void
    FenceTag::Deserialize (TagBuffer i) 
    {
        fence = i.ReadU32 ();
    }

    void
    FenceTag::Print (std::ostream &os) const
    {
    }

} //namespace ns3
