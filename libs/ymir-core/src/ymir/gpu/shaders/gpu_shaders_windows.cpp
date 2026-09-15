#include <ymir/gpu/shaders/gpu_shaders.hpp>

#include <combaseapi.h>
#include <dxcapi.h>

#include <wil/com.h>

#if YMIR_PLATFORM_HAS_VULKAN
    #include <spirv-tools/libspirv.hpp>
#endif

#include <ymir/util/string.hpp>

#include <ymir/core/types.hpp>

#include <string>
#include <string_view>

namespace ymir::gpu {

static const wchar_t *GetShaderProfile(ShaderStage stage) {
    // TODO: allow selecting version
    switch (stage) {
    case ShaderStage::Vertex: return L"vs_6_6";
    case ShaderStage::Pixel: return L"ps_6_6";
    case ShaderStage::Compute: return L"cs_6_6";
    default: return nullptr;
    }
}

template <ShaderStage stage>
util::ValueResult<CompiledShader<stage>> DoCompileShader(const ShaderCompileSpec<stage> &spec) {
    // Validate preconditions
    if (spec.language != ShaderLanguage::HLSL) {
        return util::ErrorMessage{"Unsupported shader language provided to DXC compiler"};
    }
    if (spec.format != ShaderBytecodeFormat::DXIL && spec.format != ShaderBytecodeFormat::SPIRV) {
        return util::ErrorMessage{"Unsupported shader bytecode format provided to DXC compiler"};
    }
    const wchar_t *shaderProfile = GetShaderProfile(stage);
    if (shaderProfile == nullptr) {
        return util::ErrorMessage{"Invalid shader stage provided to DXC compiler"};
    }

    HRESULT hr;

    // Create utils and compiler objects
    wil::com_ptr_nothrow<IDxcUtils> utils{};
    hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(utils.put()));
    if (FAILED(hr)) {
        return util::ErrorMessage{fmt::format("Failed to create DXC utils: error code {:X}", (UINT)hr)};
    }

    wil::com_ptr_nothrow<IDxcCompiler3> compiler{};
    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(compiler.put()));
    if (FAILED(hr)) {
        return util::ErrorMessage{fmt::format("Failed to create DXC compiler: error code {:X}", (UINT)hr)};
    }

    // Create shader source blob
    const DxcBuffer sourceBlob{
        .Ptr = spec.sourceCode.c_str(),
        .Size = spec.sourceCode.size(),
        .Encoding = CP_UTF8,
    };

    // Convert source name to WSTR
    std::wstring sourceNameStr{};
    LPCWSTR sourceName;
    if (spec.name.empty()) {
        sourceName = nullptr;
    } else {
        sourceNameStr = util::StringToWString(spec.name);
        sourceName = sourceNameStr.c_str();
    }
    std::wstring entrypoint = util::StringToWString(spec.entrypoint);

    // Set up arguments list
    std::vector<LPCWSTR> args{};
    if (spec.debug) {
        args.push_back(L"-Zi");
        args.push_back(L"-Qembed_debug");
    } else {
        args.push_back(L"-Qstrip_debug");
    }
    if (spec.optimize) {
        args.push_back(L"-O3");
    } else {
        args.push_back(L"-Od");
    }
    if (spec.format == ShaderBytecodeFormat::SPIRV) {
        args.push_back(L"-spirv");
    }
    // TODO: set up additional arguments as needed

    // Convert macros from strings to wstrings
    struct WideMacro {
        std::wstring name;
        std::wstring value;
    };
    std::vector<WideMacro> wideMacros{};
    for (auto &[name, value] : spec.macros) {
        auto &wideMacro = wideMacros.emplace_back();
        wideMacro.name = util::StringToWString(name);
        wideMacro.value = util::StringToWString(value);
    }

    // Build defines list
    std::vector<DxcDefine> defines{};
    for (auto &[name, value] : wideMacros) {
        auto &define = defines.emplace_back();
        define.Name = name.c_str();
        define.Value = value.empty() ? nullptr : value.c_str();
    }

    // Build compiler arguments
    wil::com_ptr_nothrow<IDxcCompilerArgs> dxcArgs{};
    hr = utils->BuildArguments(sourceName, entrypoint.c_str(), shaderProfile, args.data(), args.size(), defines.data(),
                               defines.size(), dxcArgs.put());
    if (FAILED(hr)) {
        return util::ErrorMessage{fmt::format("Failed to create DXC compiler arguments: error code {:X}", (UINT)hr)};
    }

    // Create include handler
    // TODO: implement include handler, delegate to frontend
    IDxcIncludeHandler *includeHandler = nullptr;

    // Compile shader
    wil::com_ptr_nothrow<IDxcOperationResult> result{};
    hr = compiler->Compile(&sourceBlob, dxcArgs->GetArguments(), dxcArgs->GetCount(), includeHandler,
                           IID_PPV_ARGS(result.put()));
    if (FAILED(hr)) {
        return util::ErrorMessage{fmt::format("Failed to compile shader: error code {:X}", (UINT)hr)};
    }

    // Check status
    HRESULT hrStatus;
    hr = result->GetStatus(&hrStatus);
    if (FAILED(hr) || FAILED(hrStatus)) {
        // Get compilation errors
        wil::com_ptr_nothrow<IDxcBlobEncoding> errors{};
        if (SUCCEEDED(result->GetErrorBuffer(errors.put()))) {
            wil::com_ptr_nothrow<IDxcBlobUtf8> errorsU8{};
            if (SUCCEEDED(errors->QueryInterface(IID_PPV_ARGS(&errorsU8))) && errorsU8 != nullptr) {
                return util::ErrorMessage{fmt::format("Failed to compile shader: error code {:X}, details:\n{}",
                                                      (UINT)hrStatus, errorsU8->GetStringPointer())};
            }
            std::string errorMessage{static_cast<char *>(errors->GetBufferPointer()), errors->GetBufferSize()};
            return util::ErrorMessage{
                fmt::format("Failed to compile shader: error code {:X}, details:\n{}", (UINT)hrStatus, errorMessage)};
        }
        return util::ErrorMessage{
            fmt::format("Failed to compile shader: error code {:X}, no details available", (UINT)hrStatus)};
    }

    // Get compiled shader bytecode
    wil::com_ptr_nothrow<IDxcBlob> shaderBlob{};
    hr = result->GetResult(shaderBlob.put());
    if (FAILED(hr)) {
        return util::ErrorMessage{fmt::format("Failed to retrieve compiled shader blob: error code {:X}", (UINT)hr)};
    }
    std::vector<char> bytecode{};
    bytecode.resize(shaderBlob->GetBufferSize());
    std::copy_n(static_cast<char *>(shaderBlob->GetBufferPointer()), shaderBlob->GetBufferSize(), bytecode.begin());

    return CompiledShader<stage>{
        .language = spec.language,
        .format = spec.format,
        .bytecode = std::move(bytecode),
        .entrypoint = spec.entrypoint,
    };
}

template <ShaderStage stage>
static util::VoidResult<> ValidateShaderDXC(CompiledShader<stage> &spec) {
    HRESULT hr;

    // Create utils and validator objects
    wil::com_ptr_nothrow<IDxcUtils> utils{};
    hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(utils.put()));
    if (FAILED(hr)) {
        return util::ErrorMessage{fmt::format("Failed to create DXC utils: error code {:X}", (UINT)hr)};
    }

    wil::com_ptr_nothrow<IDxcValidator2> validator{};
    hr = DxcCreateInstance(CLSID_DxcValidator, IID_PPV_ARGS(validator.put()));
    if (FAILED(hr)) {
        return util::ErrorMessage{fmt::format("Could not create DXC validator: error code {:X}", (UINT)hr)};
    }

    wil::com_ptr_nothrow<IDxcBlobEncoding> shaderBlob{};
    hr = utils->CreateBlobFromPinned(spec.bytecode.data(), spec.bytecode.size(), DXC_CP_ACP, shaderBlob.put());
    if (FAILED(hr)) {
        return util::ErrorMessage{fmt::format("Could not create shader blob: error code {:X}", (UINT)hr)};
    }

    // Validate shader
    wil::com_ptr_nothrow<IDxcOperationResult> result{};
    hr = validator->Validate(shaderBlob.get(), DxcValidatorFlags_Default, result.put());
    if (FAILED(hr)) {
        return util::ErrorMessage{fmt::format("Failed to validate shader: error code {:X}", (UINT)hr)};
    }

    // Check status
    HRESULT hrStatus;
    hr = result->GetStatus(&hrStatus);
    if (FAILED(hr) || FAILED(hrStatus)) {
        // Get compilation errors
        wil::com_ptr_nothrow<IDxcBlobEncoding> errors{};
        if (SUCCEEDED(result->GetErrorBuffer(errors.put()))) {
            wil::com_ptr_nothrow<IDxcBlobUtf8> errorsU8{};
            if (SUCCEEDED(errors->QueryInterface(IID_PPV_ARGS(&errorsU8))) && errorsU8 != nullptr) {
                return util::ErrorMessage{fmt::format("Failed to validate shader: error code {:X}, details:\n{}",
                                                      (UINT)hrStatus, errorsU8->GetStringPointer())};
            }
            std::string errorMessage{static_cast<char *>(errors->GetBufferPointer()), errors->GetBufferSize()};
            return util::ErrorMessage{
                fmt::format("Failed to validate shader: error code {:X}, details:\n{}", (UINT)hrStatus, errorMessage)};
        }
        return util::ErrorMessage{
            fmt::format("Failed to validate shader: error code {:X}, no details available", (UINT)hrStatus)};
    }

    // Get validated shader bytecode
    wil::com_ptr_nothrow<IDxcBlob> validatedShaderBlob{};
    hr = result->GetResult(validatedShaderBlob.put());
    if (FAILED(hr)) {
        return util::ErrorMessage{fmt::format("Failed to retrieve validated shader blob: error code {:X}", (UINT)hr)};
    }
    spec.bytecode.resize(validatedShaderBlob->GetBufferSize());
    std::copy_n(static_cast<char *>(validatedShaderBlob->GetBufferPointer()), validatedShaderBlob->GetBufferSize(),
                spec.bytecode.begin());

    return {};
}

template <ShaderStage stage>
static util::VoidResult<> ValidateShaderSPIRV(CompiledShader<stage> &spec) {
#if YMIR_PLATFORM_HAS_VULKAN
    fmt::memory_buffer buf{};
    auto out = std::back_inserter(buf);

    // TODO: allow selecting version
    spvtools::SpirvTools tools{SPV_ENV_VULKAN_1_1};
    tools.SetMessageConsumer(
        [&](spv_message_level_t level, const char *source, const spv_position_t &position, const char *message) {
            fmt::format_to(out, "[{}:{}] {}\n", position.line, position.column, message);
        });
    if (tools.Validate(reinterpret_cast<const uint32 *>(spec.bytecode.data()), spec.bytecode.size() / sizeof(uint32))) {
        return {};
    }

    return util::ErrorMessage{fmt::format("SPIR-V shader validation error:\n{}", fmt::to_string(buf))};
#else
    // Vulkan is required to use SPIR-V shaders
    // Install the SDK: https://vulkan.lunarg.com/sdk/home
    return util::ErrorMessage{"Cannot validate SPIR-V shaders: Ymir was compiled without Vulkan support"};
#endif
}

template <ShaderStage stage>
util::VoidResult<> DoValidateShader(CompiledShader<stage> &spec) {
    if (spec.format == ShaderBytecodeFormat::DXIL) {
        return ValidateShaderDXC<stage>(spec);
    }
    if (spec.format == ShaderBytecodeFormat::SPIRV) {
        return ValidateShaderSPIRV<stage>(spec);
    }

    return util::ErrorMessage{"Unsupported shader bytecode format provided to DXC compiler"};
}

// ---------------------------------------------------------------------------------------------------------------------

util::ValueResult<VertexShader> CompileShader(const VertexShaderCompileSpec &spec) {
    return DoCompileShader(spec);
}

util::ValueResult<PixelShader> CompileShader(const PixelShaderCompileSpec &spec) {
    return DoCompileShader(spec);
}

util::ValueResult<ComputeShader> CompileShader(const ComputeShaderCompileSpec &spec) {
    return DoCompileShader(spec);
}

// -----------------------------------------------------------------------------

util::VoidResult<> ValidateShader(VertexShader &shader) {
    return DoValidateShader(shader);
}

util::VoidResult<> ValidateShader(PixelShader &shader) {
    return DoValidateShader(shader);
}

util::VoidResult<> ValidateShader(ComputeShader &shader) {
    return DoValidateShader(shader);
}

} // namespace ymir::gpu
