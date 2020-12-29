# What did we change?
**mp-rdma-socket-impl.h**:

add new variables for new function

    bool m_sendretx;
    SequenceNumber32 m_detect;
    SequenceNumber32 m_High_resend_pos;
    uint32_t retx_thresold;
    SequenceNumber32 m_oversendretx;
    SequenceNumber32 m_startsendretx;
**mp-rdma-socket-impl.cc**:

* initialize above variables and add attribute 
* add resend in sender

**ecmp-leaf-spine-routing-protocol.cc**

* add : no drop for retransmit pkt

**run.py**

* modify for loop to test and add new variables control

**mp_rdma_leaf_spine.cc**

* get new variables from ***run.py*** by cmd and pass it to ***mp-rdma-socket-impl.cc***  
