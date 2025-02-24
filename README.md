# Distributed File Sharing System

A BitTorrent-like file sharing system implemented in **C**, using **MPI** for distributed communication and **pthread** for multi-threaded operations. This project simulates file uploading and downloading in a peer-to-peer (P2P) network, featuring a centralized tracker and multiple peers (clients).

---

## Project Overview

The system consists of two main components:
1. **Tracker**: A centralized server that manages file metadata, client information, and swarm updates.
2. **Peer (Client)**: Clients that upload and download file segments from each other, coordinated by the tracker.

The system is designed to efficiently distribute file segments among clients, optimizing load balancing and ensuring reliable communication using **TCP-like** protocols.

---

## Key Features

### Tracker
- Collects file metadata from clients at startup and sends an **ACK** to confirm registration.
- Maintains a centralized list of available files and their associated swarms.
- Responds to client requests with swarm information and upload statistics, sorted by the number of segments uploaded.
- Monitors and updates the number of segments uploaded by each client, ensuring accurate load balancing.
- Notifies clients when all downloads are complete, gracefully shutting down the system.

### Peer (Client)
- Reads owned and desired files from an input file (`inX.txt`, where `X` is the client's rank).
- Sends file metadata to the tracker and starts two threads:
  - **Download Thread**:
    - Requests swarm information from the tracker every 10 downloaded segments (`REQ_SWARM`).
    - Downloads missing segments from other clients in the swarm, prioritizing clients with fewer uploads.
    - Saves downloaded segments to a local file upon completion.
    - Notifies the tracker when all downloads are complete (`CLIENT_FIN`).
  - **Upload Thread**:
    - Handles three types of messages:
      - `REQ_CHUNK`: Responds to segment requests from other clients.
      - `REQ_UPLD_INFO`: Reports the number of segments uploaded to the tracker.
      - `CLOSE`: Terminates the thread gracefully.

---

## Implementation Details

### Data Structures
- **SwarmFile**: Contains metadata about a file in the swarm (name, number of segments, segment hashes, and client types: leech or seed).
- **FileInfo**: Stores information about files owned by a client.
- **ClientUploadInfo**: Tracks the number of segments uploaded by each client for load balancing.

### Core Functions
- `read_input_file`: Reads owned and desired files from an input file.
- `save_file`: Saves a downloaded file locally.
- `add_client_to_swarm`: Adds a client to a swarm.
- `download_thread_func`: Manages the file download process.
- `upload_thread_func`: Handles segment requests from other clients.
- `tracker`: Implements swarm management and client-tracker communication.
- `peer`: Initializes client files, starts threads, and communicates with the tracker.

### Optimizations
- **Wildcard Support**: Clients can subscribe to topics using wildcard patterns.
- **Efficient Networking**: Disables the Nagle algorithm for real-time communication.
- **Resource Management**: Uses static memory allocation for file descriptors and topic lists, with a limit on concurrent clients.
- **Message Tagging**: Ensures messages are routed correctly using unique tags, preventing interference.

---

## Technologies Used
- **C Programming**: Low-level implementation for performance and control.
- **MPI (Message Passing Interface)**: Facilitates distributed communication between clients and the tracker.
- **pthread**: Manages multi-threaded operations for concurrent uploads and downloads.

---

## How to Run
1. Use the Makefile (make) inside the /src directory.
2. Run the ./run_with_docker.sh or the /checker/checker.sh script to test the code or manually run the executable created using the specified format.

---

## Observations
- The system ensures **data integrity** and **efficient load balancing** by prioritizing clients with fewer uploads.
- **Graceful shutdown** is implemented to notify clients when all downloads are complete.
- Unique message tags prevent interference and ensure proper message routing.
