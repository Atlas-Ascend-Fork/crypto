#ifndef CANN_CRYPTO_TEST_PLATFORM_ASCENDC_H
#define CANN_CRYPTO_TEST_PLATFORM_ASCENDC_H

#include "register/op_impl_registry.h"

namespace platform_ascendc {
class PlatformAscendC {
public:
    explicit PlatformAscendC(gert::TestPlatform* platform) : platform_(platform) {}
    uint32_t GetCoreNumAiv() const { return platform_->coreCount; }
private:
    gert::TestPlatform* platform_;
};
}
#endif
