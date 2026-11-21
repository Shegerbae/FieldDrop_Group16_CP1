# Contributions

List each group member and their specific contributions to design, C++ implementation, tests, analysis, and report preparation.

## Shalom (Group 16)
- Packet layer (Part 1): `source/fielddrop_packet.hpp` with the Packet struct, `encode()`, `parse()`, CRC-32 using zlib `crc32`, and validation of magic, version, type, reserved field, session ID, payload length, datagram size, and per-type sequence/length rules.
- ABC packet-vector check: `tests/vector_check.cpp` compares the encoder's bytes with the five published packets and tests 15 malformed-packet cases. Output saved in `evidence/packet_vector_check.txt`.
- Makefile: added a `vector_check` target and made `make clean` remove it.

## Patrick (Group 16)
- Sender implementation: Worked on `source/sender.cpp` to send files using IPv4 UDP. Added file size and port validation, reading files in binary mode, and sending HELLO, DATA, and FIN packets.
- Stop-and-wait protocol: Implemented waiting for the correct ACK before sending the next packet, with a 500 ms timeout and up to 10 transmission attempts. Added checks for the sender's session ID, packet sequence number, and receiver address.
- Testing: Tested my sender against the instructor's reference receiver. Passed Checkpoint 1 test cases 1, 2, 3, 4, 15, and 16, including empty files, text files, binary files, delayed packets, oversized file rejection, and existing output protection.
- Evidence: Saved the test results in `evidence/sender_case_1.txt`, `sender_case_2.txt`, `sender_case_3.txt`, `sender_case_4.txt`, `sender_case_15.txt`, and `sender_case_16.txt`.

## Ruth Tibebe (Group 16)
- Receiver implementation: Worked on `source/receiver.cpp` to receive files over IPv4 UDP. Implemented socket creation, port validation, binding to the listening port, and receiving HELLO, DATA, and FIN packets.
- Stop-and-wait protocol: Implemented ACK responses, sequence number tracking, session ID verification, duplicate packet handling, and FIN_ACK responses.
- File handling: Added file size validation, protection against overwriting existing output files, temporary file handling, and saving the completed transfer.
- Testing: Tested the receiver against the instructor's reference sender. Passed Checkpoint 1 test cases 1, 2, 3, 4, 15, and 16.
- Evidence: Saved receiver test results in `evidence/receiver_case_1.txt`, `receiver_case_2.txt`, `receiver_case_3.txt`, `receiver_case_4.txt`, `receiver_case_15.txt`, and `receiver_case_16.txt`. 
