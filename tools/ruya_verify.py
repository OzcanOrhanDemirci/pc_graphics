"""Measure Rüya headlessly and retain independent evidence.

Usage: python tools/ruya_verify.py --mode benchmark --frames 300 --repeats 3
Exit codes: 0 all requested checks passed; 1 failed check/runtime; 2 usage.
"""
# /// script
# requires-python = ">=3.10"
# dependencies = ["numpy", "pillow", "OpenEXR"]
# ///
import argparse
import csv
import hashlib
import json
import math
import shutil
import subprocess
import sys
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REQUIRED_COLUMNS = (
    "view", "frame", "gpu_trace_ms", "gpu_denoise_ms", "gpu_display_ms",
    "upload_ms", "frame_ms", "cells", "triangles", "resident_bytes",
    "free_bytes", "bad_samples",
)
CAMERA_COLUMNS = ("x", "y", "z", "yaw", "pitch", "width", "height", "spp", "bounces")


class ToolError(Exception):
    """Report a failed verification or an unavailable measurement."""


@dataclass(frozen=True, slots=True)
class TimingSummary:
    frames: int
    frame_p50_ms: float
    frame_p95_ms: float
    frame_p99_ms: float
    frame_max_ms: float
    trace_mean_ms: float
    denoise_mean_ms: float
    display_mean_ms: float
    upload_max_ms: float
    peak_resident_bytes: int
    minimum_free_bytes: int
    maximum_cells: int
    maximum_triangles: int
    bad_samples: int
    passes_timing: bool


def percentile(values: list[float], fraction: float) -> float:
    """Interpolate a percentile over all measured frames."""
    if not values:
        raise ToolError("Yüzdelik için ölçülmüş kare yok")
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    low = math.floor(position)
    high = math.ceil(position)
    return ordered[low] + (ordered[high] - ordered[low]) * (position - low)


def summarize(rows: list[dict[str, float]], p50_limit: float, p95_limit: float) -> TimingSummary:
    """Reject invalid telemetry and compute thresholds independently of the renderer."""
    if not rows:
        raise ToolError("CSV içinde ölçülmüş kare yok")
    for index, row in enumerate(rows):
        for name in REQUIRED_COLUMNS:
            if name not in row or not math.isfinite(row[name]) or row[name] < 0:
                raise ToolError(f"CSV kare {index}: {name} eksik veya geçersiz")
        if row["frame_ms"] <= 0 or row["gpu_trace_ms"] <= 0:
            raise ToolError(f"CSV kare {index}: sıfır süre ölçüm değildir")
        if row["cells"] == 0 or row["triangles"] == 0 or row["resident_bytes"] == 0:
            raise ToolError(f"CSV kare {index}: boş dünya başarı sayılmaz")
        for name in ("view", "frame", "cells", "triangles", "resident_bytes", "free_bytes", "bad_samples"):
            if row[name] != int(row[name]):
                raise ToolError(f"CSV kare {index}: {name} tamsayı olmalı")
    frame_times = [row["frame_ms"] for row in rows]
    p50 = percentile(frame_times, 0.50)
    p95 = percentile(frame_times, 0.95)
    count = len(rows)
    bad = int(sum(row["bad_samples"] for row in rows))
    return TimingSummary(
        count, p50, p95, percentile(frame_times, 0.99), max(frame_times),
        sum(row["gpu_trace_ms"] for row in rows) / count,
        sum(row["gpu_denoise_ms"] for row in rows) / count,
        sum(row["gpu_display_ms"] for row in rows) / count,
        max(row["upload_ms"] for row in rows),
        int(max(row["resident_bytes"] for row in rows)),
        int(min(row["free_bytes"] for row in rows)),
        int(max(row["cells"] for row in rows)),
        int(max(row["triangles"] for row in rows)),
        bad, p50 <= p50_limit and p95 <= p95_limit and bad == 0,
    )


def read_rows(path: Path, expected_frames: int | None = None) -> list[dict[str, float]]:
    """Read complete CSV records, rejecting malformed or truncated output."""
    try:
        with path.open(encoding="utf-8-sig", newline="") as source:
            reader = csv.DictReader(source)
            if not reader.fieldnames or not set(REQUIRED_COLUMNS).issubset(reader.fieldnames):
                raise ToolError(f"{path}: beklenen CSV sütunları yok")
            rows = []
            for row in reader:
                parsed = {name: float(row[name]) for name in REQUIRED_COLUMNS}
                parsed.update({name: float(row[name]) for name in CAMERA_COLUMNS if name in row})
                rows.append(parsed)
    except (OSError, ValueError, TypeError, KeyError) as exc:
        raise ToolError(f"{path}: CSV okunamadı: {exc}") from exc
    if expected_frames is not None and len(rows) != expected_frames:
        raise ToolError(f"{path}: {expected_frames} kare beklendi, {len(rows)} geldi")
    if len({(row["view"], row["frame"]) for row in rows}) != len(rows):
        raise ToolError(f"{path}: tekrar eden kare kimliği")
    return rows


def file_hash(path: Path) -> str:
    """Hash file bytes without loading large executables into memory."""
    digest = hashlib.sha256()
    try:
        with path.open("rb") as source:
            for block in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(block)
    except OSError as exc:
        raise ToolError(f"{path}: karma alınamadı: {exc}") from exc
    return digest.hexdigest()


def save_json(path: Path, data: object) -> None:
    """Retain a complete structured result next to raw evidence."""
    try:
        path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    except OSError as exc:
        raise ToolError(f"{path}: kayıt yazılamadı: {exc}") from exc


def command(arguments: list[str], output: Path, timeout: float, records: list[dict]) -> str:
    """Run a bounded child, retaining its exact command and output on failure."""
    record = {"args": arguments, "cwd": str(ROOT), "timeout_seconds": timeout}
    records.append(record)
    try:
        completed = subprocess.run(arguments, cwd=ROOT, capture_output=True,
                                   encoding="utf-8", errors="replace", timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        record["timed_out"] = True
        raw = (exc.stdout or b"") + (exc.stderr or b"")
        if isinstance(raw, bytes):
            raw = raw.decode("utf-8", errors="replace")
        output.write_text(raw, encoding="utf-8", newline="\n")
        raise ToolError(f"Komut süre sınırını aştı; kayıt: {output}") from exc
    except OSError as exc:
        raise ToolError(f"Komut başlatılamadı: {arguments[0]}: {exc}") from exc
    record["returncode"] = completed.returncode
    output.write_text(completed.stdout + completed.stderr, encoding="utf-8", newline="\n")
    if completed.returncode != 0:
        raise ToolError(f"Komut {completed.returncode} koduyla başarısız; kayıt: {output}")
    return completed.stdout


def check_snapshot(prefix: Path, width: int, height: int) -> dict:
    """Verify actual saved pixels and reject black or non-finite results."""
    try:
        import numpy as np
        from PIL import Image
    except ImportError as exc:
        raise ToolError("Görüntü ölçümü için numpy ve pillow gerekli") from exc
    png = prefix.with_suffix(".png")
    exr = prefix.with_suffix(".exr")
    try:
        with Image.open(png) as picture:
            if picture.size != (width, height):
                raise ToolError(f"{png}: boyut {picture.size}; beklenen {(width, height)}")
            pixels = np.asarray(picture.convert("RGB"), dtype=np.float32) / 255.0
    except OSError as exc:
        raise ToolError(f"{png}: kare okunamadı: {exc}") from exc
    luminance = pixels @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    visible = float(np.mean(luminance > 0.02))
    if not np.isfinite(pixels).all() or visible < 0.01 or float(pixels.std()) < 0.001:
        raise ToolError(f"{png}: boş, tek renk veya geçersiz görüntü")
    if not exr.is_file() or exr.stat().st_size < 128:
        raise ToolError(f"{exr}: HDR kanıtı eksik")
    return {"png": str(png), "png_sha256": file_hash(png), "exr": str(exr),
            "exr_sha256": file_hash(exr), "width": width, "height": height,
            "visible_fraction": visible, "mean_luminance": float(luminance.mean())}


def compare_images(test: Path, reference: Path) -> dict:
    """Compare paired saved pixels; FLIP is optional and explicitly identified."""
    import numpy as np
    from PIL import Image
    try:
        with Image.open(test) as picture:
            tested = np.asarray(picture.convert("RGB"), dtype=np.float32) / 255.0
        with Image.open(reference) as picture:
            target = np.asarray(picture.convert("RGB"), dtype=np.float32) / 255.0
    except OSError as exc:
        raise ToolError(f"Görüntü çifti okunamadı: {exc}") from exc
    if tested.shape != target.shape:
        raise ToolError("Karşılaştırılan görüntülerin boyutları eşit değil")
    difference = np.abs(tested - target)
    result = {"mae_srgb": float(difference.mean()), "p99_absolute_srgb": float(np.quantile(difference, 0.99)),
              "rmse_srgb": float(np.sqrt(np.mean((tested - target) ** 2))), "flip": None}
    try:
        import flip_evaluator
    except ImportError:
        result["flip_unavailable_reason"] = "flip_evaluator kurulu değil; FLIP doğrulanmadı"
    else:
        _, mean, parameters = flip_evaluator.evaluate(target, tested, "LDR")
        result["flip"] = float(mean)
        result["flip_parameters"] = {str(key): str(value) for key, value in parameters.items()}
    return result


def compare_linear(test: Path, reference: Path) -> float:
    """Compare raw radiance before denoising, tone mapping or quantization."""
    import numpy as np
    try:
        import OpenEXR
    except ImportError as exc:
        raise ToolError("Doğrusal karşılaştırma için OpenEXR Python modülü gerekli") from exc

    def read(path: Path) -> object:
        try:
            with OpenEXR.File(str(path)) as picture:
                channels = picture.channels()
                name = "RGBA" if "RGBA" in channels else "RGB"
                return np.array(channels[name].pixels[..., :3], dtype=np.float64)
        except (OSError, RuntimeError, KeyError) as exc:
            raise ToolError(f"EXR okunamadı: {path}: {exc}") from exc

    tested = read(test)
    target = read(reference)
    if tested.shape != target.shape or not np.isfinite(tested).all() or not np.isfinite(target).all():
        raise ToolError("Doğrusal görüntü çifti geçersiz")
    energy = np.mean(target ** 2)
    if energy <= 0:
        raise ToolError("Siyah referans doğruluk kanıtı değildir")
    return float(np.sqrt(np.mean((tested - target) ** 2) / energy))


def verify_transport(base: list[str], destination: Path, options: argparse.Namespace, records: list[dict]) -> list[dict]:
    """Test masked primary rays against all instances and disable offscreen lighting as a control."""
    results = []
    for view in options.views:
        common = base + ["--raw", "--view", str(view), "--width", "640", "--height", "360",
                         "--warmup", "0", "--frames", "1", "--spp", "32", "--bounces", str(options.bounces)]
        variants = {"masked": [], "full": ["--full-world"], "no_offscreen": ["--no-offscreen"]}
        if view == options.views[0]:
            variants["repeat"] = []
        prefixes = {name: destination / f"transport_v{view}_{name}" for name in variants}
        for name, flags in variants.items():
            prefix = prefixes[name]
            command(common + flags + ["--out", str(prefix), "--csv", str(prefix.with_suffix(".csv"))],
                    prefix.with_suffix(".txt"), options.timeout, records)
            check_snapshot(prefix, 640, 360)
        target = prefixes["full"].with_suffix(".exr")
        masked = prefixes["masked"].with_suffix(".exr")
        error = compare_linear(masked, target)
        negative = compare_linear(prefixes["no_offscreen"].with_suffix(".exr"), target)
        deterministic = "repeat" not in variants or file_hash(masked) == file_hash(prefixes["repeat"].with_suffix(".exr"))
        results.append({"view": view, "normalized_hdr_rmse": error, "offscreen_disabled_error": negative,
                        "independent_process_exr_hash_equal": deterministic, "passes": error <= 0.001 and deterministic})
    if not any(item["offscreen_disabled_error"] > 0.001 for item in results):
        raise ToolError("Görüş dışı ışık kapatılınca test değişmedi")
    return results


def temporal_metrics(live: object, reference: object) -> dict:
    """Measure excess motion and second differences against the same camera path."""
    import numpy as np
    tested = np.asarray(live, dtype=np.float32)
    target = np.asarray(reference, dtype=np.float32)
    if tested.shape != target.shape or tested.ndim != 4 or tested.shape[0] < 3 or tested.shape[-1] != 3:
        raise ToolError("Zamansal çiftler aynı boyutta en az üç RGB kare olmalı")
    if not np.isfinite(tested).all() or not np.isfinite(target).all():
        raise ToolError("Zamansal görüntülerde sonlu olmayan değer")
    weights = np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    live_luma = tested @ weights
    reference_luma = target @ weights
    live_delta = np.diff(live_luma, axis=0)
    reference_delta = np.diff(reference_luma, axis=0)
    excess = np.abs(live_delta - reference_delta)
    height = live_luma.shape[1] // 8 * 8
    width = live_luma.shape[2] // 8 * 8
    if height == 0 or width == 0:
        raise ToolError("Zamansal görüntü 8x8 blok için küçük")
    blocked_excess = (live_delta - reference_delta)[:, :height, :width]
    blocked_excess = blocked_excess.reshape((-1, height // 8, 8, width // 8, 8)).mean(axis=(2, 4))
    second_live = np.abs(live_luma[1:-1] - 0.5 * (live_luma[:-2] + live_luma[2:])).mean()
    second_reference = np.abs(reference_luma[1:-1] - 0.5 * (reference_luma[:-2] + reference_luma[2:])).mean()
    movement = float(np.abs(reference_delta).mean())
    return {"frames": int(tested.shape[0]), "excess_pixel_mean": float(excess.mean()),
            "excess_pixel_p99": float(np.quantile(excess, 0.99)),
            "excess_block_mean": float(np.abs(blocked_excess).mean()),
            "reference_motion_mean": movement, "live_second_difference": float(second_live),
            "reference_second_difference": float(second_reference),
            "second_difference_ratio": float(second_live / second_reference) if second_reference > 1e-9 else None,
            "reference_moves": movement > 0.0001}


def analyze_sequence(live_directory: Path, reference_directory: Path, expected: int) -> dict:
    """Load complete route frame pairs without substituting missing images."""
    import numpy as np
    from PIL import Image
    sequences = []
    for directory in (live_directory, reference_directory):
        paths = sorted(directory.glob("frame_*.png"))
        if len(paths) != expected:
            raise ToolError(f"{directory}: {expected} kare bekleniyordu, {len(paths)} var")
        pictures = []
        for index, path in enumerate(paths):
            if path.stem != f"frame_{index:04d}":
                raise ToolError(f"{directory}: sıra dışı/eksik kare {path.name}")
            try:
                with Image.open(path) as picture:
                    pictures.append(np.asarray(picture.convert("RGB"), dtype=np.float32) / 255.0)
            except OSError as exc:
                raise ToolError(f"{path}: kare okunamadı: {exc}") from exc
        sequences.append(np.stack(pictures))
    return temporal_metrics(sequences[0], sequences[1])


def self_test() -> dict:
    """Prove the verifier rejects known bad measurements without starting the GPU."""
    baseline = {name: 1.0 for name in REQUIRED_COLUMNS}
    baseline.update({"bad_samples": 0.0, "frame_ms": 30.0, "gpu_trace_ms": 20.0})
    rows = [dict(baseline, frame=float(index)) for index in range(20)]
    if not summarize(rows, 33.33, 40).passes_timing:
        raise ToolError("Ölçüm aracı bilinen iyi veriyi reddetti")
    failures_detected = 0
    for field, value in (("frame_ms", math.nan), ("gpu_trace_ms", 0), ("cells", 0), ("resident_bytes", -1)):
        altered = [dict(row) for row in rows]
        altered[0][field] = value
        try:
            summarize(altered, 33.33, 40)
        except ToolError:
            failures_detected += 1
    slow = [dict(row, frame_ms=50.0) for row in rows]
    noisy = [dict(row, bad_samples=1.0) for row in rows]
    failures_detected += int(not summarize(slow, 33.33, 40).passes_timing)
    failures_detected += int(not summarize(noisy, 33.33, 40).passes_timing)
    if failures_detected != 6 or abs(percentile([10, 20, 30], 0.95) - 29.0) > 1e-9:
        raise ToolError("Ölçüm aracının negatif kontrolleri başarısız")
    try:
        import numpy as np
    except ImportError as exc:
        raise ToolError("Zamansal ölçüm ve negatif kontrol için numpy gerekli") from exc
    sequence = np.zeros((5, 8, 8, 3), dtype=np.float32)
    for index in range(5):
        sequence[index] = index * 0.1
    quiet = temporal_metrics(sequence, sequence)
    blinking = sequence.copy()
    blinking[2] += 0.3
    detected = temporal_metrics(blinking, sequence)
    motionless = temporal_metrics(sequence * 0, sequence * 0)
    if quiet["excess_pixel_mean"] != 0 or detected["excess_pixel_mean"] < 0.1 or motionless["reference_moves"]:
        raise ToolError("Zamansal ölçümün yanıp sönme veya durgun referans kontrolü başarısız")
    return {"passes": True, "negative_controls_detected": failures_detected,
            "temporal_blink_detected": True, "motionless_reference_rejected": True,
            "gpu_started": False, "percentile_known_answer": 29.0}


def parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="GPU açmadan ölçüm aracını sına")
    parser.add_argument("--analyze-csv", type=Path, help="Var olan CSV dosyasını bağımsız ölç")
    parser.add_argument("--mode", choices=("smoke", "benchmark", "quality", "temporal", "transport", "all"), default="smoke")
    parser.add_argument("--exe", type=Path, default=ROOT / "build" / "pc_graphics.exe")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--frames", type=int, default=300)
    parser.add_argument("--warmup", type=int, default=30)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--views", type=int, nargs="+", default=list(range(6)))
    parser.add_argument("--spp", type=int, default=3)
    parser.add_argument("--bounces", type=int, default=3)
    parser.add_argument("--reference-spp", type=int, default=128)
    parser.add_argument("--quality-width", type=int, default=1280)
    parser.add_argument("--quality-height", type=int, default=720)
    parser.add_argument("--quality-mae-limit", type=float, default=0.06)
    parser.add_argument("--temporal-frames", type=int, default=48)
    parser.add_argument("--temporal-view", type=int, choices=range(6), default=0)
    parser.add_argument("--temporal-width", type=int, default=960)
    parser.add_argument("--temporal-height", type=int, default=540)
    parser.add_argument("--temporal-spp", type=int, default=64)
    parser.add_argument("--temporal-extra-limit", type=float, default=0.006)
    parser.add_argument("--temporal-ratio-limit", type=float, default=1.10)
    parser.add_argument("--p50-limit", type=float, default=33.33)
    parser.add_argument("--p95-limit", type=float, default=40.0)
    parser.add_argument("--timeout", type=float, default=300)
    options = parser.parse_args(argv)
    if min(options.frames, options.repeats, options.spp, options.bounces, options.reference_spp,
           options.quality_width, options.quality_height) < 1 or options.warmup < 0:
        parser.error("Kare, tekrar, örnek ve boyutlar pozitif; ısınma sıfır veya büyük olmalı")
    if not all(0 <= view <= 5 for view in options.views) or len(set(options.views)) != len(options.views):
        parser.error("Bakışlar tekrarsız 0..5 aralığında olmalı")
    if options.temporal_frames < 3 or min(options.temporal_spp, options.temporal_width, options.temporal_height) < 1:
        parser.error("Zamansal ölçüm en az üç kare, pozitif boyutlar ve referans örneği ister")
    for name in ("quality_mae_limit", "temporal_extra_limit", "temporal_ratio_limit", "p50_limit", "p95_limit", "timeout"):
        value = getattr(options, name)
        if not math.isfinite(value) or value <= 0:
            parser.error(f"{name}: sonlu pozitif değer gerekli")
    return options


def run(options: argparse.Namespace) -> dict:
    """Execute the selected evidence suite through the production CLI."""
    if options.self_test:
        return self_test()
    if options.analyze_csv:
        return asdict(summarize(read_rows(options.analyze_csv), options.p50_limit, options.p95_limit))
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S_%fZ")
    destination = (options.output or ROOT / "captures" / "ruya" / "verification" / stamp).resolve()
    if destination.exists() and any(destination.iterdir()):
        raise ToolError(f"Eski kanıtın üzerine yazılmaz: {destination}")
    destination.mkdir(parents=True, exist_ok=True)
    executable = options.exe.resolve()
    copied = executable.with_name(f"ruya_verify_{stamp}.exe")
    try:
        shutil.copy2(executable, copied)
    except OSError as exc:
        raise ToolError(f"Çalıştırılabilir kopyalanamadı: {exc}") from exc
    commands = []
    results = {"utc_started": stamp, "mode": options.mode, "presentation_measured": False,
               "thresholds": {"frame_p50_ms": options.p50_limit, "frame_p95_ms": options.p95_limit,
                              "quality_mae_srgb": options.quality_mae_limit,
                              "temporal_excess_pixel_mean": options.temporal_extra_limit,
                              "temporal_second_difference_ratio": options.temporal_ratio_limit},
               "verifier_selftest": self_test(), "commands": commands, "timings": [], "images": [],
               "quality": [], "temporal": [], "transport": [], "hdr_is_raw_before_denoising": True,
               "exe": str(copied), "exe_sha256": file_hash(copied),
               "source_hashes": {}, "passes": False}
    sources = list((ROOT / "src" / "scenes" / "ruya").rglob("*")) + [ROOT / "scenes" / "ruya.cfg", Path(__file__)]
    for source in sources:
        if source.is_file():
            results["source_hashes"][str(source.relative_to(ROOT))] = file_hash(source)
    base = [str(copied), "--scene", "ruya", "--headless"]
    try:
        results["gpu"] = command(["nvidia-smi", "--query-gpu=name,driver_version,memory.total,power.limit",
                                  "--format=csv,noheader"], destination / "gpu.txt", 15, commands).strip()
        command([str(copied), "--scene", "ruya", "--test"], destination / "world_test.txt", options.timeout, commands)
        if options.mode in ("smoke", "all"):
            prefix = destination / "smoke"
            command(base + ["--width", "640", "--height", "360", "--frames", "2", "--warmup", "1",
                            "--out", str(prefix), "--lifecycle"], destination / "smoke.txt", options.timeout, commands)
            results["images"].append(check_snapshot(prefix, 640, 360))
        if options.mode in ("benchmark", "all"):
            for repeat in range(options.repeats):
                order = options.views if repeat % 2 == 0 else list(reversed(options.views))
                for view in order:
                    name = f"bench_r{repeat}_v{view}"
                    prefix = destination / name
                    csv_path = destination / f"{name}.csv"
                    command(base + ["--width", "2560", "--height", "1440", "--view", str(view),
                                    "--frames", str(options.frames), "--warmup", str(options.warmup),
                                    "--spp", str(options.spp), "--bounces", str(options.bounces),
                                    "--csv", str(csv_path), "--out", str(prefix)],
                            destination / f"{name}.txt", options.timeout, commands)
                    summary = summarize(read_rows(csv_path, options.frames), options.p50_limit, options.p95_limit)
                    results["timings"].append({"repeat": repeat, "view": view, **asdict(summary)})
                    results["images"].append(check_snapshot(prefix, 2560, 1440))
            prefix = destination / "route"
            csv_path = destination / "route.csv"
            command(base + ["--width", "2560", "--height", "1440", "--view", "0", "--route",
                            "--frames", str(options.frames), "--warmup", str(options.warmup),
                            "--spp", str(options.spp), "--bounces", str(options.bounces),
                            "--csv", str(csv_path), "--out", str(prefix)],
                    destination / "route.txt", options.timeout, commands)
            summary = summarize(read_rows(csv_path, options.frames), options.p50_limit, options.p95_limit)
            results["timings"].append({"route": True, **asdict(summary)})
            results["images"].append(check_snapshot(prefix, 2560, 1440))
        if options.mode in ("quality", "all"):
            for view in options.views:
                prefixes = {}
                for label, samples, offset in (("live", options.spp, 0), ("ref", options.reference_spp, 0),
                                               ("ref_independent", options.reference_spp, options.reference_spp)):
                    prefix = destination / f"quality_v{view}_{label}"
                    prefixes[label] = prefix
                    sampling = ["--raw"] if label != "live" else []
                    command(base + sampling + ["--width", str(options.quality_width), "--height", str(options.quality_height),
                                    "--view", str(view), "--frames", "1", "--warmup", "0",
                                    "--spp", str(samples), "--bounces", str(options.bounces),
                                    "--sample-offset", str(offset), "--out", str(prefix)],
                            prefix.with_suffix(".txt"), options.timeout, commands)
                    results["images"].append(check_snapshot(prefix, options.quality_width, options.quality_height))
                comparison = compare_images(prefixes["live"].with_suffix(".png"), prefixes["ref"].with_suffix(".png"))
                convergence = compare_images(prefixes["ref"].with_suffix(".png"), prefixes["ref_independent"].with_suffix(".png"))
                passed = comparison["mae_srgb"] <= options.quality_mae_limit
                passed &= convergence["mae_srgb"] <= options.quality_mae_limit / 2
                results["quality"].append({"view": view, "raw_reference": True, "comparison": comparison,
                                           "reference_independent_offset": convergence, "passes": bool(passed)})
        if options.mode in ("temporal", "all"):
            camera_records = []
            for label, samples in (("live", options.spp), ("reference", options.temporal_spp)):
                prefix = destination / f"temporal_{label}"
                dump_directory = destination / f"temporal_{label}_frames"
                csv_path = prefix.with_suffix(".csv")
                sampling = ["--raw"] if label == "reference" else []
                command(base + sampling + ["--width", str(options.temporal_width), "--height", str(options.temporal_height),
                                          "--view", str(options.temporal_view), "--route",
                                          "--frames", str(options.temporal_frames), "--warmup", "0",
                                          "--spp", str(samples), "--bounces", str(options.bounces),
                                          "--out", str(prefix), "--dump-dir", str(dump_directory),
                                          "--csv", str(csv_path)],
                        prefix.with_suffix(".txt"), options.timeout, commands)
                results["images"].append(check_snapshot(prefix, options.temporal_width, options.temporal_height))
                records = read_rows(csv_path, options.temporal_frames)
                for record in records:
                    if any(name not in record or not math.isfinite(record[name]) for name in CAMERA_COLUMNS):
                        raise ToolError(f"{csv_path}: kamera veya çizim ayarları kayıtta eksik")
                camera_records.append(records)
            compared_fields = ("x", "y", "z", "yaw", "pitch", "width", "height", "bounces", "cells", "triangles")
            if any(live[name] != reference[name] for live, reference in zip(*camera_records) for name in compared_fields):
                raise ToolError("Zamansal canlı ve referansın kamera yolu veya dünya kapsamı farklı")
            measured = analyze_sequence(destination / "temporal_live_frames", destination / "temporal_reference_frames",
                                        options.temporal_frames)
            ratio = measured["second_difference_ratio"]
            passed = measured["reference_moves"] and measured["excess_pixel_mean"] <= options.temporal_extra_limit
            passed &= ratio is not None and ratio <= options.temporal_ratio_limit
            results["temporal"].append({**measured, "camera_records_equal": True, "passes": bool(passed)})
        if options.mode in ("transport", "all"):
            results["transport"] = verify_transport(base, destination, options, commands)
        results["passes"] = all(item["passes_timing"] for item in results["timings"])
        results["passes"] &= all(item["passes"] for item in results["quality"])
        results["passes"] &= all(item["passes"] for item in results["temporal"])
        results["passes"] &= all(item["passes"] for item in results["transport"])
    except ToolError as exc:
        results["failure"] = str(exc)
        raise
    finally:
        results["utc_finished"] = datetime.now(timezone.utc).isoformat()
        save_json(destination / "manifest.json", results)
    return results


def main(argv: list[str] | None = None) -> int:
    options = parse_args(argv)
    try:
        result = run(options)
    except ToolError as exc:
        print(f"KALDI: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result.get("passes", result.get("passes_timing", False)) else 1


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    sys.exit(main())
