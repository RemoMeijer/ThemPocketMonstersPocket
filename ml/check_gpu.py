"""Checks that PyTorch sees the GPU and supports it (Pascal = sm_61), times one matrix multiply on CPU and GPU, and
checks that the engine's Python module loads. Run: .venv/bin/python ml/check_gpu.py"""
import sys
import time
from pathlib import Path

import torch

print(f"torch {torch.__version__}, CUDA build {torch.version.cuda}, CUDA available: {torch.cuda.is_available()}")
if torch.cuda.is_available():
    name = torch.cuda.get_device_name(0)
    major, minor = torch.cuda.get_device_capability(0)
    arch = f"sm_{major}{minor}"
    print(f"GPU: {name}, {arch}, {torch.cuda.get_device_properties(0).total_memory / 2**30:.1f} GB")
    if arch not in torch.cuda.get_arch_list():
        sys.exit(f"this PyTorch build doesn't include {arch} ({torch.cuda.get_arch_list()}): see ml/setup.sh for older builds")
    for device in ("cpu", "cuda"):
        a = torch.randn(2048, 2048, device=device)
        (a @ a).sum().item()  # warm-up
        start = time.time()
        for _ in range(10):
            (a @ a).sum().item()
        print(f"{device}: 10 matmuls of 2048x2048 in {time.time() - start:.3f} s")
else:
    print("no GPU: everything still runs on the CPU, just slower")

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "engine" / "build"))
try:
    import tcgp
    print(f"engine module tcgp loaded, encoding version {tcgp.ENCODING_VERSION}")
except ImportError as e:
    sys.exit(f"engine module tcgp not built ({e}): was pybind11 found by CMake?")
