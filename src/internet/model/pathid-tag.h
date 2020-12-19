#ifndef __PATH_ID_TAG_H__
#define __PATH_ID_TAG_H__

#include "ns3/packet.h"
#include "ns3/tag.h"

namespace ns3 {

    class Tag;

    class PathIdTag : public Tag
    {
      public:
        static TypeId GetTypeId (void);
        virtual TypeId GetInstanceTypeId (void) const;

        PathIdTag ();

        uint32_t GetSerializedSize (void) const;
        void Serialize (TagBuffer i) const;
        void Deserialize (TagBuffer i);
        void Print (std::ostream &os) const;
    
        uint32_t pid;

        uint32_t conState; //0 for SlowStart, 1 for CA 
    };

}

#endif
