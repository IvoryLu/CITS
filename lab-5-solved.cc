#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/config-store-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/olsr-helper.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-list-routing-helper.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/applications-module.h"
#include "myapp.h"
    
#include <iostream>
#include <fstream>
#include <vector>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("WifiSimpleAdhocGrid");

uint32_t MacTxDropCount, PhyTxDropCount, PhyRxDropCount;

void MacTxDrop(Ptr<const Packet> p)
{
  NS_LOG_INFO("Packet Drop");
  MacTxDropCount++;
}

void PrintDrop()
{
  std::cout << Simulator::Now().GetSeconds() << "\t" << MacTxDropCount << "\t"<< PhyTxDropCount << "\t" << PhyRxDropCount << "\n";
  Simulator::Schedule(Seconds(5.0), &PrintDrop);
}

void PhyTxDrop(Ptr<const Packet> p)
{
  NS_LOG_INFO("Packet Drop");
  PhyTxDropCount++;
}
void PhyRxDrop(Ptr<const Packet> p)
{
  NS_LOG_INFO("Packet Drop");
  PhyRxDropCount++;
}

void ReceivePacket (Ptr<Socket> socket)
{
	while (socket-> Recv ())
	{
		NS_LOG_UNCOND ("Received one packet!");
	}	
}

static void GenerateTraffic (Ptr<Socket> socket, uint32_t pktSize, uint32_t pktCount, Time pktInterval )
{
	if (pktCount > 0)
	{
		socket->Send (Create<Packet> (pktSize));
		Simulator::Schedule (pktInterval, &GenerateTraffic, socket, 					     pktSize,pktCount-1, pktInterval);
	}
	else
	{
		socket->Close ();
	}
}

int main(int argc, char *argv[])
{

	std::string phyMode("DsssRate1Mbps");
	double distance = 500;	//m
	uint32_t packetSize = 1000;	//bytes
	uint32_t numPackets = 1;
	uint32_t numNodes = 25;
	uint32_t sinkNode = 1;
	uint32_t sourceNode = 24;
	double interval = 1.0;	//seconds
	std::string rtslimit = "1500";
	bool verbose = false;
	bool tracing = false;
	
	CommandLine cmd;
	cmd.AddValue ("phyMode", "Wifi Phy mode", phyMode);
	cmd.AddValue ("distance", "distance (m)", distance);
	cmd.AddValue ("packetSize", "size of application packet sent", packetSize);
	cmd.AddValue ("numPackets", "number of packets generated", numPackets);
	cmd.AddValue ("interval", "interval (seconds) between packets", interval);
	cmd.AddValue ("verbose", "turn on all WifiNetDevice log components", verbose);
	cmd.AddValue ("tracing", "turn on ascii and pcap tracing", tracing);
	cmd.AddValue ("numNodes", "number of nodes", numNodes);
	cmd.AddValue ("sinkNode", "Receiver node number", sinkNode);
	cmd.AddValue ("sourceNode", "Sender node number", sourceNode);	

	cmd.Parse (argc, argv);

	// Convert to time object
	Time interPacketInterval = Seconds (interval);
	
	//disable fragmentation for frames below 2200 bytes
	Config::SetDefault ("ns3::WifiRemoteStationManager::FragmentationThreshold", StringValue ("2200"));
	// turn off RTS/CTS for frames below 2200 bytes
	Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold", StringValue ("2200"));
	// Fix non-unicast data rate to be the same as that of unicast
	Config::SetDefault ("ns3::WifiRemoteStationManager::NonUnicastMode", StringValue (phyMode));

	NodeContainer c;
	c.Create(numNodes);
	
	// The below set of helpers will help us to put together the wifi NICs we want
	WifiHelper wifi;
	if(verbose)
	{
		wifi.EnableLogComponents ();  // Turn on all Wifi logging
	}

	YansWifiPhyHelper wifiPhy =  YansWifiPhyHelper::Default (); //Create a phy helper in a default working state
	// set it to zero; otherwise, gain will be added
	wifiPhy.Set ("RxGain", DoubleValue (-10) );
	// ns-3 supports RadioTap and Prism tracing extensions for 802.11b
	wifiPhy.SetPcapDataLinkType (YansWifiPhyHelper::DLT_IEEE802_11_RADIO); 
	
	YansWifiChannelHelper wifiChannel;
	wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
	wifiChannel.AddPropagationLoss ("ns3::FriisPropagationLossModel");
	wifiPhy.SetChannel (wifiChannel.Create ());

	//Add an upper mac and disable rate control
	WifiMacHelper wifiMac;
	wifi.SetStandard (WIFI_PHY_STANDARD_80211b);
	wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager","DataMode",StringValue (phyMode),"ControlMode",StringValue (phyMode));
	//Set it to adhov mode
	wifiMac.SetType ("ns3::AdhocWifiMac");
	NetDeviceContainer devices = wifi.Install (wifiPhy, wifiMac, c);
		
	MobilityHelper mobility;
	mobility.SetPositionAllocator ("ns3::GridPositionAllocator",
					"MinX",DoubleValue (0.0),
					"MinY", DoubleValue (0.0), 
					"DeltaX", DoubleValue (distance), 
					"DeltaY", DoubleValue (distance), 
					"GridWidth", UintegerValue (5), 
					"LayoutType", StringValue ("RowFirst"));
	mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
	mobility.Install (c);
	
	// Enable OLSR
	OlsrHelper olsr;    
	Ipv4StaticRoutingHelper staticRouting;
	
	Ipv4ListRoutingHelper list;
	list.Add (staticRouting, 0);    //Parameters: routing and priority	
	list.Add (olsr, 10);    //rouing: a rouing helper    priority: the priority of the associated helper

	InternetStackHelper internet;
	internet.SetRoutingHelper (list); // has effect on the next Install ()
	internet.Install (c);

	Ipv4AddressHelper ipv4;
	NS_LOG_INFO ("Assign IP Addresses.");
	ipv4.SetBase ("10.1.1.0", "255.255.255.0");   //Set the base network number, network mask.
	Ipv4InterfaceContainer i = ipv4.Assign (devices);

	//Create Apps
	uint16_t sinkPort = 6;	//use the same for all apps

	//UPD connection from N0 to N24

	Address sinkAddress1 (InetSocketAddress (i.GetAddress(24), sinkPort));// interface of n24
	PacketSinkHelper packetSinkHelper1 ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny(), sinkPort));
	ApplicationContainer sinkApps1 = packetSinkHelper1.Install (c.Get(24));
	sinkApps1.Start (Seconds (0.));
	sinkApps1.Stop (Seconds (100.));
	
	Ptr<Socket> source1 = Socket::CreateSocket (c.Get (0), UdpSocketFactory::GetTypeId());//Source at n0

	//Create UDP application at n0
	Ptr<MyApp> app1 = CreateObject<MyApp> ();
	app1->Setup (source1, sinkAddress1, packetSize, numPackets, DataRate ("1Mbps"));
	c.Get (0)->AddApplication (app1);
	app1->SetStartTime (Seconds (31.));
	app1->SetStopTime (Seconds (100.));

	//UDP connection from N10 to N14
	 Address sinkAddress2 (InetSocketAddress (i.GetAddress (14), sinkPort)); // interface of n14
    	PacketSinkHelper packetSinkHelper2 ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), sinkPort));
   	 ApplicationContainer sinkApps2 = packetSinkHelper2.Install (c.Get (14)); //n14 as sink
    	sinkApps2.Start (Seconds (0.));

	sinkApps2.Stop (Seconds (100.));
	
	Ptr<Socket> ns3UdpSocket2 = Socket::CreateSocket (c.Get (10), UdpSocketFactory::GetTypeId ()); //source at n10
	
	//Create UDP application at n10
	Ptr<MyApp> app2 = CreateObject<MyApp> ();
    	app2->Setup (ns3UdpSocket2, sinkAddress2, packetSize, numPackets, DataRate ("1Mbps"));
    	c.Get (10)->AddApplication (app2);
    	app2->SetStartTime (Seconds (31.5));

	app2->SetStopTime (Seconds (100.));

	// UDP connection from N20 to N4

     	Address sinkAddress3 (InetSocketAddress (i.GetAddress (4), sinkPort)); // interface of n4
     	PacketSinkHelper packetSinkHelper3 ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), sinkPort));
     	ApplicationContainer sinkApps3 = packetSinkHelper3.Install (c.Get (4)); //n2 as sink
     	sinkApps3.Start (Seconds (0.));
     	sinkApps3.Stop (Seconds (100.));

     	Ptr<Socket> ns3UdpSocket3 = Socket::CreateSocket (c.Get (20), UdpSocketFactory::GetTypeId ()); //source at n20

     	// Create UDP application at n20
     	Ptr<MyApp> app3 = CreateObject<MyApp> ();
     	app3->Setup (ns3UdpSocket3, sinkAddress3, packetSize, numPackets, DataRate ("1Mbps"));
     	c.Get (20)->AddApplication (app3);
     	app3->SetStartTime (Seconds (32.));
     	app3->SetStopTime (Seconds (100.));
	
	//Install FlowMonitor on all nodes
	FlowMonitorHelper flowmon;
	Ptr<FlowMonitor> monitor = flowmon.InstallAll();

	//Trace Collisions
	Config::ConnectWithoutContext("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MacTxDrop", MakeCallback(&MacTxDrop));	
	Config::ConnectWithoutContext("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/PhyRxDrop", MakeCallback(&PhyRxDrop));
	Config::ConnectWithoutContext("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Phy/PhyTxDrop", MakeCallback(&PhyTxDrop));	
	
	Simulator::Schedule(Seconds(5.0), &PrintDrop);

  	Simulator::Stop (Seconds (100.0));
  	Simulator::Run ();

  	PrintDrop();

  	// Print per flow statistics
 	monitor->CheckForLostPackets ();
  	Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  	std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

  	for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator iter = stats.begin (); iter != stats.end (); ++iter)
    	{
	  	Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (iter->first);

      		if ((t.sourceAddress == Ipv4Address("10.1.1.1") && t.destinationAddress == Ipv4Address("10.1.1.25"))
    		|| (t.sourceAddress == Ipv4Address("10.1.1.11") && t.destinationAddress == Ipv4Address("10.1.1.15"))
    		|| (t.sourceAddress == Ipv4Address("10.1.1.21") && t.destinationAddress == Ipv4Address("10.1.1.5")))
        	{
    	  		NS_LOG_UNCOND("Flow ID: " << iter->first << " Src Addr " << t.sourceAddress << " Dst Addr " << t.destinationAddress);
    	  		NS_LOG_UNCOND("Tx Packets = " << iter->second.txPackets);
    	  		NS_LOG_UNCOND("Rx Packets = " << iter->second.rxPackets);
    	  		NS_LOG_UNCOND("Throughput: " << iter->second.rxBytes * 8.0 / (iter->second.timeLastRxPacket.GetSeconds()-iter->second.timeFirstTxPacket.GetSeconds()) / 1024  << " Kbps");
        	}
    	}
 	monitor->SerializeToXmlFile("lab-5.flowmon", true, true);

  	Simulator::Destroy ();

  	return 0;
	/*
	TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
	Ptr<Socket> recvSink = Socket::CreateSocket (c.Get (sinkNode), tid);
	InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), 80);
	recvSink->Bind (local);
	recvSink->SetRecvCallback (MakeCallback (&ReceivePacket));
	
	Ptr<Socket> source = Socket::CreateSocket (c.Get (sourceNode), tid);
	InetSocketAddress remote = InetSocketAddress (i.GetAddress (sinkNode, 0), 80);
	source->Connect (remote);
	*/

	if(tracing == true)
	{}


}

