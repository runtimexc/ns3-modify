#include <string>
#include <fstream>
#include "ns3/core-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/network-module.h"
#include "ns3/topology-module.h"
#include "ns3/net-routing-module.h"
#include "ns3/log-manager.h" 
#include <time.h>
#include <stdio.h>
#include <iomanip>
#include "sys/stat.h" 

using namespace ns3; 

//uint32_t maxBytes = 100000; //100MB
uint32_t maxBytes = 0; //infinity 
double stopTime = 1;//wsq modify 0.5 to 0.1
//double stopTime = 30; 
uint32_t flowNum = 1;
uint32_t finished_flow_num = 0;
double filterTime = 0;
std::vector<Ptr<PacketSink> > psinks;
std::vector<Ptr<BulkSendApplication> > pBulkSends;
std::vector<Ptr<UdpServer> > udpServers;

void ResetRx() 
{
    for(uint32_t i = 0; i < psinks.size(); i++)
    {
        Ptr<PacketSink> sink = psinks[i]; 
        sink->ClearRxCounter(); 
    }
}

void SetTerminalQueue(uint32_t term_num)
{
    /* Set terminal nodes' queue to be large enough so that no packet will drop at terminals */ 
    for(uint32_t i = 0; i < term_num; i++) 
    {
        char path_name[1024]; 
        sprintf(path_name, "/NodeList/%u/DeviceList/0/TxQueue/MaxPackets", i); 
        std::string str(path_name); 
        Config::Set (str, UintegerValue (1000000));  
        //Config::Set ("/NodeList/0/DeviceList/0/TxQueue/MaxPackets", UintegerValue (1000000)); 
    }
}

void SetLinkDelay(double link_delay, uint32_t index)
{
    char path_name[1024];

    sprintf(path_name, "/ChannelList/%u/Delay", index + 17);
    std::string str(path_name); 
    Config::Set (str, TimeValue (MicroSeconds(link_delay)));
}




void SetLinkRate(std::string rate, uint32_t index)
{
    char path_name[1024];

    sprintf(path_name, "/NodeList/320/DeviceList/%u/DataRate", index + 11);
    std::string str(path_name); 
    Config::Set (str, StringValue (rate));
}




void SetLinkBandwidth()
{
    char path_name[1024];

    sprintf(path_name, "/NodeList/16/DeviceList/9/DataRate");
    std::string str(path_name);
    Config::Set (str, StringValue ("5Gbps"));
    
    sprintf(path_name, "/NodeList/16/DeviceList/10/DataRate");
    std::string str1(path_name);
    Config::Set (str1, StringValue ("10Gbps"));
    
    sprintf(path_name, "/NodeList/16/DeviceList/11/DataRate");
    std::string str2(path_name);
    Config::Set (str2, StringValue ("20Gbps")); 
    
    sprintf(path_name, "/NodeList/16/DeviceList/9/ECNThresh");
    std::string str3(path_name);
    Config::Set (str3, UintegerValue (7));
    
    sprintf(path_name, "/NodeList/16/DeviceList/10/ECNThresh");
    std::string str4(path_name);
    Config::Set (str4, UintegerValue (14));
    
    sprintf(path_name, "/NodeList/16/DeviceList/11/ECNThresh");
    std::string str5(path_name);
    Config::Set (str5, UintegerValue (27));
}

void SetREDParameters()
{
    char path_name[1024];
    
    /* Link 1 */
    sprintf(path_name, "/NodeList/320/DeviceList/11/RED_Kmin");
    std::string str11(path_name);
    sprintf(path_name, "/NodeList/320/DeviceList/11/RED_Kmax");
    std::string str12(path_name);
    sprintf(path_name, "/NodeList/320/DeviceList/11/RED_Pmax");
    std::string str13(path_name);
    Config::Set (str11, UintegerValue (20));
    Config::Set (str12, UintegerValue (200));
    Config::Set (str13, DoubleValue (0.8));

    /* Link 2 */
    sprintf(path_name, "/NodeList/320/DeviceList/12/RED_Kmin");
    std::string str21(path_name);
    sprintf(path_name, "/NodeList/320/DeviceList/12/RED_Kmax");
    std::string str22(path_name);
    sprintf(path_name, "/NodeList/320/DeviceList/12/RED_Pmax");
    std::string str23(path_name);
    Config::Set (str21, UintegerValue (20));
    Config::Set (str22, UintegerValue (200));
    Config::Set (str23, DoubleValue (0.8));

    /* Link 3 */
    sprintf(path_name, "/NodeList/320/DeviceList/13/RED_Kmin");
    std::string str31(path_name);
    sprintf(path_name, "/NodeList/320/DeviceList/13/RED_Kmax");
    std::string str32(path_name);
    sprintf(path_name, "/NodeList/320/DeviceList/13/RED_Pmax");
    std::string str33(path_name);
    Config::Set (str31, UintegerValue (20));
    Config::Set (str32, UintegerValue (200));
    Config::Set (str33, DoubleValue (0.8));

    /* Link 4 */
    sprintf(path_name, "/NodeList/320/DeviceList/14/RED_Kmin");
    std::string str41(path_name);
    sprintf(path_name, "/NodeList/320/DeviceList/14/RED_Kmax");
    std::string str42(path_name);
    sprintf(path_name, "/NodeList/320/DeviceList/14/RED_Pmax");
    std::string str43(path_name);
    Config::Set (str41, UintegerValue (20));
    Config::Set (str42, UintegerValue (200));
    Config::Set (str43, DoubleValue (0.8));
}

void OnFlowFinished(Ptr<const PacketSink> sink)
{
    finished_flow_num++;

    // printf("wsqfinished %u, sink start time %lf, bulk start time %lf, flow duration: %lf, average througput %lf Mbps,recv %d\n", 
    //         finished_flow_num,
    //         sink->GetFlowStartTime(),
    //         pBulkSends[sink->GetBulkSendIndex()]->GetFlowStartTime(),
    //         //Simulator::Now().GetSeconds() - sink->GetFlowStartTime(),
    //         Simulator::Now().GetSeconds() - pBulkSends[sink->GetBulkSendIndex()]->GetFlowStartTime(),
    //         sink->GetTotalRx() * 0.001 * 0.008 / (Simulator::Now().GetSeconds() - pBulkSends[sink->GetBulkSendIndex()]->GetFlowStartTime()),
    //         sink->GetTotalRx());
    printf("wsqfinished %u,flow duration: %lf, recv %d\n", 
            finished_flow_num,
            Simulator::Now().GetSeconds() - pBulkSends[sink->GetBulkSendIndex()]->GetFlowStartTime(),
            sink->GetTotalRx());

    LogManager::WriteLog(1, "%lf, %lf, %u\n",
                        //Simulator::Now().GetSeconds() - sink->GetFlowStartTime(),
                        Simulator::Now().GetSeconds() - pBulkSends[sink->GetBulkSendIndex()]->GetFlowStartTime(),
                        //sink->GetTotalRx() * 0.001 * 0.008 / (Simulator::Now().GetSeconds() - sink->GetFlowStartTime()),
                        sink->GetTotalRx() * 0.001 * 0.008 / (Simulator::Now().GetSeconds() - pBulkSends[sink->GetBulkSendIndex()]->GetFlowStartTime()),
                        sink->GetTotalRx()
                        ); 
}

std::map<uint32_t, uint32_t> sink_history;
std::map<uint32_t, uint32_t> udp_history;
double measure_interval = 0.001; 

void MeasureGoodPut(uint32_t total_c_num)
{
    LogManager::WriteLog(10, "%f ", float(Simulator::Now().GetMicroSeconds()) / 1000);

    //for(uint32_t i = 0; i < psinks.size(); i++)
    for(uint32_t i = 0; i < total_c_num; i++)
    {
        Ptr<PacketSink> sink = psinks[i];
        double throughput = (sink->GetTotalRx() - sink_history[i]) * 0.001 * 0.008 / measure_interval;
        LogManager::WriteLog(10, "%lf ", throughput);
        sink_history[i] = sink->GetTotalRx();
    }
    LogManager::WriteLog(10, "\n");
    Simulator::Schedule(Seconds(measure_interval), &MeasureGoodPut, total_c_num);
}

void MeasureUdpGoodPut(uint32_t total_c_num)
{
    //LogManager::WriteLog(10, "%u ", Simulator::Now().GetMicroSeconds() / 1000);
    printf("%lu ", Simulator::Now().GetMicroSeconds() / 1000);

    //for(uint32_t i = 0; i < psinks.size(); i++)
    for(uint32_t i = 0; i < total_c_num; i++)
    {
        Ptr<UdpServer> udp = udpServers[i];
        double throughput = (udp->GetReceived() - udp_history[i]) * 1500 * 0.001 * 0.008 / measure_interval;
        //LogManager::WriteLog(10, "%lf ", throughput);
        printf("%lf ", throughput);
        udp_history[i] = udp->GetReceived();
    }
    //LogManager::WriteLog(10, "\n");
    printf("\n");
    Simulator::Schedule(Seconds(measure_interval), &MeasureUdpGoodPut, total_c_num);
}

int main(int argc, char *argv[])
{
    srand(3);

    uint32_t serverPerLeaf = 8; 
    uint32_t leafSwitch = 4; 
    uint32_t spineSwitch = 5;

    std::string serverToLeafRate = "40Gbps";
    std::string leafToSpineRate1 = "40Gbps"; 
    std::string leafToSpineRate2 = "20Gbps"; 

    uint32_t serverToLeafECNThresh = 100;
    uint32_t leafToSpineECNThresh1 = 54; 
    uint32_t leafToSpineECNThresh2 = 27;

    double serverToLeafDelay = 2.0; //us
    double leafToSpineDelay = 2.0; //us 
    uint32_t diff = 1;
    uint32_t sendthreshold = 16;

    bool varyCapacity = false;

    uint32_t traffic_pattern = 0;   //0: all to all, 1: all to one, 2: permutation

    std::string trace_file = "real_trace.txt";

    uint32_t rcvL = 64;
    uint32_t sndL = 32;
    double special_link_delay = 2.0;
    std::string special_link_rate = "40Gbps";

    std::string special_rate1 = "40Gbps";
    std::string special_rate2 = "60Gbps";
    std::string special_rate3 = "80Gbps";
    std::string special_rate4 = "100Gbps";

    double deltaT = 0.001;
    uint32_t messageSize = 32;
    double traffic_load = 0.4;

    CommandLine cmd;
    cmd.AddValue("cmd_serverPerLeaf", "servers per leaf number", serverPerLeaf); 
    cmd.AddValue("cmd_leafSwitch", "leaf switch number", leafSwitch); 
    cmd.AddValue("cmd_spineSwitch", "spine switch number", spineSwitch); 
    cmd.AddValue("cmd_serverToLeafRate", "server to leaf rate", serverToLeafRate); 
    cmd.AddValue("cmd_leafToSpineRate1", "leaf to spine rate 1", leafToSpineRate1); 
    cmd.AddValue("cmd_leafToSpineRate2", "leaf to spine rate 2", leafToSpineRate2);
    cmd.AddValue("cmd_serverToLeafECN", "server to leaf ECN", serverToLeafECNThresh); 
    cmd.AddValue("cmd_leafToSpineECN1", "leaf to spine ECN 1", leafToSpineECNThresh1); 
    cmd.AddValue("cmd_leafToSpineECN2", "leaf to spine ECN 2", leafToSpineECNThresh2); 
    cmd.AddValue("cmd_serverToLeafDelay", "server to leaf delay", serverToLeafDelay); 
    cmd.AddValue("cmd_leafToSpineDelay", "leaf to spine delay", leafToSpineDelay);
    cmd.AddValue("cmd_diff","diff between path",diff);
    cmd.AddValue("cmd_varyCapacity", "vary the capacity of topology", varyCapacity); 
    cmd.AddValue("cmd_trafficPattern", "traffic pattern to test", traffic_pattern); 
    cmd.AddValue("cmd_traceFile", "trace file to test", trace_file); 
    cmd.AddValue("cmd_rcvL", "rcvL for receiver", rcvL); 
    cmd.AddValue("cmd_sndL", "sndL for sender", sndL); 
    cmd.AddValue("cmd_special_link_delay", "special link delay", special_link_delay);
    cmd.AddValue("cmd_special_link_rate", "special link rate", special_link_rate);
    cmd.AddValue("cmd_deltaT", "delta T for inter-message ordering", deltaT); 
    cmd.AddValue("cmd_messageSize", "message size for inter-message ordering", messageSize); 
    cmd.AddValue("cmd_traffic_load", "traffic load", traffic_load);
    
    cmd.AddValue("cmd_special_rate1", "special link rate 1", special_rate1);
    cmd.AddValue("cmd_special_rate2", "special link rate 2", special_rate2);
    cmd.AddValue("cmd_special_rate3", "special link rate 3", special_rate3);
    cmd.AddValue("cmd_special_rate4", "special link rate 4", special_rate4);

    cmd.AddValue("cmd_resend_threshold", "special link rate 4", sendthreshold);

    cmd.Parse (argc, argv);

    char filename1[1024];
    char filename2[1024];
    char filename3[1024];
    char filename4[1024];
    char filename5[1024];
    
    sprintf(filename1, "MP_RDMA_Pattern_%u_ECN1_%u_ECN2_%u_CapVary_%u_FCT_load_%lf.txt", 
                        traffic_pattern, leafToSpineECNThresh1, leafToSpineECNThresh2, varyCapacity, traffic_load); 
    sprintf(filename2, "MP_RDMA_Pattern_%u_ECN1_%u_ECN2_%u_sndL_%u_rcvL_%u_mSize_%u_deltaT_%lf_load_%lf_%s_%s_%s_%s.txt",  
                        traffic_pattern, leafToSpineECNThresh1, leafToSpineECNThresh2, sndL, rcvL, messageSize, deltaT, traffic_load, 
                        special_rate1.c_str(), special_rate2.c_str(), special_rate3.c_str(), special_rate4.c_str());
    sprintf(filename3, "MP_RDMA_Pattern_%u_ECN1_%u_ECN2_%u_CapVary_%u_RTT_load_%lf.txt", 
                        traffic_pattern, leafToSpineECNThresh1, leafToSpineECNThresh2, varyCapacity, traffic_load); 
    sprintf(filename4, "MP_RDMA_Goodput_sndL_%u_rcvL_%u_link_%s_load_%lf.txt", sndL, rcvL, special_link_rate.c_str(), traffic_load);
    sprintf(filename5, "MP_RDMA_violate_FENCE_deltaT_%lf_mSize_%u_load_%lf.txt", deltaT, messageSize, traffic_load);

    LogManager::RegisterLog(1, filename1);
    LogManager::RegisterLog(2, filename2);
    LogManager::RegisterLog(5900, filename3);
    LogManager::RegisterLog(10, filename4);
    LogManager::RegisterLog(20160920, filename5);
    //LogComponentEnable ("PacketSink", LOG_LEVEL_INFO);//wsq add for log


    /* =========================================================================*/
    /*Set default parameters */
    Config::SetDefault("ns3::Queue::Mode", StringValue("QUEUE_MODE_PACKETS")); 
    Config::SetDefault("ns3::Queue::MaxPackets", UintegerValue(1000)); 
    Config::SetDefault("ns3::MpRDMASocket::SndBufSize", UintegerValue(128000000));
    //Config::SetDefault("ns3::MpRDMASocket::RcvBufSize", UintegerValue(2000000000));
    // Config::SetDefault("ns3::MpRDMASocket::SegmentSize", UintegerValue(1436));  
    Config::SetDefault("ns3::MpRDMASocket::RcvBufSize", UintegerValue(5000000));  
    Config::SetDefault("ns3::MpRDMASocket::InitialCwnd", UintegerValue(8));
    Config::SetDefault("ns3::MpRDMASocketImpl::MaxPathNum", UintegerValue(spineSwitch - 1));  
    Config::SetDefault("ns3::MpRDMASocketImpl::FastRecoveryAlpha", DoubleValue(2.0));
    Config::SetDefault("ns3::MpRDMASocketImpl::ReTxSendThreshold", UintegerValue(sendthreshold));  

    Config::SetDefault("ns3::MpRDMASocket::ReceiverOOL", UintegerValue(rcvL));
    Config::SetDefault("ns3::MpRDMASocket::SenderOOL", UintegerValue(sndL));

    Config::SetDefault("ns3::MpRDMASocket::DeltaT", DoubleValue(deltaT));
    Config::SetDefault("ns3::MpRDMASocket::MessageSize", UintegerValue(messageSize));

    Config::SetDefault("ns3::RttEstimator::InitialEstimation", TimeValue(Seconds(0.0001))); 

    printf("L is %u, delta is %u, diff is %u trace is %s\n", rcvL, sndL, diff,trace_file.c_str());
    
    /* =========================================================================*/

    //Topology setup
    Ptr<LeafSpineTopologyHelper> topology = CreateObject<LeafSpineTopologyHelper>();

    /* set topology attrbutes */
    topology->SetAttribute("ServerPerLeaf", UintegerValue(serverPerLeaf)); 
    topology->SetAttribute("LeafSwitchNum", UintegerValue(leafSwitch)); 
    topology->SetAttribute("SpineSwitchNum", UintegerValue(spineSwitch)); 

    topology->SetAttribute("ServerToLeafRate", StringValue(serverToLeafRate)); 
    topology->SetAttribute("LeafToSpineRate1", StringValue(leafToSpineRate1)); 
    topology->SetAttribute("LeafToSpineRate2", StringValue(leafToSpineRate2));

    topology->SetAttribute("ServerToLeafECNThreshold", UintegerValue(serverToLeafECNThresh)); 
    topology->SetAttribute("LeafToSpineECNThreshold1", UintegerValue(leafToSpineECNThresh1)); 
    topology->SetAttribute("LeafToSpineECNThreshold2", UintegerValue(leafToSpineECNThresh2));

    topology->SetAttribute("ServerToLeafDelay", DoubleValue(serverToLeafDelay)); 
    topology->SetAttribute("LeafToSpineDelay", DoubleValue(leafToSpineDelay)); 
    topology->SetAttribute("Diff", UintegerValue(diff));
    
    topology->SetAttribute("VaryCapacity", BooleanValue(varyCapacity)); 

    topology->CreateTopology(); 
    NodeContainer nodes = topology->GetNodes();

    NodeContainer tcpNodes;
    NodeContainer rdmaNodes;
//wsq
/*
    for(uint32_t i = 0; i < nodes.GetN(); i++)
    {
        if(i > 8 && i < serverPerLeaf * leafSwitch)
            tcpNodes.Add(topology->GetNode(i));
        else
            rdmaNodes.Add(topology->GetNode(i));
    }
*/
    for(uint32_t i = 0; i < nodes.GetN(); i++)
    {
            rdmaNodes.Add(topology->GetNode(i));
    }
//wsq end

    //Routing & L4 Protocol setup 
    ECMPLeafSpineRoutingStackHelper stack;

    /* set routing stack attributes*/ 
    stack.SetServerPerLeafNum(serverPerLeaf); 
    stack.SetLeafSwitchNum(leafSwitch); 
    stack.SetSpineSwitchNum(spineSwitch); 
    stack.SetSndL(sndL); 
    stack.SetRcvL(rcvL);

    stack.SetL4Protocol(MpRDMAL4Protocol::GetTypeId()); 
    //stack.SetL4Protocol(UdpL4Protocol::GetTypeId());
    stack.Install(nodes); 
    //stack.Install(rdmaNodes);
    
    //TCP stack
    //ECMPLeafSpineRoutingStackHelper tcp_stack; 
    //tcp_stack.SetServerPerLeafNum(serverPerLeaf);
    //tcp_stack.SetLeafSwitchNum(leafSwitch);
    //tcp_stack.SetSpineSwitchNum(spineSwitch);
    //tcp_stack.SetL4Protocol(TcpL4Protocol::GetTypeId());
    //tcp_stack.Install(tcpNodes);

    uint32_t totalNodes = serverPerLeaf * leafSwitch;
    //printf("there is in total %u servers\n", totalNodes); 
    
    FILE* fp; 
    fp = fopen(trace_file.c_str(), "r"); 

    uint32_t src; 
    uint32_t dest; 
    double startTime; 
    double size; 
    uint32_t flowCount = 0; 

    while(!feof(fp))
    //while(flowCount < 2000)
    //if(flowCount != 0)
    {
        flowCount++; 
        if(fscanf(fp, "%u %u %lf %lf\n", &src, &dest, &startTime, &size)) ;
        if(size != 0)
            size = ((int)size / 1436 + 1) * 1436;
        //printf("flow %u, src %u, dest %u, start %lf, size %lf\n", flowCount, src, dest, startTime, size); 

        std::string protocol = "ns3::MpRDMASocketFactory";

        //if(src > 8 || dest > 8)
        //{
        //    protocol = "ns3::TcpSocketFactory";
        //}

        BulkSendHelper source (protocol, InetSocketAddress (topology->GetTerminalInterface(dest), 1000 + flowCount)); 
        source.SetAttribute ("MaxBytes", UintegerValue ((uint32_t)size));
        ApplicationContainer sourceApps = source.Install (nodes.Get (src));
        sourceApps.Start (Seconds (startTime));
        sourceApps.Stop (Seconds (stopTime)); 
 
        PacketSinkHelper sink (protocol, InetSocketAddress (Ipv4Address::GetAny (), 1000 + flowCount)); 
        ApplicationContainer sinkApps = sink.Install (nodes.Get (dest)); 
        sinkApps.Start (Seconds (0.0));
        sinkApps.Stop (Seconds (stopTime));

        //if(flowCount > 1)
        //    sinkApps.Stop (Seconds (0.02));
        //else
        //    sinkApps.Stop (Seconds (stopTime));
        
        Ptr<PacketSink> psink = DynamicCast<PacketSink> (sinkApps.Get (0)); 
        psink->SetAttribute("FlowStartTime", DoubleValue(startTime));
        psink->SetAttribute("RecvSize", UintegerValue((uint32_t)size));
        psink->SetAttribute("BulkSendIndex", UintegerValue((uint32_t)(flowCount - 1)));
        psink->SetFinishCallback(MakeCallback(OnFlowFinished));
        sink_history[flowCount - 1] = 0;

        psinks.push_back(psink); 
        
        Ptr<BulkSendApplication> pBulk = DynamicCast<BulkSendApplication> (sourceApps.Get (0));
        if(flowCount%4==0)
            pBulk->SetAttribute("SegmentSize", UintegerValue(1436));
        else if(flowCount%4==1)
            pBulk->SetAttribute("SegmentSize", UintegerValue(1436));//wsq  //436
        pBulkSends.push_back(pBulk);
    }


    // UDP flows
    //while(!feof(fp))
    //{
    //    flowCount++; 
    //    if(fscanf(fp, "%u %u %lf %lf\n", &src, &dest, &startTime, &size)) ;
    //    printf("flow %u, src %u, dest %u, start %lf, size %lf\n", flowCount, src, dest, startTime, size); 

    //    //UDP server
    //    uint32_t port = 4000;
    //    UdpServerHelper server(port);
    //    ApplicationContainer apps = server.Install (nodes.Get(dest));
    //    apps.Start (Seconds (startTime));
    //    apps.Stop (Seconds (stopTime));

    //    udpServers.push_back(DynamicCast<UdpServer>(apps.Get(0)));

    //    //UDP client
    //    uint32_t MaxPacketSize = 1500;
    //    Time interPacketInterval = Seconds (1500 * 8.0 / 40000000000.0 / 0.95);
    //    uint32_t maxPacketCount = 1000000;
    //    UdpClientHelper client (Ipv4Address::GetAny(), port);
    //    client.SetAttribute ("MaxPackets", UintegerValue (maxPacketCount));
    //    client.SetAttribute ("Interval", TimeValue (interPacketInterval));
    //    client.SetAttribute ("PacketSize", UintegerValue (MaxPacketSize));
    //    apps = client.Install (nodes.Get(src));
    //    apps.Start (Seconds (startTime));
    //    apps.Stop (Seconds (stopTime));
    //}


    /* set terminal queues */ 
    Simulator::Schedule(MicroSeconds(1), &SetTerminalQueue, totalNodes);
    
    /* adjust link delay */
    //Simulator::Schedule(MicroSeconds(10000), &SetLinkDelay, special_link_delay, 0);
    //Simulator::Schedule(MicroSeconds(20000), &SetLinkDelay, special_link_delay, 1);
    //Simulator::Schedule(MicroSeconds(30000), &SetLinkDelay, special_link_delay, 2);
    //Simulator::Schedule(MicroSeconds(40000), &SetLinkDelay, 2.0, 0);
    //Simulator::Schedule(MicroSeconds(50000), &SetLinkDelay, 2.0, 1);
    //Simulator::Schedule(MicroSeconds(60000), &SetLinkDelay, 2.0, 2);


//wsq
/*
    Simulator::Schedule(MicroSeconds(1), &SetLinkRate, special_rate1, 0);
    Simulator::Schedule(MicroSeconds(1), &SetLinkRate, special_rate2, 1);
    Simulator::Schedule(MicroSeconds(1), &SetLinkRate, special_rate3, 2);
    Simulator::Schedule(MicroSeconds(1), &SetLinkRate, special_rate4, 3);
   
    Simulator::Schedule(MicroSeconds(1), &SetREDParameters);
*/
//wsq end

    //Simulator::Schedule(MicroSeconds(1), &SetLinkBandwidth);

    Simulator::Schedule(Seconds(filterTime), &ResetRx);
    Simulator::Schedule(Seconds(measure_interval), &MeasureGoodPut, psinks.size());
    //Simulator::Schedule(Seconds(measure_interval), &MeasureUdpGoodPut, udpServers.size());

//wsq add for trace
PointToPointHelper trHelper;
	AsciiTraceHelper ascii;
    std::string tfname = "MPRDMA.tr";
    trHelper.EnableAsciiAll (ascii.CreateFileStream (tfname));
//wsq add end

    Simulator::Stop (Seconds (stopTime));  
    Simulator::Run (); 
    Simulator::Destroy (); 

    //for(uint32_t i = 0; i < psinks.size(); i++)
    //{
    //    std::cout << "Total Bytes Received: " << psinks[i]->GetTotalRx () << std::endl;
    //}

    for(uint32_t i = 0; i < psinks.size(); i++)
    {
        double temp = psinks[i]->GetTotalRx() * 0.001 * 0.008 / (stopTime - filterTime); 
        LogManager::WriteLog(2, "%u, %lf\n", i, temp);

        //printf("%u, %lf\n", i, temp); 
    }

    return 0; 
} 
