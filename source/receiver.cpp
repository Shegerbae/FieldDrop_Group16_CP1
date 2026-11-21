#include "fielddrop_packet.hpp"

#include <iostream>
#include <string_view>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <chrono>
#include <cstdio>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>

namespace {
void usage(const char* program) {
    std::cerr << "Usage: " << program << " OUTPUT_FILE LISTEN_PORT\n";
}
bool sendPacket(int sock, const sockaddr_in& sender,
                const fielddrop::Packet& packet) {
    std::vector<uint8_t> bytes = fielddrop::encode(packet);

    ssize_t sent = sendto(sock, bytes.data(), bytes.size(), 0,
                          (const sockaddr*)&sender, sizeof(sender));

    return sent == (ssize_t)bytes.size();
}
bool sameSender(const sockaddr_in& a, const sockaddr_in& b) {
    return a.sin_family == b.sin_family &&
           a.sin_port == b.sin_port &&
           a.sin_addr.s_addr == b.sin_addr.s_addr;
}
}

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        usage(argv[0]);
        return 0;
    }
    if (argc != 3) {
        usage(argv[0]);
        return 2;
    }

    char* end = nullptr;
    long port = std::strtol(argv[2], &end, 10);

    if (argv[2][0] == '\0' || *end != '\0' ||
        port < 1 || port > 65535) {
        std::cerr << "Invalid port number\n";
        return 2;
    }

    std::string outputPath = argv[1];

    std::ifstream existingFile(outputPath, std::ios::binary);

    if (existingFile.good()) {
        std::cerr << "Output file already exists\n";
        return 1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0) {
        std::cerr << "Could not create socket\n";
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)port);

    if (bind(sock, (sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Could not bind socket\n";
        close(sock);
        return 1;
    }

        fielddrop::Packet packet;
    sockaddr_in sender{};
    bool gotHello = false;

    while (!gotHello) {
        uint8_t buffer[1024];
        socklen_t senderLength = sizeof(sender);

        ssize_t received = recvfrom(
            sock, buffer, sizeof(buffer), 0,
            (sockaddr*)&sender, &senderLength
        );

        if (received < 0) {
            std::cerr << "Error receiving packet\n";
            close(sock);
            return 1;
        }

        if (!fielddrop::parse(buffer, (size_t)received, packet)) {
            continue;
        }

        if (packet.type != fielddrop::PacketType::Hello) {
            continue;
        }

         gotHello = true;
    }

    uint32_t sessionId = packet.session_id;
    uint32_t fileSize = fielddrop::helloTotalSize(packet);

    if (fileSize > fielddrop::kMaxFileBytes) {
        std::cerr << "File is too large\n";
        close(sock);
        return 1;
    }

    fielddrop::Packet ack = fielddrop::makeAck(sessionId, 0);

    if (!sendPacket(sock, sender, ack)) {
        std::cerr << "Could not send HELLO ACK\n";
        close(sock);
        return 1;
    }
    std::vector<uint8_t> fileData;
    fileData.reserve(fileSize);

    uint32_t expectedSequence = 1;
    bool finished = false;
    while (!finished) {
        uint8_t buffer[1024];
        sockaddr_in currentSender{};
        socklen_t senderLength = sizeof(currentSender);

        ssize_t received = recvfrom(
            sock, buffer, sizeof(buffer), 0,
            (sockaddr*)&currentSender, &senderLength
        );

        if (received < 0) {
            std::cerr << "Error receiving DATA\n";
            close(sock);
            return 1;
        }

        fielddrop::Packet incoming;

        if (!fielddrop::parse(buffer, (size_t)received, incoming)) {
            continue;
        }

        if (!sameSender(sender, currentSender) ||
            incoming.session_id != sessionId) {
            continue;
        }

        if (incoming.type == fielddrop::PacketType::Hello) {
            sendPacket(sock, sender, fielddrop::makeAck(sessionId, 0));
            continue;
        }

        if (incoming.type == fielddrop::PacketType::Data) {
            if (incoming.sequence == expectedSequence &&
                incoming.payload.size() <= fileSize - fileData.size()) {

                fileData.insert(fileData.end(),
                                incoming.payload.begin(),
                                incoming.payload.end());

                sendPacket(sock, sender,
                           fielddrop::makeAck(sessionId, expectedSequence));

                expectedSequence++;
            }
            else if (incoming.sequence < expectedSequence) {
                sendPacket(sock, sender,
                           fielddrop::makeAck(sessionId, incoming.sequence));
            }
        }
        else if (incoming.type == fielddrop::PacketType::Fin) {
            if (incoming.sequence == expectedSequence &&
                fileData.size() == fileSize) {
                finished = true;
            }
        }
    }
    std::string tempPath = outputPath + ".partXXXXXX";
    std::vector<char> tempName(tempPath.begin(), tempPath.end());
    tempName.push_back('\0');

    int tempFd = mkstemp(tempName.data());

    if (tempFd < 0) {
        std::cerr << "Could not create temporary file\n";
        close(sock);
        return 1;
    }

    tempPath = tempName.data();

    // Keep the temporary file open until we finish writing.

    std::ifstream existing(outputPath, std::ios::binary);
    if (existing.good()) {
        std::cerr << "Output file already exists\n";
        close(sock);
        return 1;
    }

    std::ofstream output(
        "/proc/self/fd/" + std::to_string(tempFd),
        std::ios::binary
    );

    if (!output) {
        std::cerr << "Could not create temporary file\n";
        close(sock);
        return 1;
    }

    if (!fileData.empty()) {
        output.write(reinterpret_cast<const char*>(fileData.data()),
                     fileData.size());
    }

    output.close();

    if (!output) {
        std::cerr << "Could not write file\n";
        std::remove(tempPath.c_str());
        close(sock);
        return 1;
    }
    if (link(tempPath.c_str(), outputPath.c_str()) != 0) {
        std::cerr << "Could not save completed file\n";
        std::remove(tempPath.c_str());
        close(sock);
        return 1;
    }

    std::remove(tempPath.c_str());

    if (!sendPacket(sock, sender,
                    fielddrop::makeFinAck(sessionId, expectedSequence))) {
        std::cerr << "Could not send FIN_ACK\n";
        close(sock);
        return 1;
    }
    auto endTime = std::chrono::steady_clock::now() +
                   std::chrono::seconds(6);

    while (std::chrono::steady_clock::now() < endTime) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - std::chrono::steady_clock::now()
        ).count();

        if (remaining <= 0) {
            break;
        }

        pollfd pfd{};
        pfd.fd = sock;
        pfd.events = POLLIN;

        int ready = poll(&pfd, 1, (int)remaining);

        if (ready == 0) {
            break;
        }

        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        uint8_t buffer[1024];
        sockaddr_in currentSender{};
        socklen_t senderLength = sizeof(currentSender);

        ssize_t received = recvfrom(
            sock, buffer, sizeof(buffer), 0,
            (sockaddr*)&currentSender, &senderLength
        );

        if (received <= 0) {
            continue;
        }

        fielddrop::Packet incoming;

        if (!fielddrop::parse(buffer, (size_t)received, incoming)) {
            continue;
        }

        if (!sameSender(sender, currentSender) ||
            incoming.session_id != sessionId) {
            continue;
        }

        if (incoming.type == fielddrop::PacketType::Fin &&
            incoming.sequence == expectedSequence) {
            sendPacket(sock, sender,
                       fielddrop::makeFinAck(sessionId, expectedSequence));
        }
    }
    close(sock);
    return 0;
}
