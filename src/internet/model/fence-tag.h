#ifndef __FENCE_TAG_H__
#define __FENCE_TAG_H__

#include "ns3/packet.h"
#include "ns3/tag.h"

namespace ns3 {

    class Tag;

    class FenceTag : public Tag
    {
      public:
        static TypeId GetTypeId (void);
        virtual TypeId GetInstanceTypeId (void) const;

        FenceTag (); 

        uint32_t GetSerializedSize (void) const;
        void Serialize (TagBuffer i) const;
        void Deserialize (TagBuffer i);
        void Print (std::ostream &os) const;
    
        uint32_t fence;
    };

}

#endif
