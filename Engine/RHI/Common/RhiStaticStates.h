#pragma once

#include "Engine/RHI/Common/RhiTypes.h"

namespace ve::rhi
{
    template<bool BlendEnabled = false,
             RhiBlendFactor SourceColorBlendFactor = RhiBlendFactor::One,
             RhiBlendFactor DestinationColorBlendFactor = RhiBlendFactor::Zero,
             RhiBlendOperation ColorBlendOperation = RhiBlendOperation::Add,
             RhiBlendFactor SourceAlphaBlendFactor = RhiBlendFactor::One,
             RhiBlendFactor DestinationAlphaBlendFactor = RhiBlendFactor::Zero,
             RhiBlendOperation AlphaBlendOperation = RhiBlendOperation::Add,
             uint8_t ColorWriteMask = RhiColorWriteAll,
             bool AlphaToCoverageEnabled = false>
    struct TStaticBlendState
    {
        [[nodiscard]] static constexpr RhiBlendStateDesc GetDesc() noexcept
        {
            RhiBlendStateDesc desc = {};
            desc.alphaToCoverageEnabled = AlphaToCoverageEnabled;
            desc.renderTargets[0].blendEnabled = BlendEnabled;
            desc.renderTargets[0].sourceColorBlendFactor = SourceColorBlendFactor;
            desc.renderTargets[0].destinationColorBlendFactor = DestinationColorBlendFactor;
            desc.renderTargets[0].colorBlendOperation = ColorBlendOperation;
            desc.renderTargets[0].sourceAlphaBlendFactor = SourceAlphaBlendFactor;
            desc.renderTargets[0].destinationAlphaBlendFactor = DestinationAlphaBlendFactor;
            desc.renderTargets[0].alphaBlendOperation = AlphaBlendOperation;
            desc.renderTargets[0].colorWriteMask = ColorWriteMask;
            return desc;
        }
    };

    template<RhiFillMode FillMode = RhiFillMode::Solid,
             RhiCullMode CullMode = RhiCullMode::Back,
             bool FrontCounterClockwise = false,
             bool DepthClipEnabled = true,
             bool ScissorEnabled = false,
             bool MultisampleEnabled = false,
             bool AntialiasedLineEnabled = false,
             int32_t DepthBias = 0>
    struct TStaticRasterizerState
    {
        [[nodiscard]] static constexpr RhiRasterizerStateDesc GetDesc() noexcept
        {
            RhiRasterizerStateDesc desc = {};
            desc.fillMode = FillMode;
            desc.cullMode = CullMode;
            desc.frontCounterClockwise = FrontCounterClockwise;
            desc.depthClipEnabled = DepthClipEnabled;
            desc.scissorEnabled = ScissorEnabled;
            desc.multisampleEnabled = MultisampleEnabled;
            desc.antialiasedLineEnabled = AntialiasedLineEnabled;
            desc.depthBias = DepthBias;
            return desc;
        }
    };

    template<bool DepthTestEnabled = false,
             bool DepthWriteEnabled = false,
             RhiCompareFunction DepthCompareFunction = RhiCompareFunction::LessEqual,
             bool StencilEnabled = false,
             uint8_t StencilReadMask = 0xFF,
             uint8_t StencilWriteMask = 0xFF>
    struct TStaticDepthStencilState
    {
        [[nodiscard]] static constexpr RhiDepthStencilStateDesc GetDesc() noexcept
        {
            RhiDepthStencilStateDesc desc = {};
            desc.depthTestEnabled = DepthTestEnabled;
            desc.depthWriteEnabled = DepthWriteEnabled;
            desc.depthCompareFunction = DepthCompareFunction;
            desc.stencilEnabled = StencilEnabled;
            desc.stencilReadMask = StencilReadMask;
            desc.stencilWriteMask = StencilWriteMask;
            return desc;
        }
    };

    template<RhiSamplerFilter Filter = RhiSamplerFilter::Bilinear,
             RhiSamplerAddressMode AddressU = RhiSamplerAddressMode::Wrap,
             RhiSamplerAddressMode AddressV = AddressU,
             RhiSamplerAddressMode AddressW = AddressU,
             RhiSamplerReductionMode ReductionMode = RhiSamplerReductionMode::Standard,
             RhiCompareFunction ComparisonFunction = RhiCompareFunction::LessEqual,
             uint32_t MaxAnisotropy = 1>
    struct TStaticSamplerState
    {
        [[nodiscard]] static constexpr RhiSamplerDesc GetDesc() noexcept
        {
            RhiSamplerDesc desc = {};
            desc.filter = Filter;
            desc.addressU = AddressU;
            desc.addressV = AddressV;
            desc.addressW = AddressW;
            desc.reductionMode = ReductionMode;
            desc.comparisonFunction = ComparisonFunction;
            desc.maxAnisotropy = MaxAnisotropy;
            return desc;
        }
    };

    namespace StaticRenderStates
    {
        inline constexpr RhiBlendStateDesc OpaqueBlend = TStaticBlendState<>::GetDesc();
        inline constexpr RhiBlendStateDesc AlphaBlend = TStaticBlendState<true,
                                                                          RhiBlendFactor::SourceAlpha,
                                                                          RhiBlendFactor::OneMinusSourceAlpha,
                                                                          RhiBlendOperation::Add,
                                                                          RhiBlendFactor::One,
                                                                          RhiBlendFactor::OneMinusSourceAlpha>::GetDesc();
        inline constexpr RhiBlendStateDesc AdditiveBlend =
            TStaticBlendState<true, RhiBlendFactor::SourceAlpha, RhiBlendFactor::One, RhiBlendOperation::Add, RhiBlendFactor::One, RhiBlendFactor::One>::GetDesc();

        inline constexpr RhiRasterizerStateDesc SolidBackCullRasterizer = TStaticRasterizerState<>::GetDesc();
        inline constexpr RhiRasterizerStateDesc SolidNoCullRasterizer = TStaticRasterizerState<RhiFillMode::Solid, RhiCullMode::None>::GetDesc();
        inline constexpr RhiRasterizerStateDesc WireframeBackCullRasterizer = TStaticRasterizerState<RhiFillMode::Wireframe, RhiCullMode::Back>::GetDesc();

        inline constexpr RhiDepthStencilStateDesc DepthDisabled = TStaticDepthStencilState<>::GetDesc();
        inline constexpr RhiDepthStencilStateDesc DepthReadOnlyLessEqual =
            TStaticDepthStencilState<true, false, RhiCompareFunction::LessEqual>::GetDesc();
        inline constexpr RhiDepthStencilStateDesc DepthReadWriteLessEqual =
            TStaticDepthStencilState<true, true, RhiCompareFunction::LessEqual>::GetDesc();
        inline constexpr RhiDepthStencilStateDesc DepthReadWriteAlways = TStaticDepthStencilState<true, true, RhiCompareFunction::Always>::GetDesc();

        inline constexpr RhiSamplerDesc PointWrapSampler = TStaticSamplerState<RhiSamplerFilter::Point>::GetDesc();
        inline constexpr RhiSamplerDesc BilinearWrapSampler = TStaticSamplerState<>::GetDesc();
        inline constexpr RhiSamplerDesc BilinearClampSampler =
            TStaticSamplerState<RhiSamplerFilter::Bilinear, RhiSamplerAddressMode::Clamp>::GetDesc();
        inline constexpr RhiSamplerDesc TrilinearWrapSampler = TStaticSamplerState<RhiSamplerFilter::Trilinear>::GetDesc();
        inline constexpr RhiSamplerDesc AnisotropicWrapSampler = TStaticSamplerState<RhiSamplerFilter::Anisotropic,
                                                                                    RhiSamplerAddressMode::Wrap,
                                                                                    RhiSamplerAddressMode::Wrap,
                                                                                    RhiSamplerAddressMode::Wrap,
                                                                                    RhiSamplerReductionMode::Standard,
                                                                                    RhiCompareFunction::LessEqual,
                                                                                    8>::GetDesc();
    } // namespace StaticRenderStates
} // namespace ve::rhi
