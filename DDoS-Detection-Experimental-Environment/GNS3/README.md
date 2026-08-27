# GNS3 Experimental Environment

This directory contains the files related to the **GNS3 experimental environment** used in the study entitled **“Testbed Selection in Network Security: An Empirical Comparison of Simulator (NS-3) and Emulator (GNS3) Under DDoS Attack Scenarios.”**

The GNS3 environment was used to conduct DDoS attack scenarios and generate attack traffic. This directory provides the network topology, network configurations, and codes used in the attack scenario.

## Directory Structure

```text
GNS3/
├── topology/
├── network-config/
├── attack/
└── README.md
```

## 1. Network Topology

The `topology/` directory contains the GNS3 project file of the network topology used in the experiments.

This file contains the network devices, connections between the devices, and the overall network structure of the experimental environment used in GNS3.

## 2. Network Configurations

The `network-config/` directory contains the router configurations used in the GNS3 experimental environment.

This directory provides the commands and relevant network settings used to configure the router devices.

These configuration files enable the routers in the experimental environment to be configured according to the network structure used in the study.

## 3. DDoS Attack Codes

The `attack/` directory contains the codes used to generate DDoS attack traffic in the GNS3 environment.

This directory contains:

- **Bot code**
- **Command and Control (C&C) code**

The Bot code represents the component used to generate attack traffic in the experimental environment, while the C&C code represents the component used to control the Bot component.

These codes were used to generate DDoS attack traffic within the controlled experimental environment defined in the study.

## 4. File Purpose

| Directory | Contents |
|---|---|
| `topology/` | GNS3 experimental topology |
| `network-config/` | Router configurations |
| `attack/` | Bot and Command and Control (C&C) codes |

## Reproducibility

The files provided in this directory are intended to facilitate the examination and reproduction of the GNS3 experimental environment used in the study.
