// Generates Resources/dummy_rave_model.ts: a tiny torchscript module that
// implements HALLUCINATOR's expected model interface (get_sample_rate,
// get_latent_size, get_ratio, encode, decode) with a trivial, deterministic
// "compression" - it is NOT a trained RAVE checkpoint and will not sound
// like one. It exists purely so the plugin's buffering/latency/crossfade/
// latent-manipulation pipeline can be exercised end-to-end without network
// access to a real pretrained model (see Resources/MODEL_INTERFACE.md).
//
// Built via the main CMakeLists (HALLUCINATOR_BUILD_TOOLS option). Run it
// once from the build directory; it writes the .ts file directly into
// Resources/ at the repo root.
#include <torch/script.h>
#include <iostream>

int main(int argc, char** argv)
{
    torch::NoGradGuard noGrad;

    const std::string outputPath = argc > 1 ? argv[1] : "Resources/dummy_rave_model.ts";

    torch::jit::Module m("DummyRAVE");

    // ratio (2048) must equal latentSize (8) * chunk (256) for the
    // reshape-based encode/decode below. decode applies a tanh saturation
    // so that encode(decode(z)) != z exactly - without some nonlinearity,
    // this mean-pool/repeat-expand round trip is an exact fixed point,
    // which would make Feedback Iterations a silent no-op in testing.
    m.define(R"JIT(
def get_sample_rate(self) -> int:
    return 44100

def get_latent_size(self) -> int:
    return 8

def get_ratio(self) -> int:
    return 2048

def encode(self, x):
    b = x.shape[0]
    chunks = x.reshape([b, 8, 256])
    return chunks.mean(dim=2, keepdim=True)

def decode(self, z):
    b = z.shape[0]
    y = z.expand([b, 8, 256])
    y = y.reshape([b, 1, 2048])
    return torch.tanh(y * 1.3)
)JIT");

    m.save(outputPath);
    std::cout << "Wrote dummy RAVE-interface model to " << outputPath << std::endl;
    return 0;
}
