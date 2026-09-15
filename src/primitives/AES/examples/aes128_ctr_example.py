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

"""AES-128-CTR example: encrypt RFC 3686 test vector #2 on the NPU and check the result.

Usage: python3 aes128_ctr_example.py [device_id]
"""

import logging
import sys

import torch
import torch_npu  # noqa: F401  registers the NPU device
import ascend_ops  # noqa: F401  registers torch.ops.ascend_ops

from aes_key_expansion import expand_key

# RFC 3686 test vector #2. Its counter block is the 12-byte nonce followed by a 32-bit
# big-endian counter starting at 1, which is the layout aes128ctr uses.
KEY = bytes.fromhex("7e24067817fae0d743d6ce1f32539163")
NONCE = bytes.fromhex("006cb6dbc0543b59da48d90b")
PLAINTEXT = bytes(range(32))
EXPECTED_CIPHERTEXT = bytes.fromhex("5104a106168a72d9790d41ee8edad388eb2e1efc46da57c8fce630df9141be28")


def to_npu(data: bytes) -> torch.Tensor:
    return torch.tensor(list(data), dtype=torch.uint8).npu()


def main() -> int:
    device = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    torch.npu.set_device(device)
    output = torch.ops.ascend_ops.aes128ctr(to_npu(PLAINTEXT), to_npu(expand_key(KEY)), to_npu(NONCE))
    # The output is padded to a multiple of 512 bytes; the first len(PLAINTEXT) bytes are the ciphertext.
    ciphertext = bytes(output.cpu()[:len(PLAINTEXT)].tolist())
    if ciphertext != EXPECTED_CIPHERTEXT:
        logging.info("AES-128-CTR example: FAIL")
        logging.info("  expected %s", EXPECTED_CIPHERTEXT.hex())
        logging.info("  got      %s", ciphertext.hex())
        return 1
    logging.info("AES-128-CTR example: PASS")
    return 0


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    sys.exit(main())
