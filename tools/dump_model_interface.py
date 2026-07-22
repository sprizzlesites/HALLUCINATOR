#!/usr/bin/env python3
"""Diagnostic: dump what a torchscript RAVE export actually exposes.

Used from CI (see .github/workflows/fetch-vctk-model.yml) to find out
why RaveModel::tryNnTildeInterface() (Source/RaveModel.cpp) failed to
find sample_rate/latent_size/ratio metadata on the real VCTK checkpoint,
since that file can't be pulled into the project's own dev sandbox to
inspect interactively.
"""
import sys

import torch

path = sys.argv[1]
m = torch.jit.load(path)

print("=== dir(m), public only ===")
print([x for x in dir(m) if not x.startswith("_")])

for name in ["get_methods", "get_method_params", "get_sample_rate",
             "get_latent_size", "get_ratio", "get_samplerate"]:
    try:
        attr = getattr(m, name)
        if name == "get_method_params":
            print(name, "('encode') =", attr("encode"))
        elif callable(attr):
            print(name, "() =", attr())
        else:
            print(name, "=", attr)
    except Exception as e:
        print(name, "-> FAILED:", repr(e))

for attrname in ["sr", "sample_rate", "sampling_rate", "ratio", "latent_size"]:
    try:
        print(attrname, "attr =", m._c.getattr(attrname))
    except Exception as e:
        print(attrname, "attr -> FAILED:", repr(e))

print("=== named_buffers ===")
for n, _ in m.named_buffers():
    print(" buffer:", n)
