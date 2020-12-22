#! /usr/bin/env python

import os
import sys
import subprocess
import shutil
import time
import datetime
import threading
import curses
import signal

def myhandler(signal,frame):
    sys.exit()

algnames = ["none", "LeafSpine"]
Algs = [1]

serverPerLeaf = 4
leafSwitch = 2
spineSwitch = 6

serverToLeafRate = "40Gbps"
leafToSpineRate1 = "100Gbps"
leafToSpineRate2 = "100Gbps"

leafToSpineECN1 = [54]
leafToSpineECN2 = [54]

serverToLeafDelay = 2.0
leafToSpineDelay = 2.0
#diffbetweenpath = [2, 3, 4, 5, 6, 10, 15]
diffbetweenpath = [3]
#wsq
#leafToSpineDelay1=2.0
#leafToSpineDelay2=2.0

varyCapacity = [True]

# rate_selection = [0, 1]
rate_selection = [0]
special_rate1 = ["10Gbps", "20Gbps"]
special_rate2 = ["10Gbps", "40Gbps"]
special_rate3 = ["10Gbps", "60Gbps"]
special_rate4 = ["10Gbps", "80Gbps"]

#sndL = [16, 32, 48, 64, 80, 96, 112, 128, 256, 512, 1024]
#sndL = [32]
sndL = [32]
#rcvL = [32, 40, 48, 52, 56, 58, 60, 62, 64, 66, 68]
rcvL = [32]
#sndL = [32]
#rcvL = [64]

special_link_delay = 2.0
special_link_rate = "40Gbps"

#messageSize = [182551, 91275, 60850, 45637, 36510] #512KB
#messageSize = [45637, 22818, 15212, 11409, 9127] #128KB
#messageSize = [11409, 5704, 3803, 2852, 2281] #32KB
messageSize = [36510]

deltaT = [0]

load = 0.4

#traffic_pattern = [0, 1]
traffic_pattern = [0]
#traffic_pattern = [0, 1, 2, 3, 4]
#trace_file = ["Permutation_Traffic_Pattern_144.txt"]
#trace_file = ["real_trace_16.txt"] 
#trace_file = ["all_burst_32.txt"] 
#trace_file = ["all_burst_32_0.8.txt"] 
trace_file = ["real_trace_small.tr",
              "real_trace_big.tr", ]
#trace_file = ["real_trace_2.txt"]
#trace_file = ["Data_Mining_Flow_Size_Overall_10000_load_0.400000_MP_RDMA.txt",
#              "Data_Mining_Flow_Size_Overall_10000_load_0.500000_MP_RDMA.txt", 
#              "Data_Mining_Flow_Size_Overall_10000_load_0.600000_MP_RDMA.txt", 
#              "Data_Mining_Flow_Size_Overall_10000_load_0.700000_MP_RDMA.txt",
#              "Data_Mining_Flow_Size_Overall_10000_load_0.800000_MP_RDMA.txt"] 
#trace_file = ["Web_Search_Flow_Size_Overall_10000_load_0.400000_MP_RDMA.txt",
#              "Web_Search_Flow_Size_Overall_10000_load_0.500000_MP_RDMA.txt", 
#              "Web_Search_Flow_Size_Overall_10000_load_0.600000_MP_RDMA.txt", 
#              "Web_Search_Flow_Size_Overall_10000_load_0.700000_MP_RDMA.txt",
#              "Web_Search_Flow_Size_Overall_10000_load_0.800000_MP_RDMA.txt"] 

#trace_file = ["differentMSS.tr"]

def run_exp(id, alg):
    procs = [] 
#    print "Running %s" % (algnames[alg]) 
    
    for tp in traffic_pattern:
        load = tp * 0.1 + 0.4
        for index in range(0, len(leafToSpineECN1)): 
            for vc in varyCapacity:
                for sL in sndL:
                    for rL in rcvL:
                        for dT in deltaT:
                            for ms in messageSize:
                                for rl in rate_selection:
#log_file = open("{}-{}-{}.txt".format(tp, index, vc), 'w')
                                    for df in diffbetweenpath:
                                        proc = subprocess.Popen(['../../../build/scratch/mp_rdma_leaf_spine/mp_rdma_leaf_spine', 
                                                             '--cmd_serverPerLeaf=%d' % serverPerLeaf, 
                                                             '--cmd_leafSwitch=%d' % leafSwitch, 
                                                             '--cmd_spineSwitch=%d' % spineSwitch, 
                                                             '--cmd_serverToLeafRate=%s' % serverToLeafRate, 
                                                             '--cmd_leafToSpineRate1=%s' % leafToSpineRate1, 
                                                             '--cmd_leafToSpineRate2=%s' % leafToSpineRate2, 
                                                             '--cmd_leafToSpineECN1=%d' % leafToSpineECN1[index], 
                                                             '--cmd_leafToSpineECN2=%d' % leafToSpineECN2[index], 
                                                             '--cmd_serverToLeafDelay=%lf' % serverToLeafDelay, 
                                                             '--cmd_leafToSpineDelay=%lf' % leafToSpineDelay,
                                                             '--cmd_diff=%d' % df,
                                                             '--cmd_rcvL=%d' % rL,
                                                             '--cmd_sndL=%d' % sL,
                                                             '--cmd_special_link_delay=%lf' % special_link_delay,
#wsq
                                                             #'--cmd_leafToSpineDelay1=%lf' % leafToSpineDelay1,
                                                             #'--cmd_leafToSpineDelay2=%lf' % leafToSpineDelay2,
                                                             '--cmd_special_link_rate=%s' % special_link_rate,
                                                             '--cmd_messageSize=%d' % ms,
                                                             '--cmd_traffic_load=%lf' % load,
                                                             '--cmd_deltaT=%lf' % dT,
                                                             '--cmd_varyCapacity=%d' % varyCapacity[0], 
                                                             '--cmd_trafficPattern=%d' % traffic_pattern[tp],
                                                             '--cmd_special_rate1=%s' % special_rate1[rl],
                                                             '--cmd_special_rate2=%s' % special_rate2[rl],
                                                             '--cmd_special_rate3=%s' % special_rate3[rl],
                                                             '--cmd_special_rate4=%s' % special_rate4[rl],
#'--cmd_traceFile=%s' % trace_file[tp]], stdout=log_file)
                                                             '--cmd_traceFile=%s' % trace_file[tp]])
#log_file.flush() 
                                        proc.wait()
                                        #procs.append(proc)
    
#    for proc in procs: 
#        proc.wait() 

if __name__ == '__main__':

    start_time = datetime.datetime.now()
    threads = []
    signal.signal(signal.SIGINT,myhandler)
    for id in range(0, len(Algs)):
        alg = Algs[id]
        threads.append(threading.Thread(target=run_exp, args=(id, alg)))

    for thread in threads:
        thread.start()
        thread.join()    
#    for thread in threads:
#        thread.join()

    end_time = datetime.datetime.now()

    print ('Total time:', end_time - start_time)
