// ABC packet-vector check for the FieldDrop packet layer.
// Encodes the five published ABC packets with our own encode(), prints our
// bytes next to the published bytes, and compares them. Also runs parse()
// on the published bytes and on deliberately broken copies.
// Build (from the starter folder):
//   g++ -std=c++17 -Wall -Wextra -Wpedantic -Isource -o vector_check tests/vector_check.cpp -lz
#include "fielddrop_packet.hpp"

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

using namespace fielddrop;
using Bytes = std::vector<std::uint8_t>;

static Bytes fromHex(const std::string& s) {
    Bytes out;
    std::istringstream in(s);
    std::string tok;
    while (in >> tok) out.push_back(static_cast<std::uint8_t>(std::stoul(tok, nullptr, 16)));
    return out;
}
static std::string toHex(const Bytes& b) {
    std::string s;
    char tmp[4];
    for (std::size_t i = 0; i < b.size(); ++i) {
        std::snprintf(tmp, sizeof tmp, "%02x", b[i]);
        if (i) s += ' ';
        s += tmp;
    }
    return s;
}

static int failures = 0;
static void check(bool ok, const std::string& name) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name.c_str());
    if (!ok) ++failures;
}

int main() {
    const std::uint32_t sid = 0x01020304;
    const std::uint8_t abc[3] = {'A', 'B', 'C'};

    struct Case { const char* name; Packet pkt; const char* published; };
    const Case cases[] = {
        {"HELLO (seq 0, total size 3)", makeHello(sid, 3),
         "46 44 01 01 01 02 03 04 00 00 00 00 00 04 00 00 a5 ea 20 c2 00 00 00 03"},
        {"DATA (seq 1, payload ABC)", makeData(sid, 1, abc, 3),
         "46 44 01 02 01 02 03 04 00 00 00 01 00 03 00 00 4b 5a 87 8c 41 42 43"},
        {"ACK (seq 1)", makeAck(sid, 1),
         "46 44 01 03 01 02 03 04 00 00 00 01 00 00 00 00 b9 64 71 fc"},
        {"FIN (seq 2)", makeFin(sid, 2),
         "46 44 01 04 01 02 03 04 00 00 00 02 00 00 00 00 83 b7 0d 74"},
        {"FIN_ACK (seq 2)", makeFinAck(sid, 2),
         "46 44 01 05 01 02 03 04 00 00 00 02 00 00 00 00 5e 21 d4 f1"},
    };

    std::printf("== Encoder vs published ABC vectors ==\n");
    for (const Case& c : cases) {
        const Bytes pub = fromHex(c.published);
        const Bytes ours = encode(c.pkt);
        std::printf("%s\n  published: %s\n  produced : %s\n", c.name,
                    toHex(pub).c_str(), toHex(ours).c_str());
        check(ours == pub, "bytes (including CRC) match");
    }

    std::printf("== Parser accepts the published packets ==\n");
    for (const Case& c : cases) {
        const Bytes pub = fromHex(c.published);
        Packet p;
        const bool ok = parse(pub.data(), pub.size(), p);
        check(ok && p.type == c.pkt.type && p.session_id == sid &&
                  p.sequence == c.pkt.sequence && p.payload == c.pkt.payload,
              std::string("parses and fields round-trip: ") + c.name);
    }

    std::printf("== Parser rejects malformed datagrams ==\n");
    const Bytes data = fromHex(cases[1].published);
    auto rejects = [&](Bytes b, const char* name) {
        Packet p;
        check(!parse(b.data(), b.size(), p), name);
    };
    { Bytes b = data; b[0] ^= 0xff;  rejects(b, "wrong magic"); }
    { Bytes b = data; b[2] = 2;      rejects(b, "wrong version"); }
    { Bytes b = data; b[3] = 9;      rejects(b, "unknown type"); }
    { Bytes b = data; b[3] = 0;      rejects(b, "type 0"); }
    { Bytes b = data; b[4]=b[5]=b[6]=b[7]=0; rejects(b, "session ID zero"); }
    { Bytes b = data; b[15] = 1;     rejects(b, "reserved nonzero"); }
    { Bytes b = data; b[13] = 4;     rejects(b, "length field larger than datagram"); }
    { Bytes b = data; b.pop_back();  rejects(b, "truncated datagram"); }
    { Bytes b = data; b.push_back(0);rejects(b, "extra trailing byte"); }
    { Bytes b = data; b[22] ^= 0x01; rejects(b, "payload bit flip (bad CRC)"); }
    { Bytes b = data; b[16] ^= 0x01; rejects(b, "CRC field bit flip"); }
    { Bytes b(10, 0);                rejects(b, "shorter than header"); }
    { Bytes b = data; b[13] = 0;     rejects(b, "DATA with length 0"); }
    { Bytes b = fromHex(cases[2].published); b[13] = 1; b.push_back(0);
                                     rejects(b, "ACK with payload"); }
    { Bytes b = fromHex(cases[0].published); b[11] = 1;
                                     rejects(b, "HELLO with sequence != 0"); }
    {
        // Maximum-size DATA must round-trip; 513-byte payload must not encode.
        Bytes big(512, 0xAB);
        Bytes wire = encode(makeData(sid, 7, big.data(), big.size()));
        Packet p;
        check(wire.size() == 532 && parse(wire.data(), wire.size(), p) &&
                  p.payload == big, "512-byte DATA round-trips");
        Bytes too_big(513, 0);
        check(encode(makeData(sid, 7, too_big.data(), too_big.size())).empty(),
              "513-byte DATA refused by encode()");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "RESULT: FAIL" : "RESULT: ALL PASS",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
