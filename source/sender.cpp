#include "fielddrop_packet.hpp"

#include <iostream>
#include <string_view>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <cerrno>
#include <chrono>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>

namespace {
bool sendAndWait(int sock, const sockaddr_in& receiver,
                 const fielddrop::Packet& packet,
                 fielddrop::PacketType expectedType) {
    std::vector<std::uint8_t> bytes = fielddrop::encode(packet);

    if (bytes.empty()) {
        return false;
    }

    for (int attempt = 0; attempt < fielddrop::kMaxTransmissions; attempt++) {
        ssize_t sent = sendto(sock, bytes.data(), bytes.size(), 0,
                              (const sockaddr*)&receiver, sizeof(receiver));

        if (sent != (ssize_t)bytes.size()) {
            return false;
        }

        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(fielddrop::kTimeoutMs);

        while (std::chrono::steady_clock::now() < deadline) {
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();

            pollfd pfd = {};
            pfd.fd = sock;
            pfd.events = POLLIN;

            int ready = poll(&pfd, 1, (int)remaining);

            if (ready == 0) {
                break;
            }

            if (ready < 0) {
                if (errno == EINTR) continue;
                return false;
            }

            std::uint8_t buffer[1024];
            sockaddr_in source = {};
            socklen_t sourceLength = sizeof(source);

            ssize_t received = recvfrom(sock, buffer, sizeof(buffer), 0,
                                        (sockaddr*)&source, &sourceLength);

            if (received < 0) {
                if (errno == EINTR) continue;
                return false;
            }

            if (source.sin_addr.s_addr != receiver.sin_addr.s_addr ||
                source.sin_port != receiver.sin_port) {
                continue;
            }

            fielddrop::Packet reply;

            if (!fielddrop::parse(buffer, received, reply)) {
                continue;
            }

            if (reply.session_id == packet.session_id &&
                reply.sequence == packet.sequence &&
                reply.type == expectedType) {
                return true;
            }
        }
    }

    return false;
}

void usage(const char* program) {
    std::cerr << "Usage: " << program
              << " INPUT_FILE RECEIVER_HOST RECEIVER_PORT\n";
}
}

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        usage(argv[0]);
        return 0;
    }
    if (argc != 4) {
        usage(argv[0]);
        return 2;
    }

    
    // Check the port number
    char* end;
    long port = std::strtol(argv[3], &end, 10);

    if (*argv[3] == '\0' || *end != '\0' ||
        port < 1 || port > 65535) {
        std::cerr << "Invalid port number\n";
        return 1;
    }

    // Open and read the input file
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);

    if (!file) {
        std::cerr << "Cannot open input file\n";
        return 1;
    }

    std::streampos fileSize = file.tellg();

    if (fileSize < 0 ||
        fileSize > static_cast<std::streampos>(fielddrop::kMaxFileBytes)) {
        std::cerr << "Invalid file size\n";
        return 1;
    }

    file.seekg(0);

    std::vector<std::uint8_t> data(
        static_cast<std::size_t>(fileSize));

    if (!data.empty()) {
        file.read(reinterpret_cast<char*>(data.data()), data.size());

        if (!file) {
            std::cerr << "Cannot read input file\n";
            return 1;
        }
    }

    // Find the receiver's IPv4 address
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    addrinfo* result = nullptr;

    int status = getaddrinfo(argv[2], argv[3], &hints, &result);

    if (status != 0) {
        std::cerr << "Cannot find receiver\n";
        return 1;
    }

    sockaddr_in receiver =
        *reinterpret_cast<sockaddr_in*>(result->ai_addr);

    freeaddrinfo(result);

    // Create UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0) {
        std::cerr << "Cannot create socket\n";
        return 1;
    }

    // Create a nonzero session ID
    std::uint32_t session =
        static_cast<std::uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());

    if (session == 0) {
        session = 1;
    }

    // Send HELLO and wait for ACK 0
    fielddrop::Packet hello =
        fielddrop::makeHello(session, data.size());

    if (!sendAndWait(sock, receiver, hello,
                     fielddrop::PacketType::Ack)) {
        std::cerr << "HELLO failed\n";
        close(sock);
        return 1;
    }

    // Send DATA packets, one at a time
    std::uint32_t sequence = 1;

    for (std::size_t offset = 0; offset < data.size();
         offset += fielddrop::kMaxPayloadBytes) {

        std::size_t length =
            data.size() - offset;

        if (length > fielddrop::kMaxPayloadBytes) {
            length = fielddrop::kMaxPayloadBytes;
        }

        fielddrop::Packet packet =
            fielddrop::makeData(session, sequence,
                                data.data() + offset, length);

        if (!sendAndWait(sock, receiver, packet,
                         fielddrop::PacketType::Ack)) {
            std::cerr << "DATA failed at sequence "
                      << sequence << "\n";
            close(sock);
            return 1;
        }

        sequence++;
    }

    // Send FIN and wait for FIN_ACK
    fielddrop::Packet fin =
        fielddrop::makeFin(session, sequence);

    if (!sendAndWait(sock, receiver, fin,
                     fielddrop::PacketType::FinAck)) {
        std::cerr << "FIN failed\n";
        close(sock);
        return 1;
    }

    close(sock);

    std::cout << "File sent successfully\n";
    return 0;
}
