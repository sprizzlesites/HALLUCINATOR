#include "RaveModel.h"

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

    module.eval();

    const bool haveMetadata = tryLoadSidecarJson(modelFile)
                               || tryPurposeBuiltInterface()
                               || tryNnTildeInterface();

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

torch::Tensor RaveModel::encode(const torch::Tensor& audio)
{
    torch::NoGradGuard noGrad;
    auto result = module.get_method("encode")({ audio });
    return result.toTensor();
}

torch::Tensor RaveModel::decode(const torch::Tensor& latent)
{
    torch::NoGradGuard noGrad;
    auto result = module.get_method("decode")({ latent });
    return result.toTensor();
}
