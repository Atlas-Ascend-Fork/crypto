#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

import logging

import torch
import torch_npu
import ascend_ops
import pytest


from tests.aes_common import _prepare_key_tensor


def _aes128_ctr_reference(plaintext_bytes: bytes, key16: bytes, nonce12: bytes) -> bytes:
    """
    CPU reference implementation using pycryptodome.

    Parameters:
        plaintext_bytes: Plaintext bytes
        key16: 16-byte AES key
        nonce12: 12-byte nonce
    """
    try:
        from Crypto.Cipher import AES
    except ImportError:
        pytest.skip("pycryptodome not installed")
    cipher = AES.new(key16, AES.MODE_CTR, nonce=nonce12, initial_value=1)
    return cipher.encrypt(plaintext_bytes)


def test_aes128ctr_meta_rejects_length_past_kernel_limit():
    """Do not wrap a valid int64 tensor length into the uint32 kernel field."""
    maximum = (1 << 32) - 512
    key = torch.empty((176,), dtype=torch.uint8, device="meta")
    nonce = torch.empty((12,), dtype=torch.uint8, device="meta")
    maximum_input = torch.empty((maximum,), dtype=torch.uint8, device="meta")
    assert torch.ops.ascend_ops.aes128ctr(maximum_input, key, nonce).numel() == maximum

    too_large = torch.empty((maximum + 256,), dtype=torch.uint8, device="meta")
    with pytest.raises(RuntimeError, match="maximum representable"):
        torch.ops.ascend_ops.aes128ctr(too_large, key, nonce)


def test_aes128ctr_interface_exist():
    """Check whether the AES-128-CTR custom operator is registered."""
    logging.debug("Operator binding: %s", torch.ops.ascend_ops.aes128ctr)
    assert hasattr(torch.ops.ascend_ops, "aes128ctr"), (
        "The 'aes128ctr' operator is not registered in the 'torch.ops.ascend_ops' namespace."
    )


PLAINTEXT_LENGTHS = [
    1,
    15,
    16,
    17,
    64,
    128,
    256,
    511,
    512,
    513,
    1023,
    1024,
    1025,
    2048,
    4096,
    8192,
    10000,
    20000,
    50000,
    100000,
]


@pytest.mark.skipif(not torch.npu.is_available(), reason="NPU device not found")
@pytest.mark.parametrize("length", PLAINTEXT_LENGTHS)
def test_aes128ctr_operator(length):
    """
    Test the functionality of the AES-128-CTR operator against a CPU reference
    implementation (pycryptodome). Covers various plaintext lengths including
    512-aligned and non-aligned sizes to verify padding correctness.

    Parameters:
        length: Plaintext length in bytes
    """
    plaintext = torch.randint(0, 256, (length,), dtype=torch.uint8)
    key16 = torch.randint(0, 256, (16,), dtype=torch.uint8)
    nonce = torch.randint(0, 256, (12,), dtype=torch.uint8)

    pt_bytes = plaintext.numpy().tobytes()
    ref_bytes = _aes128_ctr_reference(pt_bytes, key16.numpy().tobytes(), nonce.numpy().tobytes())
    expected = torch.frombuffer(ref_bytes, dtype=torch.uint8)

    key_tensor = _prepare_key_tensor(key16.numpy())
    plaintext_npu = plaintext.npu()
    key_npu = key_tensor.npu()
    nonce_npu = nonce.npu()
    result_npu = torch.ops.ascend_ops.aes128ctr(plaintext_npu, key_npu, nonce_npu)
    result = result_npu.cpu()

    result_valid = result.flatten()[:length]

    assert torch.equal(result_valid, expected), (
        f"AES-128-CTR failed for length={length}. "
        f"Expected first 16 bytes: {expected[:16].tolist()}, "
        f"but got: {result_valid[:16].tolist()}"
    )

    logging.info("Test passed: length=%s", length)


@pytest.mark.skipif(not torch.npu.is_available(), reason="NPU device not found")
def test_aes128ctr_noncontiguous_512_byte_view():
    """A valid ND view with a two-byte stride is encrypted in logical element order."""
    key16 = torch.tensor(
        [0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
         0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c],
        dtype=torch.uint8,
    )
    nonce = torch.tensor(
        [0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb],
        dtype=torch.uint8,
    )
    storage = torch.arange(1024, dtype=torch.int64).remainder(251).to(torch.uint8)
    plaintext = storage[::2]
    reference = torch.tensor(
        list(_aes128_ctr_reference(plaintext.numpy().tobytes(), key16.numpy().tobytes(), nonce.numpy().tobytes())),
        dtype=torch.uint8,
    )

    key_npu = _prepare_key_tensor(key16.numpy()).npu()
    nonce_npu = nonce.npu()
    plaintext_storage_npu = storage.npu()
    plaintext_npu = plaintext_storage_npu[::2]
    assert plaintext_npu.numel() == 512
    assert plaintext_npu.stride() == (2,)
    assert not plaintext_npu.is_contiguous()

    result = torch.ops.ascend_ops.aes128ctr(plaintext_npu, key_npu, nonce_npu).cpu()
    control = torch.ops.ascend_ops.aes128ctr(plaintext_npu.contiguous(), key_npu, nonce_npu).cpu()

    assert torch.equal(result[:512], reference)
    assert torch.equal(control[:512], reference)


@pytest.mark.skipif(not torch.npu.is_available(), reason="NPU device not found")
@pytest.mark.parametrize("strided_argument", ["key", "nonce"])
def test_aes128ctr_noncontiguous_key_or_nonce(strided_argument):
    """Key and nonce views are read in logical byte order by the host entry point."""
    plaintext = torch.arange(512, dtype=torch.int64).remainder(251).to(torch.uint8)
    key16 = torch.tensor(
        [0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
         0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c],
        dtype=torch.uint8,
    )
    nonce = torch.tensor(
        [0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb],
        dtype=torch.uint8,
    )
    expanded_key = _prepare_key_tensor(key16.numpy())
    plaintext_npu = plaintext.npu()
    key_control_npu = expanded_key.npu()
    nonce_control_npu = nonce.npu()

    if strided_argument == "key":
        key_storage = torch.full((expanded_key.numel() * 2,), 0xa5, dtype=torch.uint8)
        key_storage[::2].copy_(expanded_key)
        expanded_key = key_storage[::2]
    else:
        nonce_storage = torch.full((nonce.numel() * 2,), 0x5a, dtype=torch.uint8)
        nonce_storage[::2].copy_(nonce)
        nonce = nonce_storage[::2]

    key_storage_npu = key_storage.npu() if strided_argument == "key" else None
    nonce_storage_npu = nonce_storage.npu() if strided_argument == "nonce" else None
    key_npu = key_storage_npu[::2] if key_storage_npu is not None else key_control_npu
    nonce_npu = nonce_storage_npu[::2] if nonce_storage_npu is not None else nonce_control_npu
    assert (key_npu if strided_argument == "key" else nonce_npu).stride() == (2,)

    result = torch.ops.ascend_ops.aes128ctr(plaintext_npu, key_npu, nonce_npu).cpu()
    control = torch.ops.ascend_ops.aes128ctr(plaintext_npu, key_control_npu, nonce_control_npu).cpu()

    assert torch.equal(result[:512], control[:512])
