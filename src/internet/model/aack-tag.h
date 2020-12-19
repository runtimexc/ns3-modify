#ifndef __AACK_TAG_H__
#define __AACK_TAG_H__

#include "ns3/packet.h"
#include "ns3/tag.h"

namespace ns3 {

    class Tag;

    class AAckTag : public Tag
    {
      public:
        static TypeId GetTypeId (void);
        virtual TypeId GetInstanceTypeId (void) const;

        AAckTag ();

        uint32_t GetSerializedSize (void) const;
        void Serialize (TagBuffer i) const;
        void Deserialize (TagBuffer i);
        void Print (std::ostream &os) const;
    
        uint32_t aackSeq; 
        uint32_t maxSeq;
        uint32_t pathId;
        uint32_t nack;
    };

}

#endif
