/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 *
 * This program is free software, you can redistribute it and/or modify it
 * under the terms and conditions of CANN Open Software License Agreement
 * Version 2.0 (the "License").
 *
 * Please refer to the License for details. You may not use this file except
 * in compliance with the License.
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See LICENSE in the root of the software repository for the full text
 * of the License.
 */

/*!
 * \file extension.cpp
 * \brief Python extension entry for loading registered Ascend operators.
 *
 * This implementation is adapted from the CANN fast kernel launch example
 * and remains subject to the CANN Open Software License Agreement Version 2.0.
 */

#include <Python.h>

namespace {

PyModuleDef g_module_def = {
    PyModuleDef_HEAD_INIT, "_C", nullptr, -1, nullptr, nullptr, nullptr, nullptr, nullptr,
};

} // namespace

PyMODINIT_FUNC PyInit__C() { return PyModule_Create(&g_module_def); }
