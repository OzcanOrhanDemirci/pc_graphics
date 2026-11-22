#!/usr/bin/env bash
# Fetches the third-party pieces into third_party/. Re-runnable: skips what is there.
# CUDA comes as NVIDIA's per-component redistributable archives (no installer, no driver,
# no admin rights): only what nvcc, the runtime, NVRTC and NVML need.
# The messages follow the application's language (lang in user.cfg at the repository root, as tools/dil.py reads it),
# or --lang tr|en; with neither, both. -h / --help says what it fetches and downloads nothing.
set -euo pipefail
cd "$(dirname "$0")"
lang="" help=0
while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help) help=1 ;;
    --lang) lang="${2:-}"; shift ;;
    --lang=*) lang="${1#--lang=}" ;;
    *) echo "? $1 (bash third_party/fetch.sh [--lang tr|en] [--help])" >&2; exit 2 ;;
  esac
  shift
done
if [ -z "$lang" ] && [ -f ../user.cfg ]; then
  lang=$(sed -n 's/^lang *= *\([a-z]*\).*/\1/p' ../user.cfg | head -n 1)
fi
case "$lang" in tr|en) ;; *) lang="" ;; esac
say() { case "$lang" in tr) echo "$1" ;; en) echo "$2" ;; *) echo "$2"; echo "$1" ;; esac; }
if [ "$help" = 1 ]; then
  say "bash third_party/fetch.sh   üçüncü taraf parçaları third_party/ içine indirir; yeniden çalıştırılabilir, yerinde olanı atlar.
  CUDA 13.4 (NVIDIA'nın bileşen arşivleri: kurulum yok, sürücüye dokunulmaz, yönetici hakkı gerekmez), OptiX 9.1 başlıkları,
  DLSS SDK, GLFW 3.4, stb, tinyexr, cgltf, OpenGL başlıkları. Git Bash, curl, git ve python gerekir.
  --lang tr|en   iletilerin dili (öntanımlı: uygulamanınki, user.cfg'den)" \
      "bash third_party/fetch.sh   fetches the third-party pieces into third_party/; re-runnable, skips what is there.
  CUDA 13.4 (NVIDIA's per-component archives: no installer, the driver untouched, no admin rights), OptiX 9.1 headers,
  the DLSS SDK, GLFW 3.4, stb, tinyexr, cgltf, OpenGL headers. Needs Git Bash, curl, git and python.
  --lang tr|en   the language of the messages (default: the application's, from user.cfg)"
  exit 0
fi
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
[ -f stb/stb_easy_font.h ] || curl -sSL -o stb/stb_easy_font.h https://raw.githubusercontent.com/nothings/stb/master/stb_easy_font.h
[ -f stb/stb_image.h ] || curl -sSL -o stb/stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
[ -f stb/stb_truetype.h ] || curl -sSL -o stb/stb_truetype.h https://raw.githubusercontent.com/nothings/stb/master/stb_truetype.h
[ -f tinyexr/tinyexr.h ] || curl -sSL -o tinyexr/tinyexr.h https://raw.githubusercontent.com/syoyo/tinyexr/release/tinyexr.h
for f in exr_reader.hh streamreader.hh; do [ -f tinyexr/$f ] || curl -sSL -o tinyexr/$f https://raw.githubusercontent.com/syoyo/tinyexr/release/$f; done
[ -f tinyexr/miniz.h ] || { curl -sSL -o tinyexr/miniz.c https://raw.githubusercontent.com/syoyo/tinyexr/release/deps/miniz/miniz.c; curl -sSL -o tinyexr/miniz.h https://raw.githubusercontent.com/syoyo/tinyexr/release/deps/miniz/miniz.h; }
# glTF 2.0 reader for scene 11's open-licence models (MIT)
mkdir -p cgltf
[ -f cgltf/cgltf.h ] || curl -sSL -o cgltf/cgltf.h https://raw.githubusercontent.com/jkuhlmann/cgltf/v1.15/cgltf.h
# NVIDIA's DLSS SDK (NGX; NVIDIA RTX SDK licence, see dlss/LICENSE.txt): scene 11's Ray Reconstruction through NGX's
# CUDA path. Only the headers, the loader library and the release runtime DLLs
if [ ! -d dlss ]; then
  git clone -q --depth 1 --branch v310.9.1 --filter=blob:none --sparse https://github.com/NVIDIA/DLSS.git dlss
  git -C dlss sparse-checkout set include lib/Windows_x86_64/x64 lib/Windows_x86_64/rel
  echo "dlss: v310.9.1"
fi
# OpenGL 4.6 enums and function types for scene 8's raster path (Khronos registry)
mkdir -p gl/GL gl/KHR
[ -f gl/GL/glext.h ] || curl -sSL -o gl/GL/glext.h https://registry.khronos.org/OpenGL/api/GL/glext.h
[ -f gl/KHR/khrplatform.h ] || curl -sSL -o gl/KHR/khrplatform.h https://registry.khronos.org/EGL/api/KHR/khrplatform.h
say "tamam: third_party/ hazır" "done: third_party/ is ready"
