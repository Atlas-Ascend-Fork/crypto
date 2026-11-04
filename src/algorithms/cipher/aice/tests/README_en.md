# AICE Tests

English | [简体中文](README.md)

See [build, test, and benchmark](../docs/build_en.md) for the commands.

## Host Tests

| Test | Coverage |
| --- | --- |
| aice_host | C ABI parameter checks, encrypt/decrypt direction, batch/dim forwarding, single launch, stream synchronization, and error returns; covers zero length, capacity, null pointers, address overflow, and buffer overlap |
| aice_cpu | CPU encrypt/decrypt round trips, in-place consistency, parameter checks, IV96 regression vectors, and the high 16 bits of the lane index |
| aice_cpu_kat | Byte-for-byte comparison of CPU output against fixed KAT answers |
| aice_host_only_build | Host configuration and compilation without the CANN SDK |
| aice_shared_launch | Links the production shared library; checks exported symbols, zero-length synchronization, parameter errors, and synchronization error returns |

`test_public_header.c` verifies the C compatibility of the public header at build time.
`aice_host` uses stand-ins for ACL and launch functions; `aice_shared_launch` uses the shared library plus synchronization stand-ins and is produced by the device build.

## Device Tests

| Test | Coverage |
| --- | --- |
| aice_device | NPU encrypt/decrypt round trips, plus in-place and out-of-place consistency |
| aice_cross | Byte-for-byte comparison of NPU and CPU ciphertexts for the same input; NPU decryption of CPU ciphertext is compared against the original plaintext |
| aice_tail_regression | Short inputs, tile boundaries, odd byte tails, and multi-tile in-place processing against the CPU reference |
| aice_device_kat | NPU encryption and decryption through the public C ABI, compared byte-for-byte with fixed KAT answers |

Functional and cross tests cover batch=128/256/512, single and multiple blocks, dim values matching the device AIV core count, and both in-place and out-of-place processing.
Data lengths cover short inputs, tile boundaries, and several consecutive tiles; partitioning covers exact and inexact division, idle blocks, and multiple consecutive tiles per block.
Lane indices cross the 16-bit sign boundary and the 65536 boundary.

## CPU Reference Implementation

`assets/aice_cpu.c` and `assets/aice_api_cpu.h` are the CPU implementation of the AICE submission, supporting x86 SSE2/AVX2 and ARM NEON.

`aice_cpu_batch.c/.h` provides test entry points for batch=128/256/512, using the same consecutive-tile partitioning, global lane indices, and tail padding rules as the NPU.
The CPU reference is also used to verify cross-test and benchmark results.

## Known-Answer Tests

`assets/aice_kat.rsp` comes from the AICE submission and contains 7 KEYSTREAM and 22 ENCRYPT answer sets.
Each set specifies a key, IV, lane, and expected result, and exercises encryption and decryption for all three batch sizes.

KATs are single-lane data. The test places word w of the answer at position `w * batch + lane` in the SoA buffer, zero-fills the other lanes, and compares the output of the selected lane.
KEYSTREAM sets check the keystream with an all-zero plaintext; ENCRYPT sets check both directions using the PT and CT from the file.
CPU KAT and device KAT read the same fixed answers.

## Smoke and Tail Coverage

The smoke mode of `test_aice_device` covers 17-byte inputs for every batch size and multi-tile in-place processing; the tail-regression mode adds short inputs and tile boundary combinations.
The smoke mode of `test_aice_device_kat` runs the first KAT set.
