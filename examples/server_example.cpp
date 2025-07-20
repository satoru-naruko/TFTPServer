#include "tftp/tftp_server.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <limits>

// Flag for signal handling
std::atomic<bool> running(true);

// Signal handler
void signal_handler(int signal) {
    std::cout << "Signal received: " << signal << std::endl;
    running = false;
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    std::string root_dir = ".";
    uint16_t port = 69;

    if (argc > 1) {
        root_dir = argv[1];
    }
    
    if (argc > 2) {
        auto port_long = std::stoul(argv[2]);
        if (port_long > std::numeric_limits<uint16_t>::max()) {
            std::cerr << "Port number out of range: " << port_long << std::endl;
            return 1;
        }
        port = static_cast<uint16_t>(port_long);
    }

    // Set up signal handlers
    signal(SIGINT, signal_handler);  // Ctrl+C
    signal(SIGTERM, signal_handler); // Termination signal

    try {
        // Create and configure TFTP server
        tftpserver::TftpServer server(root_dir, port);
        
        // Enable secure mode
        server.SetSecureMode(true);
        
        // Set timeout
        server.SetTimeout(5);
        
        // Set maximum transfer size (2MB)
        server.SetMaxTransferSize(2 * 1024 * 1024);
        
        // Start the server
        std::cout << "Starting TFTP server (root directory: " << root_dir 
                  << ", port: " << port << ")..." << std::endl;
        
        if (!server.Start()) {
            std::cerr << "Failed to start the server" << std::endl;
            return 1;
        }
        
        std::cout << "TFTP server started" << std::endl;
        std::cout << "Press Ctrl+C to exit" << std::endl;
        
        // Run the server (wait for signal)
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Stop the server
        std::cout << "Stopping TFTP server..." << std::endl;
        server.Stop();
        std::cout << "TFTP server stopped" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
} 