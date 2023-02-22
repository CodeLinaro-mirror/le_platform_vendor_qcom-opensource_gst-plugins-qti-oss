/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
Changes from Qualcomm Innovation Center are provided under the following license:

Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted (subject to the limitations in the
disclaimer below) provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

    * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "C2ComponentInterfaceAdapter.h"

#include <gst/gst.h>

GST_DEBUG_CATEGORY_EXTERN(gst_qcodec2_wrapper_debug);
#define GST_CAT_DEFAULT gst_qcodec2_wrapper_debug

namespace QTI {

C2ComponentInterfaceAdapter::C2ComponentInterfaceAdapter(std::shared_ptr<C2ComponentInterface> compIntf)
{

    mCompIntf = std::move(compIntf);
}

C2ComponentInterfaceAdapter::~C2ComponentInterfaceAdapter()
{
    LOG_MESSAGE("delete C2 Component Interface Adapter");
    mCompIntf = nullptr;
}

C2String C2ComponentInterfaceAdapter::getName() const
{

    return mCompIntf->getName();
}

c2_node_id_t C2ComponentInterfaceAdapter::getId() const
{

    return mCompIntf->getId();
}

c2_status_t C2ComponentInterfaceAdapter::initReflectedParamUpdater(std::shared_ptr<C2ParamReflector>& reflector)
{

    LOG_MESSAGE("Init ReflectedParamUpdater");

    c2_status_t result = C2_NO_INIT;

    mParamUpdater = nullptr;
    std::vector<std::shared_ptr<C2ParamDescriptor> > supportedParams;

    result = mCompIntf->querySupportedParams_nb(&supportedParams);
    if (C2_OK == result) {
        mParamUpdater = std::make_shared<android::ReflectedParamUpdater>();

        mParamUpdater->clear();
        mParamUpdater->addParamDesc(reflector, supportedParams);
    } else {
        LOG_ERROR("Failed(%d) to query supported params", result);
    }

    return result;
}

std::unique_ptr<C2Param> C2ComponentInterfaceAdapter::updateParamFromConfig(
    const android::ReflectedParamUpdater::Dict& kvpairs)
{

    if (mParamUpdater == nullptr)
        return nullptr;

    std::vector<C2Param::Index> paramIndices;
    mParamUpdater->getParamIndicesFromMessage(kvpairs, &paramIndices);

    for (const auto& index : paramIndices) {
        LOG_MESSAGE("update param index name = %s", mParamUpdater->getParamName(index).c_str());
    }

    std::vector<std::unique_ptr<C2Param> > updateParams;

    if (mCompIntf->query_vb({}, paramIndices, C2_MAY_BLOCK, &updateParams) == C2_OK) {
        mParamUpdater->updateParamsFromMessage(kvpairs, &updateParams);
        LOG_MESSAGE("update param vector size = %zu", updateParams.size());

        if (updateParams.size() > 0) {

            return std::move(updateParams[0]);
        }
    }

    return nullptr;
}

android::ReflectedParamUpdater::Dict C2ComponentInterfaceAdapter::getParams(
    const std::vector<std::unique_ptr<C2Param> >& params)
{
    android::ReflectedParamUpdater::Dict dict;
    if (mParamUpdater == nullptr)
        return dict;

    return mParamUpdater->getParams(params);
}

c2_status_t C2ComponentInterfaceAdapter::config(const std::vector<C2Param*>& stackParams, c2_blocking_t mayBlock)
{

    LOG_MESSAGE("Component interface (%p) configured", this);

    c2_status_t result = C2_NO_INIT;
    std::vector<std::unique_ptr<C2SettingResult> > failures;

    result = mCompIntf->config_vb(stackParams, mayBlock, &failures);
    if ((C2_OK != result) || (failures.size() != 0)) {
        LOG_WARNING("Configuration failed(%d)", static_cast<int32_t>(result));
    }

    return result;
}

c2_status_t C2ComponentInterfaceAdapter::setComponent(std::weak_ptr<C2Component> comp)
{

    c2_status_t result = C2_NO_INIT;

    if (!comp.expired()) {
        mConnectedComponent = comp;
        result = C2_OK;
    }

    return result;
}

} // namespace QTI
