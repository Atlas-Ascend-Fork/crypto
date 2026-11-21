/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * Licensed under CANN Open Software License Agreement Version 2.0.
 */
#include "cann_crypto/sm3.h"
#include "acl/acl_rt.h"
#include <stdio.h>

int main(void)
{
    const uint8_t message[] = "abc";
    uint8_t digest[32];
    void *input = NULL, *output = NULL;
    aclrtStream stream = NULL;
    int result = 1;
    if (aclInit(NULL) != ACL_SUCCESS)
        return 1;
    if (aclrtSetDevice(0) != ACL_SUCCESS)
        goto finalize;
    if (aclrtCreateStream(&stream) != ACL_SUCCESS ||
        aclrtMalloc(&input, 3, ACL_MEM_MALLOC_NORMAL_ONLY) != ACL_SUCCESS ||
        aclrtMalloc(&output, sizeof(digest), ACL_MEM_MALLOC_NORMAL_ONLY) != ACL_SUCCESS ||
        aclrtMemcpy(input, 3, message, 3, ACL_MEMCPY_HOST_TO_DEVICE) != ACL_SUCCESS ||
        CannCryptoSm3(input, output, 1, 3, stream) != CANN_CRYPTO_SM3_SUCCESS ||
        aclrtMemcpy(digest, sizeof(digest), output, sizeof(digest), ACL_MEMCPY_DEVICE_TO_HOST) != ACL_SUCCESS)
        goto cleanup;
    for (size_t i = 0; i < sizeof(digest); ++i)
        printf("%02x", (unsigned)digest[i]);
    putchar('\n');
    result = 0;
cleanup:
    if (stream)
        aclrtSynchronizeStream(stream);
    if (output)
        aclrtFree(output);
    if (input)
        aclrtFree(input);
    if (stream)
        aclrtDestroyStream(stream);
    aclrtResetDevice(0);
finalize:
    aclFinalize();
    return result;
}
