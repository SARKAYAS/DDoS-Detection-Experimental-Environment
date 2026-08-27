#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/netanim-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/mobility-module.h"
#include <iostream>

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE("DdosSimulation");

// Yardımcı: IP adresini string'e çevirir (Görselleştirme için)
std::string Ipv4ToString(ns3::Ipv4Address ip)
{
    std::ostringstream oss;
    ip.Print(oss);
    return oss.str();
}

// Yardımcı: Düğümün gerçek IP'sini bulur (Loopback hariç)
Ipv4Address GetRealIp(Ptr<Node> node)
{
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    for (uint32_t i = 0; i < ipv4->GetNInterfaces(); i++) {
        for (uint32_t j = 0; j < ipv4->GetNAddresses(i); j++) {
            Ipv4InterfaceAddress addr = ipv4->GetAddress(i, j);
            if (addr.GetLocal() != Ipv4Address("127.0.0.1"))
                return addr.GetLocal();
        }
    }
    return Ipv4Address("0.0.0.0");
}

// Saldırı Fonksiyonu: UDP veya TCP Flood
void InstallAttack(NodeContainer attackers, std::string socketType,
                        Ipv4Address serverIp, uint16_t port, std::string dataRate, std::string packetSize)
{
    Address dst(InetSocketAddress(serverIp, port));
    OnOffHelper onoff(socketType, dst);
    
    // Sürekli saldırı için OnTime 1, OffTime 0
    onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    onoff.SetAttribute("DataRate", StringValue(dataRate));
    onoff.SetAttribute("PacketSize", StringValue(packetSize)); // Payload boyutu

    // Botlar saldırıya 2. saniyede başlar
    ApplicationContainer apps = onoff.Install(attackers);
    apps.Start(Seconds(2.0));
    apps.Stop(Seconds(15.0));
}

// Saldırı Fonksiyonu: SYN Flood Simülasyonu
// NS-3'te gerçek SYN paketi oluşturmak zor olduğu için, çok kısa bağlantılar açıp kapatan
// yüksek frekanslı TCP istekleri ile simüle edilir.
void InstallSynFlood(NodeContainer attackers, Ipv4Address serverIp, uint16_t port)
{
    Address dst(InetSocketAddress(serverIp, port));

    for (uint32_t i = 0; i < attackers.GetN(); i++) {
        OnOffHelper onoff("ns3::TcpSocketFactory", dst);
        // Çok kısa On/Off süreleri ile sürekli bağlantı isteği simülasyonu
        onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=0.001]"));
        onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.001]"));
        onoff.SetAttribute("DataRate", StringValue("1Mbps")); // Hızdan ziyade paket sayısı önemli
        onoff.SetAttribute("PacketSize", UintegerValue(64)); // Küçük paketler

        ApplicationContainer app = onoff.Install(attackers.Get(i));
        // Her bot biraz farklı zamanda başlasın
        app.Start(Seconds(2.0 + (i * 0.05))); 
        app.Stop(Seconds(15.0));
    }
}


// Bu fonksiyon bot "emir" paketini aldığında tetiklenecek
void BotReceiveCommand(Ptr<Socket> socket, Ipv4Address serverIp, uint16_t port, std::string protocol, std::string dataRate)
{
    // 1. Gelen emri (paketi) soketten oku
    Ptr<Packet> packet = socket->Recv();
    std::cout << ">> [BOT] Emir alindi! Saldiri baslatiliyor... Node ID: " 
              << socket->GetNode()->GetId() << std::endl;

    // 2. Saldırı Uygulamasını (OnOffHelper) o anda oluştur ve başlat
    Address dst(InetSocketAddress(serverIp, port));
    OnOffHelper onoff(protocol, dst);
    
    onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
    onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
    onoff.SetAttribute("DataRate", StringValue(dataRate));
    onoff.SetAttribute("PacketSize", StringValue("1024"));

    // Uygulamayı kur ve hemen (0. saniyede) başlat
    // Not: Buradaki 0. saniye, şimdiki zaman (Now) anlamına gelir.
    ApplicationContainer app = onoff.Install(socket->GetNode());
    app.Start(Seconds(0.0)); 
}

void SetupBotListeners(NodeContainer bots, Ipv4Address serverIp, uint16_t serverPort, std::string protocol, std::string dataRate)
{
    for (uint32_t i = 0; i < bots.GetN(); ++i)
    {
        Ptr<Node> node = bots.Get(i);
        
        // UDP Soketi oluştur
        Ptr<Socket> socket = Socket::CreateSocket(node, UdpSocketFactory::GetTypeId());
        
        // Port 9999'u dinle (Komuta Portu)
        InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), 9999);
        socket->Bind(local);

        // Paket geldiğinde "BotReceiveCommand" fonksiyonunu çalıştır
        // MakeBoundCallback ile saldırı parametrelerini (hedef IP vs) fonksiyona taşıyoruz
        socket->SetRecvCallback(MakeBoundCallback(&BotReceiveCommand, serverIp, serverPort, protocol, dataRate));
    }
}

int main(int argc, char* argv[])
{
    // ---------------- KULLANICI GİRİŞİ (MENÜ) ----------------
    int attackChoice;
    std::cout << "========================================" << std::endl;
    std::cout << "      DDoS Simülasyonu - Hosgeldin Aysegul" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Lutfen saldiri tipini seciniz:" << std::endl;
    std::cout << "1. HTTP DDoS (TCP tabanli, Port 80)" << std::endl;
    std::cout << "2. TCP Flood (Yuksek hizli TCP)" << std::endl;
    std::cout << "3. UDP Flood (Yuksek hizli UDP)" << std::endl;
    std::cout << "4. SYN Flood Simülasyonu" << std::endl;
    std::cout << "Seciminiz (1-4): ";
    std::cin >> attackChoice;

    // ---------------- NODE CREATION ----------------
    // Toplam düğüm sayılarını tanımlayalım
    // Sol taraf: Attacker(1), Bot(3), PC1(1), Router1(1)
    // Sağ taraf: Router2(1), WebServer(1), PC2(1)
    
    NodeContainer attackers; attackers.Create(1); // 192.168.10.5 (hedef)
    NodeContainer bots; bots.Create(3);           // .7, .10, .6
    NodeContainer pc1; pc1.Create(1);             // .20
    NodeContainer router1; router1.Create(1);     // .1
    
    NodeContainer router2; router2.Create(1);     // 22.22.22.1 (Gateway)
    NodeContainer webServer; webServer.Create(1); // .21
    NodeContainer pc2; pc2.Create(1);             // .24

    // İsimlendirme (NetAnim için)
    Names::Add("Attacker", attackers.Get(0));
    Names::Add("Bot-1", bots.Get(0));
    Names::Add("Bot-2", bots.Get(1));
    Names::Add("Bot-3", bots.Get(2));
    Names::Add("PC-1", pc1.Get(0));
    Names::Add("Router-1", router1.Get(0));
    Names::Add("Router-2", router2.Get(0));
    Names::Add("Web-Server", webServer.Get(0));
    Names::Add("PC-2", pc2.Get(0));

    // Tüm node'ları tek bir konteynerda toplayalım (Mobility ve Stack kurulumu için)
    NodeContainer allNodes;
    allNodes.Add(attackers); allNodes.Add(bots); allNodes.Add(pc1);
    allNodes.Add(router1); allNodes.Add(router2);
    allNodes.Add(webServer); allNodes.Add(pc2);

    // ---------------- INTERNET STACK ----------------
    InternetStackHelper stack;
    stack.Install(allNodes);

    // ---------------- TOPOLOGY & CONNECTIONS ----------------
    
    // 1. Router 1 <-> Router 2 (Point-to-Point)
    // R1 (81.128.81.130) <---> R2 (81.128.81.129) 
    // Network: 81.128.81.128/30
    
    NodeContainer r1r2Container;
    r1r2Container.Add(router2.Get(0)); // Önce R2'yi ekliyoruz ki .129'u o alsın (sıralama önemli)
    r1r2Container.Add(router1.Get(0)); // R1 .130 alacak

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("5ms"));

    NetDeviceContainer p2pDevices = p2p.Install(r1r2Container);

    Ipv4AddressHelper address;
    address.SetBase("81.128.81.128", "255.255.255.252");
    Ipv4InterfaceContainer p2pInterfaces = address.Assign(p2pDevices);

    // 2. Sol Taraf (LAN 1) -> Switch 2
    // Router1, Attacker, Bots, PC1
    // Network: 192.168.10.0/24
    
    CsmaHelper csmaLeft;
    csmaLeft.SetChannelAttribute("DataRate", StringValue("100Mbps"));
    csmaLeft.SetChannelAttribute("Delay", StringValue("2ms"));

    NodeContainer leftLanNodes;
    leftLanNodes.Add(router1.Get(0)); // R1'i başa ekleyelim ki .1'i almaya çalışsın
    leftLanNodes.Add(pc1.Get(0));
    leftLanNodes.Add(attackers.Get(0));
    leftLanNodes.Add(bots);

    NetDeviceContainer leftDevices = csmaLeft.Install(leftLanNodes);

    address.SetBase("192.168.10.0", "255.255.255.0");
    Ipv4InterfaceContainer leftInterfaces = address.Assign(leftDevices);

    // 3. Sağ Taraf (LAN 2) -> Switch 1
    // Router2, PC2, WebServer
    // Network: 22.22.22.0/24

    CsmaHelper csmaRight;
    csmaRight.SetChannelAttribute("DataRate", StringValue("100Mbps"));
    csmaRight.SetChannelAttribute("Delay", StringValue("2ms"));

    NodeContainer rightLanNodes;
    rightLanNodes.Add(router2.Get(0)); // R2'yi başa ekleyelim (.1)
    rightLanNodes.Add(webServer.Get(0));
    rightLanNodes.Add(pc2.Get(0));

    NetDeviceContainer rightDevices = csmaRight.Install(rightLanNodes);

    address.SetBase("22.22.22.0", "255.255.255.0");
    Ipv4InterfaceContainer rightInterfaces = address.Assign(rightDevices);

    // Routing Tablolarını Oluştur
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Hedef IP (Web Server)
    Ipv4Address serverIp = GetRealIp(webServer.Get(0));
    uint16_t port = 80;

    std::cout << "Web Server IP: " << serverIp << std::endl;

    // ---------------- APPLICATIONS (SERVER SIDE) ----------------
    // Sunucu hem TCP hem UDP dinlesin
    PacketSinkHelper sinkTcp("ns3::TcpSocketFactory",
                             InetSocketAddress(Ipv4Address::GetAny(), port));
    sinkTcp.Install(webServer.Get(0));

    PacketSinkHelper sinkUdp("ns3::UdpSocketFactory",
                             InetSocketAddress(Ipv4Address::GetAny(), port));
    sinkUdp.Install(webServer.Get(0));


    // ---------------- APPLICATIONS (ATTACKER SIDE) 
    // ---------------- APPLICATIONS (ATTACKER SIDE) ----------------
    // Saldırgan grubunu oluştur (Attacker + Botlar)
    NodeContainer attackGroup;
    attackGroup.Add(attackers);
    attackGroup.Add(bots);

    // Hedef IP (Web Server)
    Ipv4Address serverIp = GetRealIp(webServer.Get(0));
    uint16_t port = 80;

    // Seçime göre saldırı parametrelerini belirle
    std::string attackProto = "ns3::TcpSocketFactory"; // Varsayılan: TCP
    std::string attackRate = "500Kbps";
    std::string packetSize = "1024";

    if (attackChoice == 3) {
        attackProto = "ns3::UdpSocketFactory";
        attackRate = "50Mbps"; // UDP Flood için yüksek hız
    } else if (attackChoice == 2) {
        attackProto = "ns3::TcpSocketFactory";
        attackRate = "50Mbps"; // TCP Flood için yüksek hız
    } else if (attackChoice == 4) {
        // SYN Flood için farklı bir uygulama tipi ve kurulumu gerektiğinden, 
        // bu seçimde botlar manuel olarak SYN Flood simülasyonunu başlatacak.
        attackProto = ""; // Boş bırak
    }

    // 1. Botları "Emir Dinleyici" moduna geçir (Port 9999)
    // Botlar, Master'dan gelecek komutu beklemeye başlar.
    if (attackChoice != 4) {
        // SYN Flood farklı bir kurulum kullandığı için sadece UDP/TCP botlarını kuruyoruz
        SetupBotListeners(bots, serverIp, port, attackProto, attackRate);
        std::cout << ">> Botlar " << (attackProto.find("Udp") != std::string::npos ? "UDP" : "TCP") << " saldirisi icin emir bekliyor." << std::endl;
    }


    // 2. Saldırgan (Master) belirli bir zamanda Botlara "Komuta Paketi" atar.
    Ptr<Socket> masterSocket = Socket::CreateSocket(attackers.Get(0), UdpSocketFactory::GetTypeId());

    // Botların her birine ayrı ayrı komut gönderimi zamanlaması (2.0. saniyede)
    for(uint32_t i=0; i<bots.GetN(); i++) {
        Ptr<Node> botNode = bots.Get(i);
        Ipv4Address botIp = GetRealIp(botNode);
        
        // Simülatöre, 2.0. saniyede komut gönderme işlemini zamanlıyoruz.
        Simulator::Schedule(Seconds(2.0 + (i * 0.01)), [masterSocket, botIp]() {
            masterSocket->Connect(InetSocketAddress(botIp, 9999));
            // Komutun içeriği önemsiz, paketin varlığı önemli.
            Ptr<Packet> pkt = Create<Packet>(10); 
            masterSocket->Send(pkt);
            // Not: masterSocket'in bot'a ulaşma gecikmesi 2.0. saniyeden sonra devreye girecektir.
            std::cout << ">> [MASTER] Bot (" << botIp << ") adresine saldiri emri gonderildi." << std::endl;
        });
    }


    // 3. Saldırganın (Master'ın) kendisi de saldırıya başlasın (Botları tetiklemesinden hemen sonra)
    NodeContainer justMaster; 
    justMaster.Add(attackers.Get(0));

    switch (attackChoice) {
        case 1: 
        case 2:
        case 3:
            // Saldırgan (Master) da saldırmak için botlarla aynı OnOff ayarlarını kullanır.
            InstallAttack(justMaster, attackProto, serverIp, port, attackRate, packetSize);
            break;
        case 4: // SYN Flood Simülasyonu
            // SYN Flood için Master ve Botlar için farklı fonksiyon çağrılır.
            std::cout << ">> SYN Flood Saldirisi baslatiliyor..." << std::endl;
            InstallSynFlood(attackGroup, serverIp, port);
            // NOT: SYN Flood'da C2 yapısı kurmak zordur, bu yüzden şimdilik tüm grup aynı anda başlıyor.
            break;
        default:
            std::cout << "Gecersiz secim. Varsayilan (HTTP) uygulaniyor." << std::endl;
            InstallAttack(justMaster, "ns3::TcpSocketFactory", serverIp, port, "200Kbps", "1024");
            break;
    }
    // ---------------- APPLICATIONS (ATTACKER SIDE) BİTİŞİ ----------------


    // ---------------- GÖRSELLEŞTİRME & MOBILITY ----------------
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(allNodes);

    // Düğümleri görselde güzel dursun diye elle yerleştiriyoruz
    // Koordinatlar (x, y)
    Ptr<ConstantPositionMobilityModel> loc;
    
    // Sol Taraf
    loc = attackers.Get(0)->GetObject<ConstantPositionMobilityModel>(); loc->SetPosition(Vector(10, 20, 0));
    loc = bots.Get(0)->GetObject<ConstantPositionMobilityModel>(); loc->SetPosition(Vector(10, 30, 0));
    loc = bots.Get(1)->GetObject<ConstantPositionMobilityModel>(); loc->SetPosition(Vector(10, 40, 0));
    loc = bots.Get(2)->GetObject<ConstantPositionMobilityModel>(); loc->SetPosition(Vector(10, 50, 0));
    loc = pc1.Get(0)->GetObject<ConstantPositionMobilityModel>(); loc->SetPosition(Vector(10, 60, 0));
    
    // Routerlar
    loc = router1.Get(0)->GetObject<ConstantPositionMobilityModel>(); loc->SetPosition(Vector(30, 40, 0));
    loc = router2.Get(0)->GetObject<ConstantPositionMobilityModel>(); loc->SetPosition(Vector(50, 40, 0));

    // Sağ Taraf
    loc = webServer.Get(0)->GetObject<ConstantPositionMobilityModel>(); loc->SetPosition(Vector(70, 30, 0));
    loc = pc2.Get(0)->GetObject<ConstantPositionMobilityModel>(); loc->SetPosition(Vector(70, 50, 0));

    AnimationInterface anim("ddos_aysegul_sim.xml");
    // Node etiketlerini güncelle
    for (uint32_t i = 0; i < allNodes.GetN(); i++) {
        Ptr<Node> n = allNodes.Get(i);
        std::string label = Names::FindName(n); 
        label += "\n" + Ipv4ToString(GetRealIp(n));
        anim.UpdateNodeDescription(n, label);
    }

    // ---------------- TRACING (PCAP) ----------------
    // Wireshark için kayıt
    // Router1 ve Router2 arasındaki trafiği kaydet
    p2p.EnablePcap("ddos-router-link", p2pDevices.Get(0), true);
    
    // Server'a gelen trafiği kaydet
    csmaRight.EnablePcap("ddos-server-side", rightDevices.Get(1), true);

    std::cout << "Simulasyon calisiyor (15 saniye)..." << std::endl;

    // ---------------- RUN ----------------
    Simulator::Stop(Seconds(15.0));
    Simulator::Run();
    Simulator::Destroy();

    std::cout << "Simulasyon tamamlandi. XML ve PCAP dosyalari olusturuldu." << std::endl;

    return 0;
}