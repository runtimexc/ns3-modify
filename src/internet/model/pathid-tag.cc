#include "pathid-tag.h"
#include "ns3/uinteger.h"

namespace ns3 {

    NS_OBJECT_ENSURE_REGISTERED (PathIdTag);

    TypeId
    PathIdTag::GetTypeId (void)
    {
        static TypeId tid = TypeId ("ns3::PathIdTag")
            .SetParent<Tag> ()
            .AddConstructor<PathIdTag> ()
            ;
        return tid;
    }

    TypeId
    PathIdTag::GetInstanceTypeId (void) const
    {
        return GetTypeId ();
    }

    PathIdTag::PathIdTag ()
        : pid (0),
          conState (0) 
    {
    }

    uint32_t
    PathIdTag::GetSerializedSize (void) const
    {
        return 8; 
    }

    void
    PathIdTag::Serialize (TagBuffer i) const
    {
        i.WriteU32 (pid); 
        i.WriteU32 (conState); 
    }

    void
    PathIdTag::Deserialize (TagBuffer i) 
    {
        pid = i.ReadU32 ();
        conState = i.ReadU32 ();
    }

    void
    PathIdTag::Print (std::ostream &os) const
    {
    }

} //namespace ns3
