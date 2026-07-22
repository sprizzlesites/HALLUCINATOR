#!/usr/bin/env python3
"""Diagnostic: dump what a torchscript RAVE export actually exposes.

Used from CI (see .github/workflows/fetch-vctk-model.yml) to find out
why RaveModel::tryNnTildeInterface() (Source/RaveModel.cpp) failed to
find sample_rate/latent_size/ratio metadata on the real VCTK checkpoint,
since that file can't be pulled into the project's own dev sandbox to
inspect interactively.

Round 1 (dir(m) at the top level) showed:
- No top-level "encode"/"decode"/"get_methods" methods - only "forward".
- Top-level buffers named encode_params/decode_params/forward_params
  (likely [in_ch, in_ratio, out_ch, out_ratio] int tensors - the real
  nn_tilde convention stores these as buffers, not a get_method_params()
  call).
- Nested buffers under "_rave." and "_prior." - this top-level module is
  a "Combined" wrapper around a _rave submodule and a _prior submodule.
  Notably "_rave.latent_size" and "_rave.sampling_rate" exist as buffers.

This round checks whether _rave (and _prior) are themselves proper
submodules with their own callable encode/decode/get_methods, and
prints the actual buffer values so RaveModel.cpp can be fixed to read
the real metadata from wherever it actually lives.
"""
import sys

import torch

path = sys.argv[1]
m = torch.jit.load(path)


def dump_buffer_value(module, name):
    try:
        val = dict(module.named_buffers())[name]
        print(f"  {name} = {val}")
    except Exception as e:
        print(f"  {name} -> FAILED: {e!r}")


print("=== top-level buffer values ===")
for name in ["encode_params", "decode_params", "forward_params"]:
    dump_buffer_value(m, name)

print()
print("=== does m have a submodule named _rave / _prior? ===")
for sub in ["_rave", "_prior"]:
    try:
        sm = getattr(m, sub)
        print(f"{sub}: type = {type(sm)}, original_name = {getattr(sm, 'original_name', '?')}")
        print(f"  dir({sub}), public only:")
        print("   ", [x for x in dir(sm) if not x.startswith("_")])
        for name in ["latent_size", "sampling_rate", "sr", "encode_params", "decode_params"]:
            dump_buffer_value(sm, name)
    except Exception as e:
        print(f"{sub} -> FAILED: {e!r}")
    print()

print("=== m.forward schema ===")
try:
    print(m.forward.schema)
except Exception as e:
    print("forward.schema -> FAILED:", repr(e))

print()
print("=== m.code (decompiled forward source) ===")
try:
    print(m.code)
except Exception as e:
    print("m.code -> FAILED:", repr(e))

print()
print("=== m._rave.code (decompiled, if present) ===")
try:
    print(m._rave.code)
except Exception as e:
    print("m._rave.code -> FAILED:", repr(e))
