/*
 *  Copyright 2019-2022 Diligent Graphics LLC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#include "SerializableResourceSignatureImpl.hpp"
#include "PipelineResourceSignatureBase.hpp"
#include "FixedLinearAllocator.hpp"
#include "EngineMemory.h"
#include "PSOSerializer.hpp"

namespace Diligent
{

DeviceObjectArchiveBase::DeviceType ArchiveDeviceDataFlagToArchiveDeviceType(ARCHIVE_DEVICE_DATA_FLAGS DataTypeFlag);

SerializableResourceSignatureImpl::SerializableResourceSignatureImpl(IReferenceCounters*                  pRefCounters,
                                                                     SerializationDeviceImpl*             pDevice,
                                                                     const PipelineResourceSignatureDesc& Desc,
                                                                     ARCHIVE_DEVICE_DATA_FLAGS            DeviceFlags,
                                                                     SHADER_TYPE                          ShaderStages) :
    TBase{pRefCounters},
    m_Name{Desc.Name}
{
    ValidatePipelineResourceSignatureDesc(Desc, pDevice->GetDevice());

    if ((DeviceFlags & pDevice->GetValidDeviceFlags()) != DeviceFlags)
    {
        LOG_ERROR_AND_THROW("DeviceFlags contain unsupported device type");
    }

    while (DeviceFlags != ARCHIVE_DEVICE_DATA_FLAG_NONE)
    {
        const auto Flag    = ExtractLSB(DeviceFlags);
        const auto DevType = ArchiveDeviceDataFlagToArchiveDeviceType(Flag);

        static_assert(ARCHIVE_DEVICE_DATA_FLAG_LAST == ARCHIVE_DEVICE_DATA_FLAG_METAL_IOS, "Please update the switch below to handle the new device data type");
        switch (Flag)
        {
#if D3D11_SUPPORTED
            case ARCHIVE_DEVICE_DATA_FLAG_D3D11:
                CreateDeviceSignature<PipelineResourceSignatureD3D11Impl>(DevType, Desc, ShaderStages);
                break;
#endif
#if D3D12_SUPPORTED
            case ARCHIVE_DEVICE_DATA_FLAG_D3D12:
                CreateDeviceSignature<PipelineResourceSignatureD3D12Impl>(DevType, Desc, ShaderStages);
                break;
#endif
#if GL_SUPPORTED || GLES_SUPPORTED
            case ARCHIVE_DEVICE_DATA_FLAG_GL:
            case ARCHIVE_DEVICE_DATA_FLAG_GLES:
                CreateDeviceSignature<PipelineResourceSignatureGLImpl>(DevType, Desc, ShaderStages);
                break;
#endif
#if VULKAN_SUPPORTED
            case ARCHIVE_DEVICE_DATA_FLAG_VULKAN:
                CreateDeviceSignature<PipelineResourceSignatureVkImpl>(DevType, Desc, ShaderStages);
                break;
#endif
#if METAL_SUPPORTED
            case ARCHIVE_DEVICE_DATA_FLAG_METAL_MACOS:
            case ARCHIVE_DEVICE_DATA_FLAG_METAL_IOS:
                CreateDeviceSignature<PipelineResourceSignatureMtlImpl>(DevType, Desc, ShaderStages);
                break;
#endif
            case ARCHIVE_DEVICE_DATA_FLAG_NONE:
                UNEXPECTED("ARCHIVE_DEVICE_DATA_FLAG_NONE(0) should never occur");
                break;

            default:
                LOG_ERROR_MESSAGE("Unexpected render device type");
                break;
        }
    }
}

SerializableResourceSignatureImpl::SerializableResourceSignatureImpl(IReferenceCounters* pRefCounters, const char* Name) noexcept :
    TBase{pRefCounters},
    m_Name{Name}
{
}

SerializableResourceSignatureImpl::~SerializableResourceSignatureImpl()
{
}

const PipelineResourceSignatureDesc& SerializableResourceSignatureImpl::GetDesc() const
{
    for (const auto& pDeviceSign : m_pDeviceSignatures)
    {
        if (pDeviceSign)
            return pDeviceSign->GetPRS()->GetDesc();
    }

    UNEXPECTED("No device signatures initialized!");
    static constexpr PipelineResourceSignatureDesc NullDesc;
    return NullDesc;
}


void SerializableResourceSignatureImpl::InitCommonData(const PipelineResourceSignatureDesc& Desc)
{
    VERIFY(m_Name == Desc.Name, "Inconsistent signature name");

    if (!m_CommonData)
    {
        // Use description of the first signature as common description.

        Serializer<SerializerMode::Measure> MeasureSer;
        PSOSerializer<SerializerMode::Measure>::SerializePRSDesc(MeasureSer, Desc, nullptr);

        m_CommonData = SerializedMemory{MeasureSer.GetSize(nullptr)};
        Serializer<SerializerMode::Write> WSer{m_CommonData.Ptr(), m_CommonData.Size()};
        PSOSerializer<SerializerMode::Write>::SerializePRSDesc(WSer, Desc, nullptr);
        VERIFY_EXPR(WSer.IsEnd());

        VERIFY_EXPR(GetDesc() == Desc);
    }
}

bool SerializableResourceSignatureImpl::IsCompatible(const SerializableResourceSignatureImpl& Rhs, ARCHIVE_DEVICE_DATA_FLAGS DeviceFlags) const
{
    while (DeviceFlags != ARCHIVE_DEVICE_DATA_FLAG_NONE)
    {
        const auto DataTypeFlag      = ExtractLSB(DeviceFlags);
        auto       ArchiveDeviceType = ArchiveDeviceDataFlagToArchiveDeviceType(DataTypeFlag);

        if (ArchiveDeviceType == DeviceType::Metal_MacOS)
            ArchiveDeviceType = DeviceType::Metal_iOS;

        const auto* pPRS0 = GetPRS(ArchiveDeviceType);
        const auto* pPRS1 = Rhs.GetPRS(ArchiveDeviceType);
        if ((pPRS0 == nullptr) != (pPRS1 == nullptr))
            return false;

        if ((pPRS0 != nullptr) && (pPRS1 != nullptr) && !pPRS0->IsCompatibleWith(pPRS1))
            return false;
    }
    return true;
}

bool SerializableResourceSignatureImpl::operator==(const SerializableResourceSignatureImpl& Rhs) const
{
    if (GetCommonData() != Rhs.GetCommonData())
        return false;

    for (size_t type = 0; type < DeviceCount; ++type)
    {
        const auto  Type  = static_cast<DeviceType>(type);
        const auto* pMem0 = GetDeviceData(Type);
        const auto* pMem1 = Rhs.GetDeviceData(Type);

        if ((pMem0 != nullptr) != (pMem1 != nullptr))
            return false;

        if ((pMem0 != nullptr) && (pMem1 != nullptr) && (*pMem0 != *pMem1))
            return false;
    }

    return true;
}

size_t SerializableResourceSignatureImpl::CalcHash() const
{
    size_t Hash = 0;
    for (size_t type = 0; type < DeviceCount; ++type)
    {
        const auto* pMem = GetDeviceData(static_cast<DeviceType>(type));
        if (pMem != nullptr)
            HashCombine(Hash, pMem->CalcHash());
    }
    return Hash;
}

} // namespace Diligent
