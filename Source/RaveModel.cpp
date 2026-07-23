#include "RaveModel.h"
#include <ATen/Parallel.h>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <vector>

bool RaveModel::load(const juce::File& modelFile, juce::String& errorMessage)
{
    loaded = false;

    if (! modelFile.existsAsFile())
    {
        errorMessage = "Model file does not exist: " + modelFile.getFullPathName();
        return false;
    }

    try
    {
        module = torch::jit::load(modelFile.getFullPathName().toStdString());
    }
    catch (const std::exception& e)
    {
        errorMessage = "Failed to load torchscript model: " + juce::String(e.what());
        return false;
    }

    // libtorch's CPU backend defaults to multi-threaded intra-op
    // parallelism, which is not bit-reproducible run to run (thread
    // scheduling affects floating-point reduction order in conv/matmul
    // kernels) - invisible with the tiny dummy test model (too small to
    // trigger multi-threaded codepaths) but real on the actual VCTK
    // checkpoint, where it was the remaining cause of Freeze Seed
    // reproducibility failing even after fixing cached-conv state
    // persistence above. Forcing single-threaded execution trades some
    // throughput for the exact reproducibility Freeze Seed promises - RAVE
    // models are specifically designed to run in real time on CPU even
    // single-threaded, so this is a reasonable default rather than a
    // reckless one.
    at::set_num_threads(1);

    module.eval();
    encodeDecodeModule = module;

    const bool haveMetadata = tryLoadSidecarJson(modelFile)
                               || tryPurposeBuiltInterface()
                               || tryNnTildeInterface()
                               || tryCombinedWrapperInterface();

    if (! haveMetadata)
    {
        errorMessage = "Loaded the model but could not determine its sample rate / "
                       "latent size / ratio. Add a \"" + modelFile.getFileNameWithoutExtension()
                       + ".rave.json\" file next to the model with "
                         "{\"sample_rate\": N, \"latent_size\": N, \"ratio\": N}. "
                         "See Resources/MODEL_INTERFACE.md.";
        return false;
    }

    if (latentSize <= 0 || ratio <= 0 || sampleRate <= 0)
    {
        errorMessage = "Model metadata is invalid (sampleRate=" + juce::String(sampleRate)
                       + ", ratio=" + juce::String(ratio)
                       + ", latentSize=" + juce::String(latentSize) + ").";
        return false;
    }

    // Sanity check: run one real encode/decode round trip so failures surface
    // at load time (in the editor's error box) rather than as a click on the
    // very first audio block.
    try
    {
        torch::NoGradGuard noGrad;
        auto silence = torch::zeros({ 1, 1, ratio });
        auto z = encode(silence);
        auto y = decode(z);

        if (y.dim() != 3 || y.size(2) != ratio)
        {
            errorMessage = "Model decode() did not return the expected [1,1,ratio] shape.";
            return false;
        }
    }
    catch (const std::exception& e)
    {
        errorMessage = "Model loaded but a test encode/decode call failed: " + juce::String(e.what());
        return false;
    }

    loaded = true;
    return true;
}

bool RaveModel::tryLoadSidecarJson(const juce::File& modelFile)
{
    auto jsonFile = modelFile.getParentDirectory()
                        .getChildFile(modelFile.getFileNameWithoutExtension() + ".rave.json");

    if (! jsonFile.existsAsFile())
        return false;

    auto parsed = juce::JSON::parse(jsonFile);
    if (! parsed.isObject())
        return false;

    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr)
        return false;

    bool anyFound = false;

    if (obj->hasProperty("sample_rate")) { sampleRate = (int) obj->getProperty("sample_rate"); anyFound = true; }
    if (obj->hasProperty("latent_size")) { latentSize = (int) obj->getProperty("latent_size"); anyFound = true; }
    if (obj->hasProperty("ratio"))       { ratio      = (int) obj->getProperty("ratio");       anyFound = true; }

    return anyFound;
}

bool RaveModel::tryPurposeBuiltInterface()
{
    try
    {
        auto sr = module.get_method("get_sample_rate")({});
        auto ls = module.get_method("get_latent_size")({});
        auto rt = module.get_method("get_ratio")({});

        sampleRate = (int) sr.toInt();
        latentSize = (int) ls.toInt();
        ratio      = (int) rt.toInt();
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

bool RaveModel::tryNnTildeInterface()
{
    try
    {
        auto methodsIValue = module.get_method("get_methods")({});
        auto methodsList = methodsIValue.toList();

        bool hasEncode = false;
        for (const auto& m : methodsList)
        {
            if (m.get().toStringRef() == "encode")
            {
                hasEncode = true;
                break;
            }
        }

        if (! hasEncode)
            return false;

        auto paramsIValue = module.get_method("get_method_params")({ std::string("encode") });
        auto params = paramsIValue.toIntList();

        if (params.size() < 4)
            return false;

        // [in_channels, in_ratio, out_channels, out_ratio]
        latentSize = (int) params[2];
        ratio      = (int) params[3];

        // Best-effort sample rate lookup; fall back to the plugin's own
        // default (documented) if the model exposes neither convention.
        try
        {
            auto srIValue = module.get_method("get_samplerate")({});
            sampleRate = (int) srIValue.toInt();
        }
        catch (const std::exception&)
        {
            try
            {
                auto attr = module.attr("sr");
                sampleRate = (int) attr.toInt();
            }
            catch (const std::exception&)
            {
                // Leave sampleRate at its current default; the caller can
                // still override via the JSON sidecar.
            }
        }

        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

bool RaveModel::tryCombinedWrapperInterface()
{
    try
    {
        // See tier 4 in this class's doc comment: the real VCTK checkpoint
        // is a "Combined" wrapper whose own forward() just delegates to a
        // "_rave" submodule - encode()/decode() only exist there.
        auto raveSubmodule = module.attr("_rave").toModule();

        auto readIntTensorElement = [](torch::jit::script::Module& m, const char* name, int index) -> int
        {
            auto tensor = m.attr(name).toTensor();
            return (int) tensor[index].item<int64_t>();
        };

        // [in_channels, in_ratio, out_channels, out_ratio]
        int encodeOutChannels = readIntTensorElement(raveSubmodule, "encode_params", 2);
        int encodeOutRatio    = readIntTensorElement(raveSubmodule, "encode_params", 3);

        latentSize = encodeOutChannels;
        ratio = encodeOutRatio;

        // Best-effort sample rate: stored as a buffer/attribute on the
        // submodule, not the wrapper. Different exports have been observed
        // to expose this as either a plain scripted int or a 0-dim tensor,
        // so handle both rather than assuming one.
        try
        {
            auto ivalue = raveSubmodule.attr("sampling_rate");
            sampleRate = ivalue.isInt() ? (int) ivalue.toInt() : (int) ivalue.toTensor().item<int64_t>();
        }
        catch (const std::exception&)
        {
            // Leave sampleRate at its current default; the caller can
            // still override via the JSON sidecar.
        }

        encodeDecodeModule = raveSubmodule;

        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

torch::Tensor RaveModel::encode(const torch::Tensor& audio)
{
    torch::NoGradGuard noGrad;
    auto result = encodeDecodeModule.get_method("encode")({ audio });
    return result.toTensor();
}

torch::Tensor RaveModel::decode(const torch::Tensor& latent)
{
    torch::NoGradGuard noGrad;
    auto result = encodeDecodeModule.get_method("decode")({ latent }).toTensor();

    if (result.dim() == 3 && result.size(1) > 1)
        result = result.narrow(1, 0, 1);

    return result;
}

int RaveModel::measureInternalLatency()
{
    if (! loaded)
        return 0;

    try
    {
        torch::NoGradGuard noGrad;

        const int warmupFrames = 6;   // let the silence floor settle
        const int activeFrames  = 30; // loud region; covers latency up to ~30*ratio
        const int totalFrames   = warmupFrames + activeFrames;

        std::vector<float> out;
        out.reserve((size_t) totalFrames * (size_t) ratio);

        // Deterministic loud broadband test signal (a tiny LCG), so the
        // measurement is repeatable and survives whatever the model does to
        // it far better than a pure tone would.
        uint32_t rng = 0x1234567u;
        auto nextNoise = [&rng]() -> float
        {
            rng = rng * 1664525u + 1013904223u;
            return ((float) (rng >> 8) / (float) (1u << 24)) * 2.0f - 1.0f; // [-1,1)
        };

        for (int f = 0; f < totalFrames; ++f)
        {
            auto x = torch::zeros({ 1, 1, ratio });
            if (f >= warmupFrames)
            {
                auto* xp = x.data_ptr<float>();
                for (int i = 0; i < ratio; ++i)
                    xp[i] = 0.3f * nextNoise();
            }

            auto z = encode(x);
            auto y = decode(z).contiguous();
            const auto* yp = y.data_ptr<float>();
            out.insert(out.end(), yp, yp + ratio);
        }

        return juce::jlimit(0, activeFrames * ratio, detectResponseOnset(out, warmupFrames * ratio));
    }
    catch (const std::exception&)
    {
        return 0;
    }
}

int RaveModel::detectResponseOnset(const std::vector<float>& out, int inputStepSample)
{
    if (inputStepSample <= 0 || inputStepSample >= (int) out.size())
        return 0;

    // Silence floor from the warmup (pre-step) region's output.
    float floorMag = 1.0e-6f;
    for (int i = 0; i < inputStepSample; ++i)
        floorMag = std::max(floorMag, std::abs(out[(size_t) i]));

    const float threshold = std::max(floorMag * 4.0f, 0.02f);

    // First output sample (at/after the input step) whose short trailing RMS
    // exceeds the threshold => the input step's influence has arrived.
    const int rmsWin = 64;
    double acc = 0.0;
    int onset = -1;
    for (int i = 0; i < (int) out.size(); ++i)
    {
        const float s = out[(size_t) i];
        acc += (double) s * s;
        if (i >= rmsWin)
        {
            const float old = out[(size_t) (i - rmsWin)];
            acc -= (double) old * old;
        }
        if (i >= inputStepSample && i >= rmsWin)
        {
            const float rms = (float) std::sqrt(acc / rmsWin);
            if (rms > threshold)
            {
                onset = i - rmsWin / 2; // approx the leading edge, not the window centre
                break;
            }
        }
    }

    if (onset < inputStepSample)
        return 0; // couldn't determine - safe fallback

    return onset - inputStepSample;
}
