// Copyright (C) 2023-2024 Arm Technology (China) Co. Ltd.
//
// SPDX-License-Identifier: Apache-2.0


#ifndef __AIPU_H__
#define __AIPU_H__

#include <stdint.h>
#include <memory>

#define TSM_CMD_SCHED_CTRL       0x0
#define TSM_CMD_SCHED_ADDR_HI    0x8
#define TSM_CMD_SCHED_ADDR_LO    0xC
#define TSM_CMD_TCB_NUMBER       0x1C

#define TSM_STATUS               0x18
#define TSM_STATUS_CMDPOOL_FULL_QOSL(val) (val & 0xff)
#define TSM_STATUS_CMDPOOL_FULL_QOSH(val) ((val >> 8) & 0xff)

#define CREATE_CMD_POOL          0x1
#define DESTROY_CMD_POOL         0x2
#define DISPATCH_CMD_POOL        0x4
#define CMD_POOL0_STATUS         0x804
#define CLUSTER0_CONFIG          0xC00
#define CLUSTER0_CTRL            0xC04
#define CMD_POOL0_IDLE           (1 << 6)

namespace sim_aipu
{
    class IMemEngine;
    class IDbgLite;

    class Aipu
    {
    public:
        Aipu(const struct config_t &, IMemEngine &);
        ~Aipu();

        Aipu(const Aipu &) = delete;
        Aipu &operator=(const Aipu &) = delete;

        int read_register(uint32_t addr, uint32_t &v) const;

        int write_register(uint32_t addr, uint32_t v);

        static int version();

        void set_dbg_lite(const std::shared_ptr<IDbgLite> &);

        void enable_profiling(bool en);

        void dump_profiling();

        void set_event_handler(void (*)(uint32_t event, uint64_t value, void *context),
                               void *context);
    private:
        std::unique_ptr<class AipuImpl> impl_;
    };
} //!sim_aipu

#ifdef __cplusplus
extern "C"
{
    typedef sim_aipu::Aipu* (*sim_convert_t) (void*);
    sim_aipu::Aipu* sim_convert(void*);
}
#endif //!__cplusplus

#endif //!__AIPU_H__
