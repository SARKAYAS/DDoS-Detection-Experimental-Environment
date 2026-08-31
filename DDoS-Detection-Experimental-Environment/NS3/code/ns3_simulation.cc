#include <ns3/csma-helper.h>
#include "ns3/bridge-helper.h"
#include "ns3/mobility-module.h"
#include "ns3/nstime.h"
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/netanim-module.h"
#include "ns3/ping-helper.h"
#include "ns3/tcp-header.h"
#include <iostream>

#define TCP_SINK_PORT 9000
#define HTTP_SINK_PORT 80
#define UDP_SINK_PORT 9001
#define SYN_SINK_PORT 9002
#define MDNS_PORT 5353
#define LLMNR_PORT 5355

#define MAX_BULK_BYTES 100000
#define DDOS_RATE "20Mbps"
#define MAX_SIMULATION_TIME 600.0
#define NUMBER_OF_BOTS 3
#define PING_START_TIME 0.5
#define PING_STOP_TIME 60.0

using namespace ns3;
NS_LOG_COMPONENT_DEFINE("DDoSAttack");

// Neden: Tüm saldırı uygulamalarını tek kapsayıcıda tutup birlikte başlatmak için.
// Neden: Tüm saldırı uygulamalarını tek kapsayıcıda tutup birlikte başlatmak için.
static ApplicationContainer MakeOnOff(Ptr<Node> n, const std::string& factory, Ipv4Address dstIp, uint16_t port) {
  Address dst = Address(InetSocketAddress(dstIp, port));
  OnOffHelper onoff(factory, dst);
  onoff.SetAttribute("DataRate", DataRateValue(DataRate(DDOS_RATE)));

  if (port == TCP_SINK_PORT) {
      // 1460 Byte = Standart Ethernet MTU (1500) - IP Header (20) - TCP Header (20)
      // Bu, tek bir pakette gönderilebilecek en büyük "anlamsız veri" miktarını temsil eder.
      onoff.SetAttribute("PacketSize", UintegerValue(1460)); 
  } else {
      // Diğer saldırılar (SYN, UDP vs.) veya varsayılan durumlar için standart boyut
      onoff.SetAttribute("PacketSize", UintegerValue(1024)); 
  }
  // --- REVİZE BİTİŞİ ---

  onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=30]"));
  onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
  return onoff.Install(n);
}



// --- YENİ EKLENECEK KISIM: HTTP GET FLOOD UYGULAMASI ---
// Gerçek bir tarayıcı gibi davranıp "GET / HTTP/1.1" isteği gönderir.
// --- GÜNCELLENMİŞ HTTP GET FLOOD UYGULAMASI ---
// Her istekte yeni bir bağlantı açar (Connection: close mantığı)
// Bu sayede TCP penceresi dolup kilitlense bile yeni bağlantı zorlanır.
// --- REVİZE EDİLMİŞ HTTP GET FLOOD APP (CALLBACK YÖNTEMİ) ---
class HttpGetFloodApp : public Application {
public:
  HttpGetFloodApp () : m_peer (), m_running (false) {}

  void Setup (Ptr<Node> node, Ipv4Address peer, uint16_t port) {
    m_node = node;
    m_peer = InetSocketAddress (peer, port);
  }

private:
  virtual void StartApplication (void) {
    m_running = true;
    ScheduleNextConnection();
  }

  virtual void StopApplication (void) {
    m_running = false;
  }

  // Yeni bir bağlantı başlat
  void ScheduleNextConnection (void) {
    if (!m_running) return;

    Ptr<Socket> socket = Socket::CreateSocket (m_node, TypeId::LookupByName ("ns3::TcpSocketFactory"));
    
    // ÖNEMLİ: Bağlantı kurulduğunda veya hata verdiğinde ne yapacağını söylüyoruz
    socket->SetConnectCallback (
       MakeCallback (&HttpGetFloodApp::ConnectionSucceeded, this),
       MakeCallback (&HttpGetFloodApp::ConnectionFailed, this)
    );

    socket->Connect (m_peer);
  }

  // Bağlantı kuruldu! (SYN-ACK geldi) -> Şimdi HTTP Gönder
  void ConnectionSucceeded (Ptr<Socket> socket) {
    if (!m_running) return;

    std::string httpContent = "GET /index.html HTTP/1.1\r\n"
                              "Host: www.hedef.com\r\n"
                              "User-Agent: DDoS-Bot/1.0\r\n"
                              "Connection: close\r\n\r\n";

    Ptr<Packet> packet = Create<Packet> ((uint8_t*)httpContent.c_str(), httpContent.length());
    socket->Send (packet);

    // İşi biten soketi hemen kapatmıyoruz, verinin gitmesi için minik bir şans verip kapatıyoruz
    Simulator::Schedule(MilliSeconds(10), &Socket::Close, socket);
    
    // Bir sonraki saldırıyı planla (Biraz nefes payı bırakıyoruz ki sunucu cevap verebilsin)
    Simulator::Schedule(MilliSeconds(50), &HttpGetFloodApp::ScheduleNextConnection, this);
  }

  // Bağlantı kurulamadı (Sunucu çok meşgul) -> Soketi kapat ve tekrar dene
  void ConnectionFailed (Ptr<Socket> socket) {
    socket->Close();
    // Sunucu cevap vermiyorsa hemen tekrar yüklenip simülasyonu kilitleme, az bekle
    Simulator::Schedule(MilliSeconds(100), &HttpGetFloodApp::ScheduleNextConnection, this);
  }

  Address m_peer;
  bool m_running;
  Ptr<Node> m_node;
};



// --- ÖZEL SYN FLOOD UYGULAMASI ---
// Standart TCP yerine Raw Socket kullanarak sadece SYN bayraklı paket basar.
// Böylece "Half-Open" (Yarı Açık) bağlantı saldırısını simüle eder.
class SynFloodApp : public Application {
public:
  SynFloodApp () : m_socket (0), m_peer (), m_packetSize (0), m_dataRate (0), m_running (false) {}


  void Setup (Ptr<Node> node, Ipv4Address peer, uint16_t port, StringValue dataRate, uint32_t packetSize) {
    m_node = node;
    m_peer = InetSocketAddress (peer, port);
    // DÜZELTME: dataRate yerine dataRate.Get() kullanıyoruz
    m_dataRate = DataRate (dataRate.Get());
    m_packetSize = packetSize;
  }

private:
  virtual void StartApplication (void) {
    m_running = true;
    // Raw Socket (Ham Soket) oluşturuyoruz - Protokol 6 (TCP)
    TypeId tid = TypeId::LookupByName ("ns3::Ipv4RawSocketFactory");
    m_socket = Socket::CreateSocket (m_node, tid);
    m_socket->SetAttribute ("Protocol", UintegerValue(6)); 
    m_socket->Connect (m_peer); // Hedef adresi belirle
    SendPacket ();
  }

  virtual void StopApplication (void) {
    m_running = false;
    if (m_socket) { m_socket->Close (); }
  }

  void SendPacket (void) {
    if (!m_running) return;

    // 1. Boş bir paket oluştur
    Ptr<Packet> packet = Create<Packet> (0); // Payload yok, sadece başlık

    // 2. TCP Başlığını elle hazırla ve SYN bayrağını çek
    TcpHeader tcpHeader;
    tcpHeader.SetFlags (TcpHeader::SYN); 
    tcpHeader.SetSequenceNumber (SequenceNumber32 (rand ()));
    tcpHeader.SetDestinationPort (InetSocketAddress::ConvertFrom(m_peer).GetPort ());
    tcpHeader.SetSourcePort (1025 + (rand () % 60000)); // Rastgele kaynak port
    tcpHeader.SetWindowSize (65535);
    
    // 3. Başlığı pakete ekle
    packet->AddHeader (tcpHeader);

    // 4. Gönder
    m_socket->Send (packet);

    // 5. Bir sonraki paketi zamanla (Hıza göre hesaplama)
    Time tNext (Seconds (m_packetSize * 8 / static_cast<double>(m_dataRate.GetBitRate ())));
    Simulator::Schedule (tNext, &SynFloodApp::SendPacket, this);
  }

  Ptr<Socket> m_socket;
  Address m_peer;
  uint32_t m_packetSize;
  DataRate m_dataRate;
  bool m_running;
  Ptr<Node> m_node;
};



int main(int argc, char *argv[]) {
  int choice; bool enableDDoS=false, enablePing=false; uint16_t ddosPort=0;
  std::string socketFactory;
  std::string attackType = "bostest";
  std::cout << "\n--- DDoS Simulation Selection Menu---\n";
  std::cout << "1: Run Ping Test (No Attack)\n";
  std::cout << "2: TCP DDoS Attack (Port 9000)\n";
  std::cout << "3: HTTP DDoS Attack (Port 80)\n";
  std::cout << "4: UDP DDoS Attack (Port 9001)\n";
  std::cout << "5: SYN DDoS Attack (Port 9002)\n";
  std::cout << "6: Benign (TCP - UDP - ICMP )\n";
  std::cout << "Make your choice. (1-6): ";
  std::cin >> choice;

  switch (choice) {
    case 1: 
        enablePing=true; 
        attackType = "Ping_Test"; // Dosya adı bu olacak
        break;
    case 2: 
        enableDDoS=true; ddosPort=TCP_SINK_PORT; socketFactory="ns3::TcpSocketFactory"; 
        attackType = "TCP_Attack"; // Dosya adı bu olacak
        break;
    case 3: 
        enableDDoS=true; ddosPort=HTTP_SINK_PORT; socketFactory="ns3::TcpSocketFactory"; 
        attackType = "HTTP_Attack"; // Dosya adı bu olacak
        break;
    case 4: 
        enableDDoS=true; 
        ddosPort=9001; // Hedef Port: 80 (Ekran görüntüsündeki gibi)
        socketFactory="ns3::UdpSocketFactory"; // Protokol: UDP
        attackType = "UDP_Attack"; 
        break;
    case 5: 
        enableDDoS=true; ddosPort=SYN_SINK_PORT; socketFactory="ns3::TcpSocketFactory"; 
        attackType = "SYN_Attack"; // Dosya adı bu olacak
        break;
    case 6:
        enableDDoS = false; // Saldırı yok
        enablePing = true;  // Normal trafikte ping olur (ICMP)
        attackType = "Benign";
        break;
    default: 
        enablePing=true; 
        attackType = "Default_Ping"; 
        break;
  }

  Time::SetResolution(Time::NS);

  NodeContainer core; core.Create(4);        // 0:Attacker, 1:R1, 2:R2, 3:Victim
  NodeContainer bots; bots.Create(NUMBER_OF_BOTS);
  NodeContainer pings; pings.Create(2);      // 0:Ping-PC1, 1:Ping-PC2
  
  Ptr<Node> switch1 = CreateObject<Node>();
  Ptr<Node> switch2 = CreateObject<Node>();
  Ptr<Node> cloudLeft = CreateObject<Node>();
  Ptr<Node> cloudRight = CreateObject<Node>();

  // 2. İNTERNET STACK (En başta kuralım ki karışıklık olmasın)
  InternetStackHelper stack;
  stack.Install(core);
  stack.Install(bots);
  stack.Install(pings);
  // Switch ve Cloud düğümlerine IP stack kurulmaz (L2 cihazlar)

  // 3. TOPOLOJİ VE BAĞLANTILAR

  // A) OMURGA (R1 <-> R2)
  PointToPointHelper pp; 
  pp.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
  pp.SetChannelAttribute("Delay", StringValue("1ms"));
  NetDeviceContainer d12 = pp.Install(core.Get(1), core.Get(2)); // R1-R2 hattı

  // B) LAN AYARLARI (CSMA + Bridge)
  CsmaHelper csma; 
  csma.SetChannelAttribute("DataRate", StringValue("100Mbps"));
  csma.SetChannelAttribute("Delay", StringValue("0.5ms"));
  BridgeHelper bridge;

  // --- SOL LAN (Attacker, Bots, Ping1, R1) ---
  NetDeviceContainer cloudLeftPorts; // Cloud'a bağlı portlar
  NetDeviceContainer leftClients;    // IP alacak istemciler (Router HARİÇ)
  
  // Helper fonksiyon (Cloud bağlantısı için)
  auto connectToCloudLeft = [&](Ptr<Node> host) {
    NetDeviceContainer pair = csma.Install(NodeContainer(host, cloudLeft));
    cloudLeftPorts.Add(pair.Get(1)); // Cloud tarafı
    return pair.Get(0);              // Host tarafı
  };

  // İstemcileri bağla (Listeye ekle)
  leftClients.Add(connectToCloudLeft(core.Get(0))); // Attacker


  for (uint32_t i=0; i<bots.GetN(); ++i) {
      leftClients.Add(connectToCloudLeft(bots.Get(i))); // Botlar
  }
  
  // Cloud <-> Switch1
  NetDeviceContainer uplCloudL_Sw1 = csma.Install(NodeContainer(cloudLeft, switch1));
  cloudLeftPorts.Add(uplCloudL_Sw1.Get(0)); // Cloud tarafı
  NetDeviceContainer sw1Ports; 
  sw1Ports.Add(uplCloudL_Sw1.Get(1));       // Switch tarafı

  // PingPC1 <-> Switch1
  NetDeviceContainer pc1_sw1 = csma.Install(NodeContainer(pings.Get(0), switch1));
  leftClients.Add(pc1_sw1.Get(0));          // PingPC1
  sw1Ports.Add(pc1_sw1.Get(1));

  // Router1 <-> Switch1
  NetDeviceContainer r1_sw1 = csma.Install(NodeContainer(core.Get(1), switch1));
  // DİKKAT: Router bacağını 'leftClients'a EKLEMİYORUZ. Onu ayrı tutuyoruz.
  NetDeviceContainer r1_lan_dev = r1_sw1.Get(0); 
  sw1Ports.Add(r1_sw1.Get(1));

  // Köprüleri kur
  bridge.Install(cloudLeft, cloudLeftPorts);
  bridge.Install(switch1, sw1Ports);


  // --- SAĞ LAN (Victim, Ping2, R2) ---
  NetDeviceContainer sw2Ports;
  
  // Router2 <-> Switch2
  NetDeviceContainer r2_sw2 = csma.Install(NodeContainer(core.Get(2), switch2));
  NetDeviceContainer r2_lan_dev = r2_sw2.Get(0); // R2 LAN bacağı
  sw2Ports.Add(r2_sw2.Get(1));

  // PingPC2 <-> Switch2
  NetDeviceContainer pc2_sw2 = csma.Install(NodeContainer(pings.Get(1), switch2));
  NetDeviceContainer rightClients; // Sağ taraftaki istemciler
  rightClients.Add(pc2_sw2.Get(0));
  sw2Ports.Add(pc2_sw2.Get(1));

  // Victim <-> CloudRight
  NetDeviceContainer cloudRightPorts;
  NetDeviceContainer victim_cloudR = csma.Install(NodeContainer(core.Get(3), cloudRight));
  rightClients.Add(victim_cloudR.Get(0)); // Victim
  cloudRightPorts.Add(victim_cloudR.Get(1));

  // CloudRight <-> Switch2
  NetDeviceContainer uplCloudR_Sw2 = csma.Install(NodeContainer(cloudRight, switch2));
  cloudRightPorts.Add(uplCloudR_Sw2.Get(0));
  sw2Ports.Add(uplCloudR_Sw2.Get(1));

  // Köprüleri kur
  bridge.Install(cloudRight, cloudRightPorts);
  bridge.Install(switch2, sw2Ports);


  // 4. IP ATAMALARI (İstediğin Düzenleme Burada)
  
  // A) SOL LAN (192.168.10.0/24)
  Ipv4AddressHelper ipv4Left;
  ipv4Left.SetBase("192.168.10.0", "255.255.255.0");
  
  // ÖNCE ROUTER'A ATA (Böylece 192.168.10.1 Router olur)
  ipv4Left.Assign(r1_lan_dev);
  
  // SONRA DİĞERLERİNE ATA (Attacker 192.168.10.2 olur)
  ipv4Left.Assign(leftClients);


  // B) SAĞ LAN (22.22.22.0/24)
  Ipv4AddressHelper ipv4Right;
  ipv4Right.SetBase("22.22.22.0", "255.255.255.0");
  
  // Önce Router2 (22.22.22.1 olur)
  ipv4Right.Assign(r2_lan_dev);
  
  // Sonra Victim ve Ping2
  Ipv4InterfaceContainer rightIfs = ipv4Right.Assign(rightClients);
  // Victim'in IP adresini al (Saldırı hedefi için lazım)
  // rightClients'a ekleme sıramız: 0:Ping2, 1:Victim. O yüzden Get(1) Victim'dir.
  Ipv4Address serverIpv4 = rightIfs.GetAddress(1); 


  // C) OMURGA (WAN)
  Ipv4AddressHelper ipv4Wan;
  ipv4Wan.SetBase("81.128.81.0", "255.255.255.252");
  ipv4Wan.Assign(d12);

  // Routing Tablolarını Oluştur
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();
  
  
  
  


// -----------------------------------------------------
  // Buradan sonrası senin kodunla aynı (Uygulamalar ve Animasyon)
  // -----------------------------------------------------

  // Uygulamalar (Sink)
  ApplicationContainer sinks;
  // mDNS ve LLMNR için Dinleyiciler (Victim üzerinde)
  sinks.Add(PacketSinkHelper("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), MDNS_PORT)).Install(core.Get(3)));
  sinks.Add(PacketSinkHelper("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), LLMNR_PORT)).Install(core.Get(3)));
  sinks.Add(PacketSinkHelper("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), UDP_SINK_PORT)).Install(core.Get(3)));
  sinks.Add(PacketSinkHelper("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), TCP_SINK_PORT)).Install(core.Get(3)));
  sinks.Add(PacketSinkHelper("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), HTTP_SINK_PORT)).Install(core.Get(3)));
  sinks.Add(PacketSinkHelper("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), SYN_SINK_PORT)).Install(core.Get(3)));
  sinks.Start(Seconds(0.0)); 
  sinks.Stop(Seconds(MAX_SIMULATION_TIME));

  // Saldırı Uygulamaları
  ApplicationContainer appsAll;
  
  if (enableDDoS) {
      NodeContainer attackers;
      attackers.Add(core.Get(0)); // Attacker
      attackers.Add(bots);        // Tüm Botlar

      for (uint32_t k = 0; k < attackers.GetN(); ++k) {
          Ptr<Node> node = attackers.Get(k);

          // 1. DURUM: SYN FLOOD (Port 9002)
          if (ddosPort == SYN_SINK_PORT) {
              Ptr<SynFloodApp> synApp = CreateObject<SynFloodApp> ();
              synApp->Setup (node, serverIpv4, ddosPort, StringValue(DDOS_RATE), 64);
              node->AddApplication (synApp);
              synApp->SetStartTime (Seconds (2.0));
              synApp->SetStopTime (Seconds (MAX_SIMULATION_TIME));
          } 
          // 2. DURUM: HTTP GET FLOOD (Port 80) -> YENİ EKLENEN KISIM
          else if (ddosPort == HTTP_SINK_PORT) {
              Ptr<HttpGetFloodApp> httpApp = CreateObject<HttpGetFloodApp> ();
              
              // Sadece Node, IP ve Port veriyoruz. Hız parametresini kaldırdık.
              httpApp->Setup (node, serverIpv4, ddosPort); 
              
              node->AddApplication (httpApp);
              httpApp->SetStartTime (Seconds (2.0));
              httpApp->SetStopTime (Seconds (MAX_SIMULATION_TIME));
          }

          // 3. DURUM: DİĞERLERİ (UDP, TCP Raw) - Standart OnOffHelper
          else {
              appsAll.Add(MakeOnOff(node, socketFactory, serverIpv4, ddosPort));
          }
      }
      
  }
  
  
  
  
  else if (choice == 6) { // Seçenek 6: Normal Trafik Modu (SÜREKLİ AKIŞ)
      NodeContainer normalUsers;
      normalUsers.Add(core.Get(0)); // Attacker -> Normal Kullanıcı
      normalUsers.Add(bots);        // Botlar -> Normal Kullanıcı

      for (uint32_t k = 0; k < normalUsers.GetN(); ++k) {
          Ptr<Node> node = normalUsers.Get(k);

          // 1. HTTP Gezintisi (TCP 80) - DAHA SIK
          // OnTime (Veri yollama): 2 sn, OffTime (Bekleme): 0.5 sn (Hemen yeni sayfa açıyor gibi)
          OnOffHelper httpNormal("ns3::TcpSocketFactory", InetSocketAddress(serverIpv4, HTTP_SINK_PORT));
          httpNormal.SetAttribute("DataRate", StringValue("50Kbps")); 
          httpNormal.SetAttribute("PacketSize", UintegerValue(1024));
          httpNormal.SetAttribute("OnTime", StringValue("ns3::ExponentialRandomVariable[Mean=2.0]")); 
          httpNormal.SetAttribute("OffTime", StringValue("ns3::ExponentialRandomVariable[Mean=0.5]")); // 5.0'dı, 0.5 yaptık
          appsAll.Add(httpNormal.Install(node));

          // 2. Arka Plan UDP Trafiği (Ses/Video vb.) - NEREDEYSE HİÇ DURMAZ
          // OffTime'ı çok küçülttük, sürekli veri akacak.
          OnOffHelper udpNormal("ns3::UdpSocketFactory", InetSocketAddress(serverIpv4, UDP_SINK_PORT));
          udpNormal.SetAttribute("DataRate", StringValue("100Kbps"));
          udpNormal.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=2.0]"));
          udpNormal.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.1]")); // Neredeyse kesintisiz
          appsAll.Add(udpNormal.Install(node));

          // 3. mDNS (Cihaz bulma) - DAHA SIK GÖRÜNSÜN
          // 10-20 saniye beklemek yerine 2-4 saniyede bir göndersin.
          OnOffHelper mdnsApp("ns3::UdpSocketFactory", InetSocketAddress(serverIpv4, MDNS_PORT));
          mdnsApp.SetAttribute("DataRate", StringValue("1Kbps"));
          mdnsApp.SetAttribute("PacketSize", UintegerValue(200));
          mdnsApp.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=0.1]"));
          mdnsApp.SetAttribute("OffTime", StringValue("ns3::UniformRandomVariable[Min=2.0|Max=4.0]")); // Sıklaştırdık (Pipe | işaretine dikkat)
          appsAll.Add(mdnsApp.Install(node));

          // 4. LLMNR (Yerel isim çözümleme) - DAHA SIK GÖRÜNSÜN
          OnOffHelper llmnrApp("ns3::UdpSocketFactory", InetSocketAddress(serverIpv4, LLMNR_PORT));
          llmnrApp.SetAttribute("DataRate", StringValue("1Kbps"));
          llmnrApp.SetAttribute("PacketSize", UintegerValue(150));
          llmnrApp.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=0.1]"));
          llmnrApp.SetAttribute("OffTime", StringValue("ns3::UniformRandomVariable[Min=3.0|Max=5.0]")); // Sıklaştırdık
          appsAll.Add(llmnrApp.Install(node));
      }
  }
  
  
  


  // Sadece OnOff (appsAll içindekiler) için start/stop veriyoruz.
  // SynFloodApp kendi içinde start aldığı için buraya eklememize gerek yoktu ama appsAll boşsa hata vermez.
  appsAll.Start(Seconds(2.0));                    
  appsAll.Stop(Seconds(MAX_SIMULATION_TIME));
  
  

  if (enablePing) {
    PingHelper ping(serverIpv4);
    ping.SetAttribute("Interval", TimeValue(Seconds(1.0)));
    ping.SetAttribute("Size", UintegerValue(1024));
    ping.Install(pings.Get(0)).Start(Seconds(PING_START_TIME));
    ping.Install(pings.Get(1)).Start(Seconds(PING_START_TIME));
  }

  // Pcap
  csma.EnablePcap(attackType + "_VictimLog", victim_cloudR.Get(0), true);

  // Animasyon Çizgileri (Görselleştirme)
  PointToPointHelper vis; 
  vis.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  vis.SetChannelAttribute("Delay", StringValue("1ms"));
  
  vis.Install(core.Get(0), cloudLeft); // Attacker
  for (uint32_t i=0; i<bots.GetN(); ++i) vis.Install(bots.Get(i), cloudLeft);
  vis.Install(cloudLeft, switch1);
  vis.Install(switch1, core.Get(1));   // Switch1 - R1
  vis.Install(pings.Get(0), switch1);
  
  vis.Install(core.Get(3), cloudRight); // Victim
  vis.Install(cloudRight, switch2);
  vis.Install(switch2, core.Get(2));    // Switch2 - R2
  vis.Install(pings.Get(1), switch2);

  // NetAnim Ayarları
  MobilityHelper mob; 
  mob.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mob.Install(core); mob.Install(bots); mob.Install(pings);
  mob.Install(NodeContainer(switch1, switch2, cloudLeft, cloudRight));

  AnimationInterface anim("DDoSim.xml");
  anim.SetMaxPktsPerTraceFile(1000000);

  anim.UpdateNodeDescription(core.Get(0), "Attacker");
  anim.UpdateNodeDescription(core.Get(1), "R1");
  anim.UpdateNodeDescription(core.Get(2), "R2");
  anim.UpdateNodeDescription(core.Get(3), "WebServer");
  anim.UpdateNodeDescription(pings.Get(0), "PC1");
  anim.UpdateNodeDescription(pings.Get(1), "PC2");
  anim.UpdateNodeDescription(switch1, "Switch1");
  anim.UpdateNodeDescription(switch2, "Switch2");
  anim.UpdateNodeDescription(cloudLeft, "Cloud1");
  anim.UpdateNodeDescription(cloudRight, "Cloud2");

  AnimationInterface::SetConstantPosition(core.Get(0), 0, 0);
  AnimationInterface::SetConstantPosition(core.Get(1), 10, 10);
  AnimationInterface::SetConstantPosition(core.Get(2), 20, 10);
  AnimationInterface::SetConstantPosition(core.Get(3), 30, 10);
  AnimationInterface::SetConstantPosition(pings.Get(0), 10, 20);
  AnimationInterface::SetConstantPosition(pings.Get(1), 20, 20);
  
  uint32_t botX = 0;
  for (int i=0; i<NUMBER_OF_BOTS; ++i) {
    AnimationInterface::SetConstantPosition(bots.Get(i), botX+=3, 30);
    anim.UpdateNodeDescription(bots.Get(i), ("Bot " + std::to_string(i+1)));
  }
  AnimationInterface::SetConstantPosition(switch1, 7, 10);
  AnimationInterface::SetConstantPosition(cloudLeft, 6, 12);
  AnimationInterface::SetConstantPosition(switch2, 23, 10);
  AnimationInterface::SetConstantPosition(cloudRight, 24, 12);

  Simulator::Run();
  Simulator::Destroy();
  return 0;
}

