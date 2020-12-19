#include "aack-tag.h"
#include "ns3/uinteger.h"

namespace ns3 {

    NS_OBJECT_ENSURE_REGISTERED (AAckTag);

    TypeId
    AAckTag::GetTypeId (void)
    {
        static TypeId tid = TypeId ("ns3::AAckTag")
            .SetParent<Tag> ()
            .AddConstructor<AAckTag> ()
            ;
        return tid;
    }

    TypeId
    AAckTag::GetInstanceTypeId (void) const
    {
        return GetTypeId ();
    }

    AAckTag::AAckTag ()
        : aackSeq (0), maxSeq(0), pathId(0), nack(0) 
    {
    }

    uint32_t
    AAckTag::GetSerializedSize (void) const
    {
        return 4 * 4;
    }

    void
    AAckTag::Serialize (TagBuffer i) const
    {
        i.WriteU32 (aackSeq);
        i.WriteU32 (maxSeq);
        i.WriteU32 (pathId);
        i.WriteU32 (nack);
    }

    void
    AAckTag::Deserialize (TagBuffer i) 
    {
        aackSeq = i.ReadU32 ();
        maxSeq = i.ReadU32 ();
        pathId = i.ReadU32 ();
        nack = i.ReadU32 ();
    }

    void
    AAckTag::Print (std::ostream &os) const 
    {
    }

} //namespace ns3
