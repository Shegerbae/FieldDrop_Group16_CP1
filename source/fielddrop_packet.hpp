#ifndef FIELDDROP_PACKET_HPP
#define FIELDDROP_PACKET_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

#include <zlib.h>

// Wire-format constants from the assignment. Multibyte values on the wire
// use network byte order. Implement encoding and validation in your own files.
namespace fielddrop {

constexpr std::uint16_t kMagic = 0x4644;
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kHeaderBytes = 20;
constexpr std::size_t kMaxPayloadBytes = 512;
constexpr std::size_t kMaxFileBytes = 262144;
constexpr int kTimeoutMs = 500;
constexpr int kMaxTransmissions = 10;
constexpr int kReceiverIdleMs = 15000;
constexpr int kFinLingerMs = 6000;

enum class PacketType : std::uint8_t {
    Hello = 1,
    Data = 2,
    Ack = 3,
    Fin = 4,
    FinAck = 5,
};

// Packet layer (Part 1). Header-only on purpose: the starter Makefile builds
// each program from one .cpp plus this header, so inline functions need no
// Makefile changes.
//
// Wire header (20 bytes, big-endian):
//   0 magic(2) | 2 version(1) | 3 type(1) | 4 session(4) | 8 sequence(4)
//   12 length(2) | 14 reserved(2) | 16 crc32(4) | 20 payload(0-512)

// Host-order representation. Never send this struct directly.
struct Packet {
    PacketType type = PacketType::Ack;
    std::uint32_t session_id = 0;
    std::uint32_t sequence = 0;
    std::vector<std::uint8_t> payload;
};

namespace detail {

inline void put16(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 8);
    p[1] = static_cast<std::uint8_t>(v);
}
inline void put32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}
inline std::uint16_t get16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}
inline std::uint32_t get32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           static_cast<std::uint32_t>(p[3]);
}

// CRC-32 over header bytes 0-15 followed by the payload (CRC field excluded).
inline std::uint32_t packetCrc(const std::uint8_t* header16,
                               const std::uint8_t* payload,
                               std::size_t payload_len) {
    uLong crc = crc32(0L, header16, 16);
    if (payload_len > 0) {
        crc = crc32(crc, payload, static_cast<uInt>(payload_len));
    }
    return static_cast<std::uint32_t>(crc);
}

// Type-specific length and sequence rules (not state-dependent ones).
inline bool fieldsValidForType(PacketType type, std::uint32_t sequence,
                               std::size_t length) {
    switch (type) {
        case PacketType::Hello:  return length == 4 && sequence == 0;
        case PacketType::Data:   return length >= 1 && length <= kMaxPayloadBytes &&
                                        sequence >= 1;
        case PacketType::Ack:    return length == 0;  // ACK 0 acknowledges HELLO
        case PacketType::Fin:    return length == 0 && sequence >= 1;
        case PacketType::FinAck: return length == 0 && sequence >= 1;
    }
    return false;
}

} // namespace detail

// Serialize a packet to its wire bytes. Returns an empty vector if the packet
// itself is invalid (zero session ID, or wrong length/sequence for its type),
// so a caller bug can never put a malformed datagram on the wire.
inline std::vector<std::uint8_t> encode(const Packet& pkt) {
    const std::size_t n = pkt.payload.size();
    if (pkt.session_id == 0 ||
        !detail::fieldsValidForType(pkt.type, pkt.sequence, n)) {
        return {};
    }
    std::vector<std::uint8_t> out(kHeaderBytes + n);
    detail::put16(&out[0], kMagic);
    out[2] = kVersion;
    out[3] = static_cast<std::uint8_t>(pkt.type);
    detail::put32(&out[4], pkt.session_id);
    detail::put32(&out[8], pkt.sequence);
    detail::put16(&out[12], static_cast<std::uint16_t>(n));
    detail::put16(&out[14], 0);  // reserved
    for (std::size_t i = 0; i < n; ++i) out[kHeaderBytes + i] = pkt.payload[i];
    detail::put32(&out[16],
                  detail::packetCrc(out.data(), out.data() + kHeaderBytes, n));
    return out;
}

// Parse one complete UDP datagram. Returns true and fills `out` only if every
// wire-level check passes; otherwise returns false and `out` is untouched.
// `len` must be the number of bytes recvfrom() returned, and the caller's
// buffer must be larger than the biggest valid packet (kHeaderBytes +
// kMaxPayloadBytes) so oversized datagrams are not silently truncated.
inline bool parse(const std::uint8_t* buf, std::size_t len, Packet& out) {
    if (buf == nullptr || len < kHeaderBytes) return false;
    if (detail::get16(buf) != kMagic) return false;
    if (buf[2] != kVersion) return false;
    const std::uint8_t raw_type = buf[3];
    if (raw_type < 1 || raw_type > 5) return false;
    const std::uint32_t session = detail::get32(buf + 4);
    const std::uint32_t sequence = detail::get32(buf + 8);
    const std::size_t length = detail::get16(buf + 12);
    if (detail::get16(buf + 14) != 0) return false;  // reserved must be zero
    if (session == 0) return false;
    if (length > kMaxPayloadBytes) return false;
    if (len != kHeaderBytes + length) return false;  // exact datagram size
    const PacketType type = static_cast<PacketType>(raw_type);
    if (!detail::fieldsValidForType(type, sequence, length)) return false;
    if (detail::get32(buf + 16) !=
        detail::packetCrc(buf, buf + kHeaderBytes, length)) {
        return false;
    }
    out.type = type;
    out.session_id = session;
    out.sequence = sequence;
    out.payload.assign(buf + kHeaderBytes, buf + kHeaderBytes + length);
    return true;
}

// Convenience builders so sender/receiver never hand-fill a Packet.
inline Packet makeHello(std::uint32_t session, std::uint32_t total_size) {
    Packet p;
    p.type = PacketType::Hello;
    p.session_id = session;
    p.sequence = 0;
    p.payload.resize(4);
    detail::put32(p.payload.data(), total_size);
    return p;
}
inline Packet makeData(std::uint32_t session, std::uint32_t seq,
                       const std::uint8_t* bytes, std::size_t n) {
    Packet p;
    p.type = PacketType::Data;
    p.session_id = session;
    p.sequence = seq;
    p.payload.assign(bytes, bytes + n);
    return p;
}
inline Packet makeAck(std::uint32_t session, std::uint32_t seq) {
    Packet p;
    p.type = PacketType::Ack;
    p.session_id = session;
    p.sequence = seq;
    return p;
}
inline Packet makeFin(std::uint32_t session, std::uint32_t seq) {
    Packet p;
    p.type = PacketType::Fin;
    p.session_id = session;
    p.sequence = seq;
    return p;
}
inline Packet makeFinAck(std::uint32_t session, std::uint32_t seq) {
    Packet p;
    p.type = PacketType::FinAck;
    p.session_id = session;
    p.sequence = seq;
    return p;
}

// Total file size advertised by a (parsed, valid) HELLO packet.
inline std::uint32_t helloTotalSize(const Packet& hello) {
    return hello.payload.size() == 4 ? detail::get32(hello.payload.data()) : 0;
}

} // namespace fielddrop

#endif
