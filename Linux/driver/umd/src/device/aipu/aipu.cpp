// Copyright (C) 2023-2024 Arm Technology (China) Co. Ltd.
//
// SPDX-License-Identifier: Apache-2.0


/**
 * @file  aipu.cpp
 * @brief AIPU User Mode Driver (UMD) aipu module header
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include "aipu.h"
#include "ukmemory.h"
#include "job_base.h"
#include "helper.h"

aipudrv::Aipu* aipudrv::Aipu::m_aipu = nullptr;
std::mutex aipudrv::Aipu::m_tex;

aipudrv::Aipu::Aipu()
{
    m_dev_type = DEV_TYPE_AIPU;
}

aipudrv::Aipu::~Aipu()
{
    deinit();
}

aipu_ll_status_t aipudrv::Aipu::init()
{
    aipu_ll_status_t ret = AIPU_LL_STATUS_SUCCESS;
    int kret = 0;
    aipu_cap cap;
    std::vector<aipu_partition_cap> part_caps;

    m_fd = open("/dev/aipu", O_RDWR | O_SYNC);
    if (m_fd <= 0)
    {
        m_fd = 0;
        LOG(LOG_ERR, "open /dev/aipu [fail]");
        dump_stack();
        return AIPU_LL_STATUS_ERROR_OPEN_FAIL;
    }

    kret = ioctl(m_fd, AIPU_IOCTL_QUERY_CAP, &cap);
    if (kret || (cap.partition_cnt == 0))
    {
        LOG(LOG_ERR, "query capability [fail]");
        ret = AIPU_LL_STATUS_ERROR_IOCTL_QUERY_CAP_FAIL;
        goto fail;
    }

    part_caps.resize(cap.partition_cnt);
    memset(part_caps.data(), 0, cap.partition_cnt * sizeof(aipu_partition_cap));
    kret = ioctl(m_fd, AIPU_IOCTL_QUERY_PARTITION_CAP, part_caps.data());
    if (kret)
    {
        LOG(LOG_ERR, "query partition [fail]");
        ret = AIPU_LL_STATUS_ERROR_IOCTL_QUERY_CORE_CAP_FAIL;
        goto fail;
    }

    for (uint32_t i = 0; i < cap.partition_cnt; i++)
        m_part_caps.push_back(part_caps[i]);

    if ((m_part_caps.at(0).version >= AIPU_ISA_VERSION_ZHOUYI_V1)
        && (m_part_caps.at(0).version <= AIPU_ISA_VERSION_ZHOUYI_V2_2))
    {
        m_partition_cnt = 0;
        m_cluster_cnt = 0;

        /* indicate core count for aipu v1/v2 */
        m_core_cnt = cap.partition_cnt;
    } else {
        m_partition_cnt = cap.partition_cnt;

        /* default get the below count from cluster0 in partition0 */
        m_cluster_cnt = m_part_caps.at(0).cluster_cnt;
        m_core_cnt = m_part_caps.at(0).clusters[0].core_cnt;
    }

    m_dram = UKMemory::get_memory(m_fd);
    for (uint32_t i = 0; i < cap.asid_cnt; ++i)
    {
        m_dram->set_asid_base(i, cap.asid_base[i]);
        LOG(LOG_DEBUG, "asid index: %u, asid base: 0x%llx\n", i, cap.asid_base[i]);
    }

    if (m_part_caps.at(0).version == AIPU_ISA_VERSION_ZHOUYI_V1)
        m_dram->set_asid_base(0, 0);

    if (m_part_caps.at(0).version == AIPU_ISA_VERSION_ZHOUYI_V2_2)
        m_dram->set_dtcm_info(cap.dtcm_base, cap.dtcm_size);

    if (m_part_caps.at(0).version == AIPU_ISA_VERSION_ZHOUYI_V3)
    {
        if (cap.gm0_size == 0)
            return AIPU_LL_STATUS_ERROR_IOCTL_QUERY_STATUS_FAIL;

        if (m_dram->is_gm_enable())
        {
            m_dram->set_gm_size(0, cap.gm0_size);
            m_dram->set_gm_size(1, cap.gm1_size);
        }
    } else if (m_part_caps.at(0).version == AIPU_ISA_VERSION_ZHOUYI_V3_1) {
        /* hardware doesn't provide gm size access register */
        if (m_dram->is_gm_enable())
        {
            uint32_t gm_size = 4 * MB_SIZE;
            if (m_core_cnt == 2 || m_core_cnt == 4)
                gm_size = 8 * MB_SIZE;
            m_dram->set_gm_size(0, gm_size);
        }
    }

    return ret;
fail:
    close(m_fd);
    return ret;
}

void aipudrv::Aipu::deinit()
{
    if (m_dram != nullptr)
        m_dram = nullptr;

    if (m_fd > 0)
    {
        ioctl_cmd(AIPU_IOCTL_DISABLE_TICK_COUNTER, nullptr);
        close(m_fd);
        m_fd = 0;
    }
}

bool aipudrv::Aipu::has_target(uint32_t arch, uint32_t version, uint32_t config, uint32_t rev)
{
    for (uint32_t i = 0; i < m_part_caps.size(); i++)
    {
        if ((arch == m_part_caps[i].arch)
            && (version == m_part_caps[i].version)
            && ((version == AIPU_ISA_VERSION_ZHOUYI_V3)
                || (version == AIPU_ISA_VERSION_ZHOUYI_V3_1)
                || (config == m_part_caps[i].config)))
            return true;
    }

    return false;
}

aipu_ll_status_t aipudrv::Aipu::read_reg(uint32_t core_id, uint32_t offset, uint32_t* value)
{
    int kret = 0;
    aipu_io_req ioreq;

    if (value == nullptr)
        return AIPU_LL_STATUS_ERROR_NULL_PTR;

    ioreq.core_id = core_id;
    ioreq.rw = aipu_io_req::AIPU_IO_READ;
    ioreq.offset = offset;
    kret = ioctl(m_fd, AIPU_IOCTL_REQ_IO, &ioreq);
    if (kret)
    {
        LOG(LOG_ERR, "request register read [fail]");
        return AIPU_LL_STATUS_ERROR_IOCTL_REQ_IO_FAIL;
    }

    /* success */
    *value = ioreq.value;
    return AIPU_LL_STATUS_SUCCESS;
}

aipu_ll_status_t aipudrv::Aipu::write_reg(uint32_t core_id, uint32_t offset, uint32_t value)
{
    int kret = 0;
    aipu_io_req ioreq;

    ioreq.core_id = core_id;
    ioreq.rw = aipu_io_req::AIPU_IO_WRITE;
    ioreq.offset = offset;
    ioreq.value = value;
    kret = ioctl(m_fd, AIPU_IOCTL_REQ_IO, &ioreq);
    if (kret)
    {
        LOG(LOG_ERR, "request register write [fail]");
        return AIPU_LL_STATUS_ERROR_IOCTL_REQ_IO_FAIL;
    }

    return AIPU_LL_STATUS_SUCCESS;
}

aipu_status_t aipudrv::Aipu::schedule(const JobDesc& job)
{
    int kret = 0;

    kret = ioctl(m_fd, AIPU_IOCTL_SCHEDULE_JOB, &job.kdesc);
    if (kret)
    {
        LOG(LOG_ERR, "schedule job [fail]");
        return AIPU_STATUS_ERROR_INVALID_OP;
    }

    return AIPU_STATUS_SUCCESS;
}

aipu_ll_status_t aipudrv::Aipu::get_status(uint32_t max_cnt, bool of_this_thread, void *jobbase)
{
    aipu_ll_status_t ret = AIPU_LL_STATUS_JOB_NO_DONE;
    int kret = 0;
    aipu_job_status_query status_query;
    JobBase *job = (JobBase *)jobbase;
    JobBase *done_job = nullptr;
    aipu_job_callback_func_t job_callback_func = nullptr;

    status_query.of_this_thread = of_this_thread;
    status_query.max_cnt = max_cnt;
    status_query.status = new aipu_job_status_desc[max_cnt];
    kret = ioctl(m_fd, AIPU_IOCTL_QUERY_STATUS, &status_query);
    if (kret)
    {
        LOG(LOG_ERR, "query job status [fail]");
        ret = AIPU_LL_STATUS_ERROR_IOCTL_QUERY_STATUS_FAIL;
        goto clean;
    }

    for (uint32_t i = 0; i < status_query.poll_cnt; i++)
    {
        done_job = job->get_base_graph().get_job(status_query.status[i].job_id);
        if (done_job != nullptr)
        {
            /**
             * it must ensure the job's status changing flow obeys
             * AIPU_JOB_STATUS_SCHED->AIPU_JOB_STATUS_DONE/EXCEPTION
             * in asyncronous IO. actually this only costs little time
             * to toggle status. it's absolutely not a bottleneck.
             */
            while (done_job->get_job_status() != AIPU_JOB_STATUS_SCHED);
            done_job->update_job_status(status_query.status[i].state);
            job_callback_func = done_job->get_job_cb();

            /* deliver done job to backend timely. */
            if (job_callback_func != nullptr)
            {
                job_callback_func(status_query.status[i].job_id,
                    (aipu_job_status_t)status_query.status[i].state);
            }

            if (done_job == job)
                ret = AIPU_LL_STATUS_SUCCESS;
        }
    }

clean:
    delete[] status_query.status;
    status_query.status = nullptr;
    return ret;
}

aipu_ll_status_t aipudrv::Aipu::get_status(std::vector<aipu_job_status_desc>& jobs_status,
    uint32_t max_cnt, void *jobbase)
{
    return poll_status(max_cnt, 0, true, jobbase);
}

aipu_ll_status_t aipudrv::Aipu::poll_status(uint32_t max_cnt, int32_t time_out,
    bool of_this_thread, void *jobbase)
{
    aipu_ll_status_t ret = AIPU_LL_STATUS_SUCCESS;
    int kret = 0;
    struct pollfd poll_list;
    JobBase *job = (JobBase *)jobbase;

    /**
     * the laterly committed job maybe finished firstly, but the current polling job
     * isn't the laterly committed job, so it has to cache the laterly committed but
     * firstly finished job. so it's necesssary to check the cache queue containing
     * finished job firstly. if there's no target job, switch to poll NPU HW.
     */
    if (job->get_job_status() == AIPU_JOB_STATUS_DONE)
        return ret;

    poll_list.fd = m_fd;
    poll_list.events = POLLIN | POLLPRI;

    do
    {
        /**
         * it has to ensure the job is in AIPU_JOB_STATUS_SCHED.
         * if lack the checking and when use specific polling thread which may
         * modify job's status as AIPU_JOB_STATUS_DONE before AIPU_JOB_STATUS_SCHED.
         */
        if (job->get_job_status() != AIPU_JOB_STATUS_SCHED)
            continue;

        kret = poll(&poll_list, 1, time_out);
        if (kret < 0)
        {
            LOG(LOG_ERR, "poll /dev/aipu [fail]");
            return AIPU_LL_STATUS_ERROR_POLL_FAIL;
        } else if (kret == 0) {
            return AIPU_LL_STATUS_ERROR_POLL_TIMEOUT;
        }

        /* normally return */
        if ((poll_list.revents & POLLIN) == POLLIN)
        {
            ret = get_status(max_cnt, of_this_thread, jobbase);
            if (ret == AIPU_LL_STATUS_SUCCESS)
                return ret;
        }
    } while (time_out == -1);

    return ret;
}

#define WRITE_DMABUF 1
aipu_ll_status_t readwrite_dmabuf_helper(int devfd, aipu_dmabuf_op_t *dmabuf_op, bool write)
{
    aipu_ll_status_t ret = AIPU_LL_STATUS_ERROR_IOCTL_FAIL;
    struct aipu_dma_buf dma_buf = {0};
    char *va = nullptr;
    int kret = 0;

    dma_buf.fd = dmabuf_op->dmabuf_fd;
    if (dmabuf_op->data == nullptr)
    {
        LOG(LOG_ERR, "dmabuf_op: dma_buf is null");
        goto out;
    }

    kret = ioctl(devfd, AIPU_IOCTL_GET_DMA_BUF_INFO, &dma_buf);
    if (kret < 0)
    {
        LOG(LOG_ERR, "dmabuf_op: query dma_buf [fail]");
        goto out;
    }

    if (dmabuf_op->offset_in_dmabuf + dmabuf_op->size > dma_buf.bytes)
    {
        LOG(LOG_ERR, "dmabuf_op: access beyond dma_buf scope");
        goto out;
    }

    va = (char *)mmap(NULL, dma_buf.bytes, PROT_READ|PROT_WRITE, MAP_SHARED,
        dmabuf_op->dmabuf_fd, 0);
    if (va == MAP_FAILED)
    {
        LOG(LOG_ERR, "dmabuf_op: mmap dmabuf [fail]");
        goto out;
    }

    if (write)
        memcpy(va + dmabuf_op->offset_in_dmabuf, dmabuf_op->data, dmabuf_op->size);
    else
        memcpy(dmabuf_op->data, va + dmabuf_op->offset_in_dmabuf, dmabuf_op->size);

    munmap(va, dma_buf.bytes);
    ret = AIPU_LL_STATUS_SUCCESS;

out:
    return ret;
}

aipu_ll_status_t aipudrv::Aipu::ioctl_cmd(uint32_t cmd, void *arg)
{
    aipu_ll_status_t ret = AIPU_LL_STATUS_SUCCESS;
    int kret = 0;

    switch (cmd)
    {
        case AIPU_IOCTL_ABORT_CMD_POOL:
            kret = ioctl(m_fd, AIPU_IOCTL_ABORT_CMD_POOL);
            if (kret < 0)
            {
                LOG(LOG_ERR, "abort cmdpool [fail]");
                ret = AIPU_LL_STATUS_ERROR_IOCTL_ABORT_CMDPOOL;
            }
            break;

        case AIPU_IOCTL_ENABLE_TICK_COUNTER:
            if (!m_tick_counter)
            {
                kret = ioctl(m_fd, AIPU_IOCTL_ENABLE_TICK_COUNTER);
                if (kret < 0)
                {
                    LOG(LOG_ERR, "enable tick counter [fail]");
                    ret = AIPU_LL_STATUS_ERROR_IOCTL_TICK_COUNTER;
                }
                m_tick_counter = true;
            }
            break;

        case AIPU_IOCTL_DISABLE_TICK_COUNTER:
            if (m_tick_counter)
            {
                kret = ioctl(m_fd, AIPU_IOCTL_DISABLE_TICK_COUNTER);
                if (kret < 0)
                {
                    LOG(LOG_ERR, "disable tick counter [fail]");
                    ret = AIPU_LL_STATUS_ERROR_IOCTL_TICK_COUNTER;
                }
                m_tick_counter = false;
            }
            break;

        case AIPU_IOCTL_CONFIG_CLUSTERS:
            kret = ioctl(m_fd, AIPU_IOCTL_CONFIG_CLUSTERS, arg);
            if (kret < 0)
            {
                LOG(LOG_ERR, "config cluster [fail]");
                ret = AIPU_LL_STATUS_ERROR_CONFIG_CLUSTER;
            }
            break;

        case AIPU_IOCTL_ALLOC_DMABUF:
            {
            struct aipu_dma_buf_request *dmabuf_reg = (struct aipu_dma_buf_request *)arg;
            struct aipu_dma_buf dma_buf = {0};
            std::string name;

            kret = ioctl(m_fd, AIPU_IOCTL_ALLOC_DMA_BUF, dmabuf_reg);
            if (kret < 0)
            {
                LOG(LOG_ERR, "alloc dma_buf [fail]");
                ret = AIPU_LL_STATUS_ERROR_IOCTL_FAIL;
                goto out;
            }

            dma_buf.fd = dmabuf_reg->fd;
            kret = ioctl(m_fd, AIPU_IOCTL_GET_DMA_BUF_INFO, &dma_buf);
            if (kret < 0)
            {
                LOG(LOG_ERR, "get dma_buf [fail]");
                ret = AIPU_LL_STATUS_ERROR_IOCTL_FAIL;
                goto out;
            }
            name = "dmabuf_fd_" + std::to_string(dma_buf.fd);
            m_dma_buf_map[dma_buf.fd] = dma_buf;
            m_dram->add_tracking(dma_buf.pa, dma_buf.bytes,
                MemOperationAlloc, name.c_str(), false, 0);
            }
            break;

        case AIPU_IOCTL_FREE_DMABUF:
            {
            int dma_buf_fd = *(int *)arg;
            kret = ioctl(m_fd, AIPU_IOCTL_FREE_DMA_BUF, &dma_buf_fd);
            if (kret < 0)
            {
                LOG(LOG_ERR, "free dma_buf [fail], fd=%d", dma_buf_fd);
                ret = AIPU_LL_STATUS_ERROR_IOCTL_FAIL;
                goto out;
            }

            if (m_dma_buf_map.count(dma_buf_fd) == 1)
            {
                struct aipu_dma_buf dma_buf = m_dma_buf_map[dma_buf_fd];
                std::string name = "dmabuf_fd_" + std::to_string(dma_buf.fd);
                m_dram->add_tracking(dma_buf.pa, dma_buf.bytes,
                    MemOperationFree, name.c_str(), false, 0);
                m_dma_buf_map.erase(dma_buf_fd);
                close(dma_buf_fd);
            }
            }
            break;

        case AIPU_IOCTL_GET_DMA_BUF_INFO:
            kret = ioctl(m_fd, AIPU_IOCTL_GET_DMA_BUF_INFO, arg);
            if (kret < 0)
            {
                LOG(LOG_ERR, "get dma_buf [fail]");
                ret = AIPU_LL_STATUS_ERROR_IOCTL_FAIL;
            }
            break;

        case AIPU_IOCTL_WRITE_DMABUF:
            ret = readwrite_dmabuf_helper(m_fd, (aipu_dmabuf_op_t *)arg, WRITE_DMABUF);
            break;

        case AIPU_IOCTL_READ_DMABUF:
            ret = readwrite_dmabuf_helper(m_fd, (aipu_dmabuf_op_t *)arg, !WRITE_DMABUF);
            break;

        case AIPU_IOCTL_ATTACH_DMABUF:
            {
                struct aipu_dma_buf *dma_buf = (struct aipu_dma_buf *)arg;
                std::string name;

                kret = ioctl(m_fd, AIPU_IOCTL_ATTACH_DMA_BUF, dma_buf);
                if (kret < 0)
                {
                    LOG(LOG_ERR, "Attatch dma_buf [fail]");
                    ret = AIPU_LL_STATUS_ERROR_IOCTL_FAIL;
                    goto out;
                }
                name = "dmabuf_fd_" + std::to_string(dma_buf->fd);
                m_dma_buf_map[dma_buf->fd] = *dma_buf;
                m_dram->add_tracking(dma_buf->pa, dma_buf->bytes,
                    MemOperationAlloc, name.c_str(), false, 0);
                break;
            }

        case AIPU_IOCTL_DETACH_DMABUF:
            {
                int dmabuf_fd = *(int *)arg;

                kret = ioctl(m_fd, AIPU_IOCTL_DETACH_DMA_BUF, &dmabuf_fd);
                if (kret < 0)
                {
                    LOG(LOG_ERR, "Detatch dma_buf [fail]");
                    ret = AIPU_LL_STATUS_ERROR_IOCTL_FAIL;
                    goto out;
                }

                if (m_dma_buf_map.count(dmabuf_fd) == 1)
                {
                    struct aipu_dma_buf dma_buf = m_dma_buf_map[dmabuf_fd];
                    std::string name = "dmabuf_fd_" + std::to_string(dma_buf.fd);
                    m_dram->add_tracking(dma_buf.pa, dma_buf.bytes,
                        MemOperationFree, name.c_str(), false, 0);
                    m_dma_buf_map.erase(dmabuf_fd);
                }
                break;
            }
        case AIPU_IOCTL_GET_VERSION:
            {
                aipu_driver_version_t *drv_ver = (aipu_driver_version_t *)arg;

                kret = ioctl(m_fd, AIPU_IOCTL_GET_DRIVER_VERSION, drv_ver->kmd_version);
                if (kret < 0)
                {
                    LOG(LOG_ERR, "get kmd version [fail]");
                    ret = AIPU_LL_STATUS_ERROR_IOCTL_FAIL;
                }
            }
            break;

        default:
            LOG(LOG_ERR, "AIPU can't support cmd: %d\n", cmd);
            ret = AIPU_LL_STATUS_ERROR_OPERATION_UNSUPPORTED;
    }

out:
    return ret;
}

int aipudrv::Aipu::get_grid_id(uint16_t &grid_id)
{
    int ret = 0;

    grid_id = 0;
    if (ioctl(m_fd, AIPU_IOCTL_ALLOC_GRID_ID, &grid_id) < 0)
    {
        LOG(LOG_ERR, "Alloc grid id [fail]");
        ret = -1;
        goto out;
    }

out:
    return ret;
}

int aipudrv::Aipu::get_start_group_id(int group_cnt, uint16_t &start_group_id)
{
    int ret = 0;
    struct aipu_group_id_desc id_desc = {0};

    if (group_cnt == 0)
        return ret;

    id_desc.group_size = group_cnt;
    if (ioctl(m_fd, AIPU_IOCTL_ALLOC_GROUP_ID, &id_desc) < 0)
    {
        LOG(LOG_ERR, "Alloc group id [fail]");
        ret = -1;
        goto out;
    }

    start_group_id = id_desc.first_id;

out:
    return ret;
}