# AES Invocation Examples

English | [简体中文](README.md)

Both examples encrypt known-answer inputs on the NPU and verify the results. On success, they print PASS and return 0; on failure, they print the expected and actual values and return 1.

| Example | Operator | Known-Answer Source |
| --- | --- | --- |
| `aes128_ctr_example.py` | `torch.ops.ascend_ops.aes128ctr` | RFC 3686 test vector #2 |
| `aes128_gcm_example.py` | `torch.ops.ascend_ops.aes_gcm` | Key, nonce, and AAD from GCM specification test case 4, with a 2560-byte plaintext; expected results computed independently with pycryptodome and pyca/cryptography and verified to match |

Both examples use [`aes_key_expansion.py`](../../src/primitives/AES/examples/aes_key_expansion.py) to expand a 16-byte key into the operator's round-key format according to FIPS 197.

## Run

Build and install `ascend_ops` as described in the [component README](../README_en.md), load the CANN environment, and run the following commands from this directory:

```bash
python3 aes128_ctr_example.py      # Use device 0 by default
python3 aes128_gcm_example.py 1    # Specify the device ID
```

## Usage Notes

- The key parameter contains the 176-byte expanded key for 11 rounds, rather than the original 16-byte key.
- The nonce is 12 bytes. The CTR counter starts at 1, and the GCM encryption counter starts at 2, as specified by the standards.
- Output length is padded to a multiple of 512 bytes. The first `len(plaintext)` bytes of the CTR output are the ciphertext; GCM appends a 16-byte tag after the padded ciphertext.
- In the current implementation, GCM tags are correct only when the plaintext length is a multiple of 2560 bytes: after padding to a multiple of 512 bytes, the input is split across 20 cores for GHASH. If the block count is not divisible by 20, the merged tag is incorrect; the ciphertext is unaffected.
- GCM supports up to 32 bytes of AAD.
