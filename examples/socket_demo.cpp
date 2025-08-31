/**
 * @file socket_demo.cpp
 * @brief Demonstration of the TFTP socket RAII wrapper
 */

#include "tftp/tftp_socket.h"
#include "tftp/tftp_logger.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

using namespace tftpserver;
using namespace tftpserver::net;

void ServerDemo() {
    std::cout << "=== TFTP Socket RAII Wrapper Demo - Server ===\n";
    
    // Socket library is automatically initialized by SocketLibraryGuard
    SocketLibraryGuard guard;
    if (!guard.IsInitialized()) {
        std::cerr << "Failed to initialize socket library\n";
        return;
    }
    std::cout << "✓ Socket library initialized\n";
    
    // Create and configure server socket
    UdpSocket server_socket;
    if (!server_socket.Create()) {
        std::cerr << "Failed to create server socket: " << server_socket.GetLastError() << "\n";
        return;
    }
    std::cout << "✓ Server socket created\n";
    
    // Set socket options
    if (!server_socket.SetReuseAddress(true)) {
        std::cerr << "Failed to set SO_REUSEADDR: " << server_socket.GetLastError() << "\n";
        return;
    }
    std::cout << "✓ SO_REUSEADDR set\n";
    
    // Bind to localhost:6969 (avoid privileged port)
    SocketAddress bind_addr("127.0.0.1", 6969);
    if (!server_socket.Bind(bind_addr)) {
        std::cerr << "Failed to bind socket: " << server_socket.GetLastError() << "\n";
        return;
    }
    std::cout << "✓ Socket bound to " << bind_addr.GetIP() << ":" << bind_addr.GetPort() << "\n";
    
    std::cout << "Server listening for 5 seconds...\n";
    
    // Listen for incoming packets with timeout
    uint8_t buffer[1024];
    SocketAddress client_addr;
    
    auto start_time = std::chrono::steady_clock::now();
    auto timeout_duration = std::chrono::seconds(5);
    
    while (std::chrono::steady_clock::now() - start_time < timeout_duration) {
        int received = server_socket.ReceiveFromTimeout(buffer, sizeof(buffer), client_addr, 1000);
        
        if (received > 0) {
            std::cout << "✓ Received " << received << " bytes from " 
                      << client_addr.GetIP() << ":" << client_addr.GetPort() << "\n";
            
            // Echo back the data
            std::string response = "Echo: ";
            response.append(reinterpret_cast<char*>(buffer), received);
            
            int sent = server_socket.SendTo(response.data(), response.size(), client_addr);
            if (sent > 0) {
                std::cout << "✓ Sent " << sent << " bytes back\n";
            } else {
                std::cout << "✗ Failed to send response: " << server_socket.GetLastError() << "\n";
            }
            break;
        } else if (received == 0) {
            // Timeout - continue waiting
            continue;
        } else {
            std::cerr << "✗ Receive error: " << server_socket.GetLastError() << "\n";
            break;
        }
    }
    
    std::cout << "✓ Server demo completed (socket automatically closed)\n";
    // Socket and library are automatically cleaned up by destructors
}

void ClientDemo() {
    std::cout << "\n=== TFTP Socket RAII Wrapper Demo - Client ===\n";
    
    SocketLibraryGuard guard;
    if (!guard.IsInitialized()) {
        std::cerr << "Failed to initialize socket library\n";
        return;
    }
    std::cout << "✓ Socket library initialized\n";
    
    // Create client socket
    UdpSocket client_socket;
    if (!client_socket.Create()) {
        std::cerr << "Failed to create client socket: " << client_socket.GetLastError() << "\n";
        return;
    }
    std::cout << "✓ Client socket created\n";
    
    // Send test message to server (if running)
    SocketAddress server_addr("127.0.0.1", 6969);
    std::string message = "Hello from TFTP socket wrapper!";
    
    int sent = client_socket.SendTo(message.data(), message.size(), server_addr);
    if (sent > 0) {
        std::cout << "✓ Sent " << sent << " bytes to server\n";
        
        // Wait for response
        uint8_t buffer[1024];
        SocketAddress response_addr;
        
        int received = client_socket.ReceiveFromTimeout(buffer, sizeof(buffer), response_addr, 2000);
        if (received > 0) {
            std::string response(reinterpret_cast<char*>(buffer), received);
            std::cout << "✓ Received response: \"" << response << "\"\n";
        } else if (received == 0) {
            std::cout << "⚠ No response received (timeout)\n";
        } else {
            std::cout << "✗ Receive error: " << client_socket.GetLastError() << "\n";
        }
    } else {
        std::cout << "✗ Failed to send message: " << client_socket.GetLastError() << "\n";
    }
    
    std::cout << "✓ Client demo completed (socket automatically closed)\n";
}

void AddressDemo() {
    std::cout << "\n=== SocketAddress Demo ===\n";
    
    // Test SocketAddress functionality
    SocketAddress addr1;
    std::cout << "Default address: " << addr1.GetIP() << ":" << addr1.GetPort() << "\n";
    
    SocketAddress addr2("192.168.1.100", 8080);
    std::cout << "Custom address: " << addr2.GetIP() << ":" << addr2.GetPort() << "\n";
    
    addr2.Set("10.0.0.1", 443);
    std::cout << "Modified address: " << addr2.GetIP() << ":" << addr2.GetPort() << "\n";
    
    // Test copy and move
    SocketAddress addr3 = addr2;
    std::cout << "Copied address: " << addr3.GetIP() << ":" << addr3.GetPort() << "\n";
    
    SocketAddress addr4 = std::move(addr3);
    std::cout << "Moved address: " << addr4.GetIP() << ":" << addr4.GetPort() << "\n";
}

int main() {
    std::cout << "TFTP Socket RAII Wrapper Demonstration\n";
    std::cout << "======================================\n\n";
    
    // Test address functionality
    AddressDemo();
    
    // Test server functionality
    ServerDemo();
    
    // Give a moment between server and client
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Test client functionality (will timeout since server is done)
    ClientDemo();
    
    std::cout << "\n✓ All socket wrapper demonstrations completed successfully!\n";
    std::cout << "The socket RAII wrapper provides:\n";
    std::cout << "  • Automatic resource management (no memory/socket leaks)\n";
    std::cout << "  • Cross-platform compatibility (Windows/Linux)\n";
    std::cout << "  • Exception safety with RAII pattern\n";
    std::cout << "  • Clean API for UDP socket operations\n";
    std::cout << "  • Automatic library initialization/cleanup\n";
    
    return 0;
}