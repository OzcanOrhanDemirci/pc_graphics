#!/usr/bin/env bash
# Fetches the third-party pieces into third_party/. Re-runnable: skips what is there.
# CUDA comes as NVIDIA's per-component redistributable archives (no installer, no driver,
# no admin rights): only what nvcc, the runtime, NVRTC and NVML need.
set -euo pipefail
cd "$(dirname "$0")"
CUDA_VER=13.4.2
BASE=https://developer.download.nvidia.com/compute/cuda/redist
mkdir -p _dl cuda
curl -sSL -o _dl/redist.json "$BASE/redistrib_${CUDA_VER}.json"
for c in cuda_nvcc libnvvm cuda_crt cuda_cudart cccl cuda_nvrtc cuda_nvml_dev cuda_nvtx; do
  rel=$(python -c "import json;print(json.load(open('_dl/redist.json'))['$c']['windows-x86_64']['relative_path'])")
  zip="_dl/$(basename "$rel")"
  [ -f "$zip" ] || curl -sSL -o "$zip" "$BASE/$rel"
  python - "$zip" <<'PY'
import sys, zipfile, pathlib, shutil
z = zipfile.ZipFile(sys.argv[1])
dest = pathlib.Path("cuda")
for m in z.infolist():
    parts = pathlib.PurePosixPath(m.filename).parts
    if len(parts) < 2 or m.is_dir():
        continue
    out = dest.joinpath(*parts[1:])          # drop the archive's root folder: merge into one tree
    out.parent.mkdir(parents=True, exist_ok=True)
    with z.open(m) as src, open(out, "wb") as dst:
        shutil.copyfileobj(src, dst)
PY
  echo "cuda: $c"
done
[ -d optix ] || git clone -q --depth 1 --branch v9.1.0 https://github.com/NVIDIA/optix-dev.git optix && echo "optix: v9.1.0"
if [ ! -d glfw ]; then
  curl -sSL -o _dl/glfw.zip https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.bin.WIN64.zip
  python -c "import zipfile;zipfile.ZipFile('_dl/glfw.zip').extractall('_dl')" && mv _dl/glfw-3.4.bin.WIN64 glfw && echo "glfw: 3.4"
fi
mkdir -p stb tinyexr
[ -f stb/stb_image_write.h ] || curl -sSL -o stb/stb_image_write.h https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h
[ -f stb/stb_image.h ] || curl -sSL -o stb/stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
[ -f tinyexr/tinyexr.h ] || curl -sSL -o tinyexr/tinyexr.h https://raw.githubusercontent.com/syoyo/tinyexr/release/tinyexr.h
[ -f tinyexr/miniz.h ] || { curl -sSL -o tinyexr/miniz.c https://raw.githubusercontent.com/syoyo/tinyexr/release/deps/miniz/miniz.c; curl -sSL -o tinyexr/miniz.h https://raw.githubusercontent.com/syoyo/tinyexr/release/deps/miniz/miniz.h; }
echo "done"
