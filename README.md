### Gnutella style file transfer system in C

This repository contains the implementation of a Gnutella style peer-to-peer (P2P) system, implemented in C, that simulates a distributed file-sharing environment.

## Features

- **Peer-to-Peer Communication:** The project includes server and client components, enabling peer-to-peer file sharing.
- **Data Transfer:** Supports transfer of both small and large data files between peers.
- **Scalability:** Efficiently handles different data sizes using scripts to test performance with small and large datasets.
- **Modular Design:** The project is designed with modularity in mind to support easy testing and future enhancements.

## File Structure

- **peer-server.c**: Contains the code for the server component that manages peer connections and file sharing.
- **peer.c**: Implements the peer client, which connects to the server to upload and download files.
- **add_big_data.sh**: A script to simulate uploading large data files to the peer-to-peer network.
- **add_sm-data.sh**: A script to simulate uploading small data files to the peer-to-peer network.
- **config.txt**: Configuration file for setting up the network.
- **Makefile.txt**: Instructions for compiling the project.
- **pa2-report.pdf**: Detailed report on the project’s design, functionality, and performance analysis.
- **Output_Video.mp4**: A video demonstrating the working of the project.

## Getting Started

### Prerequisites

- **C Compiler**: Ensure you have GCC or any other compatible C compiler installed.
- **Bash**: The shell scripts (`.sh` files) are written for a Linux environment with bash.

### Installation and Compilation

1. Clone the repository:
   ```bash
   git clone https://github.com/your-username/cs550-fall2023-pa2-the-a-team.git
   cd cs550-fall2023-pa2-the-a-team
   ```

2. Compile the project using the provided Makefile:
   ```bash
   make -f Makefile.txt
   ```

### Running the Project

1. To start the peer server:
   ```bash
   ./peer-server
   ```

2. To add and transfer data between peers, use the provided scripts:
   - For large data:
     ```bash
     ./add_big_data.sh
     ```
   - For small data:
     ```bash
     ./add_sm-data.sh
     ```

### Configuration

Modify the **config.txt** file to set up the network configuration, including the server IP and port details.

### Output

- The **Output_Video.mp4** demonstrates the project’s successful execution, showing peer-to-peer communication and file transfers.

## Documentation

For a detailed explanation of the system architecture, design decisions, and performance metrics, please refer to the **pa2-report.pdf**.
