# Experimental Environments for DDoS Attack Traffic Generation

This GitHub repository has been prepared to facilitate the reproduction of the **experimental environments, network topologies, and configuration files used in DDoS attack experiments conducted in GNS3 and NS-3 environments**.

This repository contains the files used to establish the experimental environments and generate attack traffic described in the corresponding academic study.

## Repository Contents

The repository is divided into two main experimental environments:

- **GNS3:** Network topology, router configurations, and Bot and Command and Control (C&C) codes used to generate DDoS attack traffic.
- **NS-3:** Commands required to install and configure the experimental environment.

## Repository Structure

```text
.
├── GNS3/
│   ├── topology/
│   ├── network-config/
│   ├── attack/
│   └── README.md
│
├── NS3/
│   ├── code/
│   └── README.md
│
└── README.md
```

## GNS3 Experimental Environment

The `GNS3/` directory contains the files related to the GNS3 environment used in the DDoS attack experiments.

This directory contains:

- GNS3 network topology,
- Router configurations,
- Bot code,
- Command and Control (C&C) code.

For detailed information about the GNS3 experimental environment, please refer to the `GNS3/README.md` file.

## NS-3 Experimental Environment

The `NS3/` directory contains the files related to the NS-3 environment used in the experiments.

This directory contains:

- Commands required to install and configure the NS-3 environment.

For detailed information about the NS-3 experimental environment, please refer to the `NS3/README.md` file.

## Reproducibility

The files provided in this repository are intended to facilitate the reproduction of the experimental environments used in the corresponding study.

The topology, configuration, and environment setup information required for the experiments is provided in the respective directories.

## Related Study

This repository supports the experimental environments used in the following academic study:

**Testbed Selection in Network Security: An Empirical Comparison of Simulator (NS-3) and Emulator (GNS3) Under DDoS Attack Scenarios**

## License

The contents of this repository are distributed under **GNU GPLv3**.
