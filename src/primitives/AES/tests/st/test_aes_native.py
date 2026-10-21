# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

"""Exercise the public C API on an NPU without importing PyTorch."""

import argparse
import ctypes as ct
import logging
from pathlib import Path
import sys

from Crypto.Cipher import AES

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "examples"))
from aes_key_expansion import expand_key


def check(status):
    if status != 0:
        raise RuntimeError(f"ACL operation failed with status {status}")


def configure_acl():
    acl = ct.CDLL("libascendcl.so")
    signatures = {
        "aclInit": [ct.c_char_p],
        "aclFinalize": [],
        "aclrtSetDevice": [ct.c_int32],
        "aclrtResetDevice": [ct.c_int32],
        "aclrtCreateStream": [ct.POINTER(ct.c_void_p)],
        "aclrtDestroyStream": [ct.c_void_p],
        "aclrtMalloc": [ct.POINTER(ct.c_void_p), ct.c_size_t, ct.c_int],
        "aclrtFree": [ct.c_void_p],
        "aclrtMemcpy": [ct.c_void_p, ct.c_size_t, ct.c_void_p, ct.c_size_t, ct.c_int],
        "aclrtMemset": [ct.c_void_p, ct.c_size_t, ct.c_int, ct.c_size_t],
    }
    for name, arguments in signatures.items():
        function = getattr(acl, name)
        function.argtypes = arguments
        function.restype = ct.c_int32
    return acl


def run_case(acl, library, stream, case):
    mode, length, aad_length = case
    plaintext = bytes(index % 251 for index in range(length))
    key = bytes(range(16))
    nonce = bytes(range(12))
    aad = bytes(range(aad_length))
    aligned = (length + 511) // 512 * 512
    output_size = aligned + (16 if mode == "gcm" else 0)
    input_device, output_device = ct.c_void_p(), ct.c_void_p()
    check(acl.aclrtMalloc(ct.byref(input_device), max(length, 1), 0))
    try:
        check(acl.aclrtMalloc(ct.byref(output_device), output_size + 32, 0))
        check(acl.aclrtMemset(output_device, output_size + 32, 0xA5, output_size + 32))
        host_input = ct.create_string_buffer(plaintext)
        if length:
            check(acl.aclrtMemcpy(input_device, length, host_input, length, 1))
        host_key = ct.create_string_buffer(expand_key(key))
        host_nonce = ct.create_string_buffer(nonce)
        if mode == "ctr":
            check(library.CannCryptoAes128Ctr(input_device, length, output_device, output_size,
                                             host_key, host_nonce, stream))
            expected = AES.new(key, AES.MODE_CTR, nonce=nonce, initial_value=1).encrypt(plaintext)
        else:
            host_aad = ct.create_string_buffer(aad)
            check(library.CannCryptoAes128Gcm(input_device, length, output_device, output_size,
                                             host_key, host_nonce, host_aad, len(aad), stream))
            reference = AES.new(key, AES.MODE_GCM, nonce=nonce)
            reference.update(aad)
            ciphertext, tag = reference.encrypt_and_digest(plaintext + bytes(aligned - length))
            expected = ciphertext + tag
        host_output = ct.create_string_buffer(output_size + 32)
        check(acl.aclrtMemcpy(host_output, len(host_output), output_device, len(host_output), 2))
        actual = host_output.raw
        if actual[:len(expected)] != expected:
            raise AssertionError(f"{mode} ciphertext/tag mismatch: length={length}, AAD={aad_length}")
        if actual[output_size:] != bytes([0xA5]) * 32:
            raise AssertionError(f"{mode} wrote beyond the output buffer")
        logging.info("PASS %s bytes=%d aad=%d", mode, length, aad_length)
    finally:
        if output_device.value:
            check(acl.aclrtFree(output_device))
        check(acl.aclrtFree(input_device))


def main():
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", required=True)
    parser.add_argument("--device", type=int, default=0)
    args = parser.parse_args()
    acl = configure_acl()
    library = ct.CDLL(str(Path(args.library).resolve()))
    base = [ct.c_void_p, ct.c_uint64, ct.c_void_p, ct.c_uint64, ct.c_void_p, ct.c_void_p]
    library.CannCryptoAes128Ctr.argtypes = base + [ct.c_void_p]
    library.CannCryptoAes128Gcm.argtypes = base + [ct.c_void_p, ct.c_uint32, ct.c_void_p]
    library.CannCryptoAes128Ctr.restype = library.CannCryptoAes128Gcm.restype = ct.c_int32
    check(acl.aclInit(None))
    stream = ct.c_void_p()
    device_set = False
    try:
        check(acl.aclrtSetDevice(args.device))
        device_set = True
        check(acl.aclrtCreateStream(ct.byref(stream)))
        ctr_lengths = [1, 15, 16, 17, 64, 128, 256, 511, 512, 513, 1023, 1024,
                       1025, 2048, 4096, 8192, 10000, 20000, 50000, 100000]
        for length in ctr_lengths:
            run_case(acl, library, stream, ("ctr", length, 0))
        for length in [0, 2560, 20480, 40960, 61440, 737280]:
            for aad_length in [0, 16, 32]:
                run_case(acl, library, stream, ("gcm", length, aad_length))
        logging.info("AES_NATIVE_ST_RESULT=PASS cases=38")
    finally:
        if stream.value:
            check(acl.aclrtDestroyStream(stream))
        if device_set:
            check(acl.aclrtResetDevice(args.device))
        check(acl.aclFinalize())


if __name__ == "__main__":
    main()
