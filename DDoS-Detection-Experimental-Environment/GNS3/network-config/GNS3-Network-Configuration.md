# Basic-Network-Configuration-with-GNS3

In the simulation environment, we first set up the desired topology. To enable communication between devices on two different networks for a DDoS attack, I set up the following topology.

<img width="987" height="867" alt="image" src="https://github.com/user-attachments/assets/0b05fdce-54e1-4c2c-a725-5c005cdbc379" />

### DDoS Attack

In this DDoS attack simulation, the botnet and the web server are in different networks. Bot devices and the attacker are located in the 192.168.10.0/24 network. The target web server is located in the 22.22.22.0/24 network.

> *The Cloud object connects VMware device VMnet addresses with the GNS3 network address.*

To control communication between these networks, I added VPC devices to each network. You can use the following commands to assign IP addresses to the VPC devices:
```bash
ip 22.22.22.24/22.22.22.1  
ip 192.168.10.20 255.255.255.0
```
To check the IP address and subnet mask, you can use the following command:
```bash
show ip
```

<img width="720" height="369" alt="image" src="https://github.com/user-attachments/assets/c66cc22e-3fbb-43b1-8c51-155b6967115b" />

### VPC IP Address

To open the router’s console screen, you need to right-click and open the console.


### Router Console

To assign IP addresses to the routers, enter the following commands in the console screen in order:
<img width="665" height="682" alt="image" src="https://github.com/user-attachments/assets/c920cbaa-225f-4df0-b4a6-85fb6ac38079" />

```bash
R1# config T  
R1(config)# int f0/0  
R1(config-if)# ip add 22.22.22.1 255.255.255.0  
R1(config-if)# no sh  
R1(config-if)# int s0/0  
R1(config-if)# ip add 81.128.81.129 255.255.255.0  
R1(config-if)# router rip  
R1(config-router)# ver2  
R1(config-router)# net 22.22.22.0  
R1(config-router)# net 82.128.82.0
```

<img width="720" height="323" alt="image" src="https://github.com/user-attachments/assets/79e22bc2-67ad-42a3-9da7-92e9f8d494d7" />



### Router IP Address

To check the IP addresses added to the router, you can enter the following command in the console to view the interfaces and IP addresses:

```bash
R1# show ip int brief
```

<img width="720" height="153" alt="image" src="https://github.com/user-attachments/assets/dfe7391f-8f65-48f7-8d84-aca273ff9780" />

### Router Network

Router and VPC device configurations are done. After configuring the necessary VMnet settings on VMware devices, the only thing left is for the two different networks to see each other.

First, default gateway settings need to be configured on the bot devices. On the virtual machines in the bot network, we set the gateway addresses by entering the following command in the terminal:

```bash
sudo ip route add default via 192.168.10.1
```
To set the gateway of the web server, use the following command in the web server’s terminal:
```bash
sudo ip route add default via 22.22.22.1
```
If these gateways already exist but are different, you can first clear them with this command:
```bash
sudo ip route del default
```
We have now configured the gateway settings on the VMware devices, meaning they can see their respective router addresses. Now it’s time to add routes to devices in different networks. First, we add the botnet network route to the web server using the following command:

```bash
sudo ip route add 192.168.10.0/24 via 22.22.22.1
```
We can also define the web server’s route in the botnet network using the command below:

```bash
sudo ip route add 22.22.22.0/24 via 192.168.10.1
```
If devices on the routers cannot see each other, it might be due to an outdated or corrupted ARP table. You can refresh the ARP table by entering the following commands in the router consoles:

```bash
R2# clear arp
```

or
```bash
R2# clear arp-cache
```
