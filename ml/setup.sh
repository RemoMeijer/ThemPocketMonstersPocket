#!/usr/bin/env bash
# One-time setup for the ML steps (AI_PLAN.md step 3 on): a project .venv with PyTorch, numpy and pybind11, then the
# Python module build and a GPU check. Run from the project folder: ml/setup.sh
#
# The GTX 1050 and GTX 1080 are Pascal cards (compute capability 6.1). Newer PyTorch CUDA builds may leave Pascal out,
# so this uses the CUDA 12.6 build and checks for sm_61. If the check fails, try an older build, for example:
#   .venv/bin/pip install "torch==2.7.1" --index-url https://download.pytorch.org/whl/cu126
#   .venv/bin/pip install "torch==2.5.1" --index-url https://download.pytorch.org/whl/cu118
set -euo pipefail
cd "$(dirname "$0")/.."

[ -d .venv ] || python3 -m venv .venv
.venv/bin/pip install --upgrade pip
.venv/bin/pip install torch --index-url https://download.pytorch.org/whl/cu126
.venv/bin/pip install numpy scipy pybind11

# The engine with the Python module (built only when CMake finds pybind11).
cmake -S engine -B engine/build -Dpybind11_DIR="$(.venv/bin/python -m pybind11 --cmakedir)"
cmake --build engine/build -j

.venv/bin/python ml/check_gpu.py
