# FieldDrop – Group 16, Checkpoint 1 

## Overview
FieldDrop transfers a file over IPv4 UDP using a sender and receiver. It uses Stop-and-Wait ARQ, sequence numbers, acknowledgments, and CRC-32 packet validation. The receiver collects file bytes in a temporary file and creates the requested output file after a valid FIN.

Checkpoint 1 covers packet encoding/validation, normal transfers, and file guards. The later fault tests, Kathara run, field trial, and final report are not part of this checkpoint submission.
## Requirements
Build and run the C++ programs on Linux with:
- g++ supporting C++17
- make
- zlib development files (the build links with -lz)
The separate FieldDrop course tools are needed for the supplied reference-peer tests; their Linux executables are not included in this submission.
## Build
From the extracted FieldDrop_Group16_CP1/ folder:
make clean && make
This creates fielddrop-send and fielddrop-recv. Use make clean to remove generated build files.
## Run a Transfer on Localhost
Open two terminals in the extracted project folder. In Terminal 1, start the receiver:
./fielddrop-recv received.bin 43000
In Terminal 2, send the included ABC example:
./fielddrop-send tests/abc.txt 127.0.0.1 43000
Wait until both programs finish. On success they return exit status 0, and received.bin must contain exactly ABC.

Check the result using:
cmp tests/abc.txt received.bin
sha256sum tests/abc.txt received.bin
Important: The receiver refuses to overwrite an existing output file. For another run, choose a new output filename. After successful FIN, the receiver normally remains available for about six seconds to answer a repeated FIN before exiting.
## Command Lines and Limits
./fielddrop-send INPUT_FILE RECEIVER_HOST RECEIVER_PORT
./fielddrop-recv OUTPUT_FILE LISTEN_PORT
- Port arguments must be decimal numbers from 1 through 65535.
- File sizes from 0 through 262,144 bytes are supported, including binary files.
- DATA packets carry up to 512 bytes at a time.
- The sender waits for the matching ACK before sending another DATA packet.
- Both programs use IPv4 UDP and reject invalid inputs with nonzero exit status.
## Packet-Vector Check
The packet encoder is compared against the published ABC examples in tests/packet-vectors.txt.
make vector_check
./vector_check
The recorded comparison is in evidence/packet_vector_check.txt.
## Checkpoint 1 Course Tests
The provided Linux course-tool folder must be next to the original, unchanged FieldDrop_Student_Starter/ folder, because run_case.sh obtains its fixtures there. The submitted group folder can be beside them:
FieldDrop_Course_Tools/
FieldDrop_Student_Starter/
FieldDrop_Group16_CP1/
From FieldDrop_Group16_CP1/, after building:
tools=$(realpath ../FieldDrop_Course_Tools)
for n in 1 2 3 4 15 16; do
  "$tools/scripts/run_case.sh" "$n" "$PWD/fielddrop-send" "$PWD/fielddrop-recv"
done
Test interoperability with the reference peers:
"$tools/scripts/run_case.sh" 2 "$PWD/fielddrop-send" "$tools/fielddrop-recv"
"$tools/scripts/run_case.sh" 2 "$tools/fielddrop-send" "$PWD/fielddrop-recv"
The expected results are successful transfers for cases 1-4 and correctly rejected transfers for cases 15-16. The course script prints PASS/FAIL and a temporary results-folder path. Preserve the relevant sender, receiver, and relay logs, and compare output bytes or SHA-256 values.
## Evidence and Documentation
- PROGRESS.md: Checkpoint 1 implementation status, tests, remaining work, and member responsibilities.
- CONTRIBUTIONS.md: Actual work completed by group members.
- ACKNOWLEDGMENTS.md: Disclosed outside and AI assistance.
- tests/: Test fixtures and the packet-vector checker.
- evidence/: Packet comparison and checkpoint test results. The full course-script logs and comparisons must be included for a complete evidence record.
The final Checkpoint 1 ZIP is named FieldDrop_Group16_CP1.zip and contains one top-level folder named FieldDrop_Group16_CP1/.