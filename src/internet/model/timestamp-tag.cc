#include "timestamp-tag.h"
#include "ns3/uinteger.h"

namespace ns3 {

    NS_OBJECT_ENSURE_REGISTERED (TimeStampTag);

    TypeId
    TimeStampTag::GetTypeId (void)
    {
        static TypeId tid = TypeId ("ns3::TimeStampTag")
            .SetParent<Tag> ()
            .AddConstructor<TimeStampTag> ()
            ;
        return tid;
    }

    TypeId
    TimeStampTag::GetInstanceTypeId (void) const
    {
        return GetTypeId ();
    }

    TimeStampTag::TimeStampTag ()
        : timeStamp (0), 
          rWndTS (0) 
    {
    }

    uint32_t
    TimeStampTag::GetSerializedSize (void) const
    {
        return 8 * 2;  
    }

    void
    TimeStampTag::Serialize (TagBuffer i) const
    {
        i.WriteDouble (timeStamp); 
        i.WriteDouble (rWndTS); 
    }

    void
    TimeStampTag::Deserialize (TagBuffer i) 
    {
        timeStamp = i.ReadDouble ();
        rWndTS = i.ReadDouble (); 
    }

    void
    TimeStampTag::Print (std::ostream &os) const
    {
    }

} //namespace ns3
