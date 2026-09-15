#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

"""AES-128-GCM example: encrypt a 2560-byte message on the NPU and check ciphertext and tag.

Usage: python3 aes128_gcm_example.py [device_id]
"""

import hashlib
import logging
import sys

import torch
import torch_npu  # noqa: F401  registers the NPU device
import ascend_ops  # noqa: F401  registers torch.ops.ascend_ops

from aes_key_expansion import expand_key

# Key, nonce and AAD reuse test case 4 of the GCM specification by McGrew and Viega.
# The plaintext is 2560 bytes. aes_gcm pads the input to a multiple of 512 bytes and splits
# GHASH across 20 cores; its tag is currently correct only when the padded block count is
# divisible by 20, which holds for multiples of 2560 bytes. Expected values were computed
# with both pycryptodome and pyca/cryptography.
KEY = bytes.fromhex("feffe9928665731c6d6a8f9467308308")
NONCE = bytes.fromhex("cafebabefacedbaddecaf888")
AAD = bytes.fromhex("feedfacedeadbeeffeedfacedeadbeefabaddad2")
PLAINTEXT = bytes(i & 0xFF for i in range(2560))
EXPECTED_CIPHERTEXT_SHA256 = "95c2fc16fa3f39d343d411d630f7d025c8fb4bfc624c46243609aa829b69f4a1"
EXPECTED_TAG = bytes.fromhex("779e978e25c735f8dab2eb42e934821d")
TAG_SIZE = 16


def to_npu(data: bytes) -> torch.Tensor:
    return torch.tensor(list(data), dtype=torch.uint8).npu()


def main() -> int:
    device = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    torch.npu.set_device(device)
    output = torch.ops.ascend_ops.aes_gcm(
        to_npu(PLAINTEXT), to_npu(expand_key(KEY)), to_npu(NONCE), to_npu(AAD)).cpu()
    # The output is the ciphertext padded to a multiple of 512 bytes, followed by the 16-byte tag.
    aligned_size = (len(PLAINTEXT) + 511) // 512 * 512
    ciphertext = bytes(output[:len(PLAINTEXT)].tolist())
    tag = bytes(output[aligned_size:aligned_size + TAG_SIZE].tolist())
    ciphertext_sha256 = hashlib.sha256(ciphertext).hexdigest()
    if ciphertext_sha256 != EXPECTED_CIPHERTEXT_SHA256 or tag != EXPECTED_TAG:
        logging.info("AES-128-GCM example: FAIL")
        logging.info("  ciphertext sha256 expected %s", EXPECTED_CIPHERTEXT_SHA256)
        logging.info("  ciphertext sha256 got      %s", ciphertext_sha256)
        logging.info("  tag expected %s", EXPECTED_TAG.hex())
        logging.info("  tag got      %s", tag.hex())
        return 1
    logging.info("AES-128-GCM example: PASS")
    return 0


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    sys.exit(main())
