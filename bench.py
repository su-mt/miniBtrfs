#!/usr/bin/env python3
"""
fs_minibenchmark.py — Mini filesystem benchmark
Операции: создание файлов/директорий, запись, чтение.
Без удалений, rename и других нереализованных операций.
"""

import os
import sys
import time
import tempfile
import statistics
import random
import hashlib
import traceback
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, Dict, Tuple, Optional


# ──────────────────────────────────────────────
# Конфигурация
# ──────────────────────────────────────────────

@dataclass
class BenchConfig:
    base_dir: str        = ""
    file_count: int      = 500
    file_size_kb: int    = 64
    dir_count: int       = 50
    dir_depth: int       = 3
    read_iterations: int = 3
    large_file_mb: int   = 64
    sequential_writes: int = 8
    random_write_files: int = 200
    verbose: bool        = False      # включается флагом -v


# ──────────────────────────────────────────────
# Логирование
# ──────────────────────────────────────────────

_cfg_ref: Optional[BenchConfig] = None

def vlog(msg: str) -> None:
    """Выводит сообщение только в verbose-режиме."""
    if _cfg_ref and _cfg_ref.verbose:
        ts = time.perf_counter()
        print(f"  [DBG {ts:>10.3f}s]  {msg}", flush=True)

def elog(msg: str) -> None:
    """Всегда выводит сообщение об ошибке."""
    print(f"  [ERR]  {msg}", file=sys.stderr, flush=True)


# ──────────────────────────────────────────────
# Вспомогательные утилиты
# ──────────────────────────────────────────────

def hr(label: str, width: int = 60) -> None:
    print(f"\n{'─' * width}")
    print(f"  {label}")
    print(f"{'─' * width}")


def result_line(label: str, value: str, unit: str = "") -> None:
    print(f"  {label:<40} {value:>10} {unit}")


def random_bytes(size: int) -> bytes:
    return os.urandom(size)


def fmt_speed(bytes_total: int, seconds: float) -> str:
    mb = bytes_total / (1024 * 1024)
    speed = mb / seconds if seconds > 0 else float("inf")
    return f"{speed:.1f} MB/s"


def fmt_iops(count: int, seconds: float) -> str:
    iops = count / seconds if seconds > 0 else float("inf")
    return f"{iops:.0f} ops/s"


def sha256_of_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_of_file(path: Path, buf_size: int = 4 * 1024 * 1024) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        buf = bytearray(buf_size)
        while True:
            n = fh.readinto(buf)
            if not n:
                break
            h.update(buf[:n])
    return h.hexdigest()


# ──────────────────────────────────────────────
# FUSE-совместимый mkdir
# ──────────────────────────────────────────────

def _mkdir_p(base: Path, rel_parts: List[str]) -> int:
    """
    Создаёт суффикс пути (rel_parts) относительно base,
    по одному уровню за вызов (один syscall = один mkdir).
    Совместимо с FUSE ФС, где parents=True ломается.
    """
    created = 0
    current = base
    for part in rel_parts:
        current = current / part
        try:
            current.mkdir()
            created += 1
            vlog(f"mkdir OK  {current}")
        except FileExistsError:
            vlog(f"mkdir SKIP (exists)  {current}")
        except OSError as e:
            elog(f"mkdir FAILED  {current}: {e}")
            raise
    return created


# ──────────────────────────────────────────────
# Benchmark-функции
# ──────────────────────────────────────────────

def bench_mkdir(cfg: BenchConfig) -> dict:
    dirs_created = 0
    paths: List[Path] = []
    base = Path(cfg.base_dir)

    vlog("bench_mkdir: создаём корневой каталог 'dirs'")
    try:
        (base / "dirs").mkdir()
    except FileExistsError:
        vlog("  'dirs' уже существует, пропускаем")

    t0 = time.perf_counter()
    for i in range(cfg.dir_count):
        rel = [f"lvl{d}_{i % max(1, cfg.dir_count // cfg.dir_depth)}"
               for d in range(cfg.dir_depth)]
        rel.append(f"leaf_{i}")
        vlog(f"  mkdir subtree [{i}/{cfg.dir_count}]: {'/'.join(rel)}")
        try:
            cnt = _mkdir_p(base / "dirs", rel)
            dirs_created += cnt
        except OSError as e:
            elog(f"  bench_mkdir: subtree {i} — {e}")
            raise
        paths.append(base / "dirs" / Path(*rel))
    elapsed = time.perf_counter() - t0

    return {
        "label": "mkdir (создание директорий)",
        "count": dirs_created,
        "elapsed": elapsed,
        "paths": paths,
    }


def bench_create_files(cfg: BenchConfig) -> dict:
    size = cfg.file_size_kb * 1024
    data = random_bytes(size)
    expected_hash = sha256_of_bytes(data)
    vlog(f"bench_create_files: size={size} bytes, sha256={expected_hash[:16]}…")

    root = Path(cfg.base_dir) / "files"
    try:
        root.mkdir()
    except FileExistsError:
        vlog("  'files' уже существует, пропускаем")

    paths: List[Path] = []
    first_error: Optional[str] = None

    t0 = time.perf_counter()
    for i in range(cfg.file_count):
        p = root / f"file_{i:06d}.bin"
        try:
            p.write_bytes(data)
            vlog(f"  write OK  {p.name}")
        except OSError as e:
            msg = f"write FAILED {p.name}: {e}"
            elog(msg)
            if first_error is None:
                first_error = msg
            # продолжаем — считаем сколько файлов упало
        paths.append(p)
    elapsed = time.perf_counter() - t0

    total_bytes = size * cfg.file_count
    result = {
        "label": "create+write (много мелких файлов)",
        "count": cfg.file_count,
        "elapsed": elapsed,
        "bytes": total_bytes,
        "paths": paths,
        "expected_hash": expected_hash,
    }
    if first_error:
        result["write_error"] = first_error
    return result


def bench_read_files(cfg: BenchConfig, paths: List[Path], expected_hash: str) -> dict:
    vlog(f"bench_read_files: {len(paths)} файлов × {cfg.read_iterations} итераций")
    vlog(f"  ожидаемый sha256={expected_hash[:16]}…")

    total_bytes = 0
    times: List[float] = []
    corrupted: List[str] = []
    read_errors: List[str] = []

    for iteration in range(cfg.read_iterations):
        vlog(f"  итерация {iteration + 1}/{cfg.read_iterations}")
        t0 = time.perf_counter()
        for p in paths:
            try:
                data = p.read_bytes()
                actual_size = len(data)
                total_bytes += actual_size
                if iteration == 0:
                    got = sha256_of_bytes(data)
                    if got != expected_hash:
                        corrupted.append(p.name)
                        vlog(f"    CORRUPT {p.name}: "
                             f"size={actual_size} expected_hash={expected_hash[:16]}… "
                             f"got={got[:16]}…")
                    else:
                        vlog(f"    OK {p.name}  size={actual_size}")
            except OSError as e:
                msg = f"read FAILED {p.name}: {e}"
                elog(msg)
                read_errors.append(msg)
        times.append(time.perf_counter() - t0)

    if corrupted:
        elog(f"bench_read_files: {len(corrupted)}/{len(paths)} файлов с corruption")

    status = "✓ OK" if not corrupted else f"✗ CORRUPT ({len(corrupted)} файлов)"
    return {
        "label": f"read (чтение × {cfg.read_iterations} итераций)",
        "count": len(paths) * cfg.read_iterations,
        "elapsed": sum(times),
        "elapsed_avg_iter": statistics.mean(times),
        "bytes": total_bytes,
        "times": times,
        "integrity": status,
        "corrupted": corrupted,
        "read_errors": read_errors,
    }


def _build_expected(chunk: bytes, size: int) -> bytes:
    parts = []
    written = 0
    while written < size:
        to_write = min(len(chunk), size - written)
        parts.append(chunk[:to_write])
        written += to_write
    return b"".join(parts)


def bench_sequential_write(cfg: BenchConfig) -> dict:
    size = cfg.large_file_mb * 1024 * 1024
    chunk_size = min(size, 4 * 1024 * 1024)
    chunk = random_bytes(chunk_size)

    vlog(f"bench_sequential_write: size={size // (1024*1024)} МБ, "
         f"chunk={chunk_size // 1024} КБ, runs={cfg.sequential_writes}")

    dest = Path(cfg.base_dir) / "seq_write"
    try:
        dest.mkdir()
    except FileExistsError:
        vlog("  'seq_write' уже существует, пропускаем")

    times: List[float] = []
    written_hash = sha256_of_bytes(_build_expected(chunk, size))
    vlog(f"  эталонный sha256={written_hash[:16]}…")
    file_hashes: Dict[str, str] = {}
    write_errors: List[str] = []

    for run in range(cfg.sequential_writes):
        p = dest / f"large_{run}.bin"
        vlog(f"  run {run}: записываем {p.name}")
        try:
            t0 = time.perf_counter()
            with p.open("wb") as fh:
                written = 0
                while written < size:
                    to_write = min(len(chunk), size - written)
                    n = fh.write(chunk[:to_write])
                    if n != to_write:
                        elog(f"  short write at offset {written}: "
                             f"expected {to_write}, got {n}")
                    written += to_write
                fh.flush()
                os.fsync(fh.fileno())
            elapsed_run = time.perf_counter() - t0
            times.append(elapsed_run)
            file_hashes[p.name] = written_hash
            vlog(f"  run {run}: OK  {elapsed_run:.3f}s  "
                 f"{(size / (1024*1024)) / elapsed_run:.1f} MB/s")
        except OSError as e:
            msg = f"seq_write run {run} FAILED {p.name}: {e}"
            elog(msg)
            write_errors.append(msg)
            times.append(0.0)

    total_bytes = size * cfg.sequential_writes
    result = {
        "label": f"seq write fsync ({cfg.large_file_mb} МБ × {cfg.sequential_writes})",
        "count": cfg.sequential_writes,
        "elapsed": sum(times),
        "elapsed_avg": statistics.mean(times) if times else 0.0,
        "bytes": total_bytes,
        "times": times,
        "file_hashes": file_hashes,
        "dest_dir": dest,
    }
    if write_errors:
        result["write_errors"] = write_errors
    return result


def bench_sequential_read(cfg: BenchConfig, file_hashes: Dict[str, str]) -> dict:
    src = Path(cfg.base_dir) / "seq_write"
    files = sorted(src.glob("*.bin"))
    if not files:
        elog("bench_sequential_read: нет файлов в seq_write/")
        return {"label": "seq read", "skipped": True}

    vlog(f"bench_sequential_read: {len(files)} файлов, "
         f"проверяем sha256 каждого")

    total_bytes = 0
    corrupted: List[str] = []
    read_errors: List[str] = []

    t0 = time.perf_counter()
    for p in files:
        try:
            got_hash = sha256_of_file(p)
            stat = p.stat()
            actual_size = stat.st_size
            total_bytes += actual_size

            expected = file_hashes.get(p.name)
            if expected is None:
                vlog(f"  {p.name}: нет эталона, пропускаем")
                continue
            if got_hash != expected:
                corrupted.append(p.name)
                vlog(f"  CORRUPT {p.name}: size={actual_size}  "
                     f"expected={expected[:16]}…  got={got_hash[:16]}…")
            else:
                vlog(f"  OK {p.name}  size={actual_size}")
        except OSError as e:
            msg = f"seq_read FAILED {p.name}: {e}"
            elog(msg)
            read_errors.append(msg)

    elapsed = time.perf_counter() - t0

    if corrupted:
        elog(f"bench_sequential_read: {len(corrupted)}/{len(files)} файлов с corruption")

    status = "✓ OK" if not corrupted else f"✗ CORRUPT ({len(corrupted)} файлов)"
    result = {
        "label": f"seq read+verify ({cfg.large_file_mb} МБ × {len(files)})",
        "count": len(files),
        "elapsed": elapsed,
        "bytes": total_bytes,
        "integrity": status,
        "corrupted": corrupted,
    }
    if read_errors:
        result["read_errors"] = read_errors
    return result


def bench_random_write(cfg: BenchConfig) -> dict:
    size = cfg.file_size_kb * 1024
    data = random_bytes(size)
    dest = Path(cfg.base_dir) / "rand_write"
    try:
        dest.mkdir()
    except FileExistsError:
        vlog("  'rand_write' уже существует, пропускаем")

    vlog(f"bench_random_write: создаём {cfg.random_write_files} файлов")
    paths: List[Path] = []
    for i in range(cfg.random_write_files):
        p = dest / f"rand_{i:05d}.bin"
        try:
            p.write_bytes(data)
        except OSError as e:
            elog(f"  initial write FAILED {p.name}: {e}")
            raise
        paths.append(p)

    write_size = 512
    writes = cfg.random_write_files * 4
    rng = random.Random(42)
    patch = random_bytes(write_size)

    vlog(f"  начинаем {writes} случайных записей по {write_size} байт")

    errors: List[str] = []
    t0 = time.perf_counter()
    for idx in range(writes):
        p = rng.choice(paths)
        offset = rng.randint(0, max(0, size - write_size))
        try:
            with p.open("r+b") as fh:
                fh.seek(offset)
                n = fh.write(patch)
                if n != write_size:
                    elog(f"  short write [{idx}] {p.name} "
                         f"offset={offset}: expected {write_size}, got {n}")
            vlog(f"  [{idx}/{writes}] write OK  {p.name}  offset={offset}")
        except OSError as e:
            msg = f"rand_write [{idx}] FAILED {p.name} offset={offset}: {e}"
            elog(msg)
            errors.append(msg)
    elapsed = time.perf_counter() - t0

    result = {
        "label": f"random write (4 × {cfg.random_write_files} файлов)",
        "count": writes,
        "elapsed": elapsed,
        "bytes": write_size * writes,
    }
    if errors:
        result["write_errors"] = errors
    return result


def bench_stat(cfg: BenchConfig, paths: List[Path]) -> dict:
    vlog(f"bench_stat: {len(paths)} файлов")
    errors: List[str] = []
    t0 = time.perf_counter()
    for p in paths:
        try:
            st = p.stat()
            vlog(f"  stat OK  {p.name}  size={st.st_size}")
        except OSError as e:
            msg = f"stat FAILED {p.name}: {e}"
            elog(msg)
            errors.append(msg)
    elapsed = time.perf_counter() - t0

    result = {
        "label": "stat (метаданные файлов)",
        "count": len(paths),
        "elapsed": elapsed,
    }
    if errors:
        result["stat_errors"] = errors
    return result


def bench_listdir(cfg: BenchConfig) -> dict:
    root = Path(cfg.base_dir)
    dirs = [root / "files", root / "dirs", root / "seq_write", root / "rand_write"]

    vlog(f"bench_listdir: scandir по {[str(d) for d in dirs]}")

    t0 = time.perf_counter()
    entries = 0
    for d in dirs:
        if d.exists():
            try:
                cnt = 0
                for entry in os.scandir(d):
                    cnt += 1
                    vlog(f"  {d.name}/{entry.name}")
                entries += cnt
                vlog(f"  {d.name}: {cnt} записей")
            except OSError as e:
                elog(f"  scandir FAILED {d}: {e}")
        else:
            vlog(f"  {d} — не существует, пропускаем")
    elapsed = time.perf_counter() - t0

    return {
        "label": "scandir (листинг директорий)",
        "count": entries,
        "elapsed": elapsed,
    }


# ──────────────────────────────────────────────
# Вывод результатов
# ──────────────────────────────────────────────

def print_result(r: dict) -> None:
    if r.get("skipped"):
        print(f"  {r['label']:<42} — пропущено")
        return

    label = r["label"]
    count = r.get("count", 0)
    elapsed = r.get("elapsed", 0)

    result_line(label, f"{elapsed:.3f}", "сек")

    if "bytes" in r:
        speed = fmt_speed(r["bytes"], elapsed)
        result_line("  └─ пропускная способность", speed)

    iops_count = r.get("count", count)
    result_line("  └─ операций/сек", fmt_iops(iops_count, elapsed))

    if "times" in r:
        times = r["times"]
        result_line("  └─ мин / сред / макс (сек)",
                    f"{min(times):.3f} / {statistics.mean(times):.3f} / {max(times):.3f}")

    if "integrity" in r:
        status = r["integrity"]
        result_line("  └─ целостность данных", status)
        if r.get("corrupted"):
            for name in r["corrupted"][:5]:
                print(f"       ⚠  {name}")
            if len(r["corrupted"]) > 5:
                print(f"       … ещё {len(r['corrupted']) - 5} файл(ов)")

    # Сводка ошибок (всегда, не только в verbose)
    for err_key in ("write_error", "write_errors", "read_errors", "stat_errors"):
        errs = r.get(err_key)
        if errs:
            if isinstance(errs, str):
                errs = [errs]
            print(f"  └─ ошибки ({err_key}):")
            for e in errs[:3]:
                print(f"       ✗  {e}")
            if len(errs) > 3:
                print(f"       … ещё {len(errs) - 3}")


# ──────────────────────────────────────────────
# main
# ──────────────────────────────────────────────

def main() -> None:
    cfg = BenchConfig()

    # Парсим аргументы вручную: [base_dir] [-v]
    args = sys.argv[1:]
    if "-v" in args:
        cfg.verbose = True
        args = [a for a in args if a != "-v"]

    global _cfg_ref
    _cfg_ref = cfg

    if args:
        cfg.base_dir = args[0]
        os.makedirs(cfg.base_dir, exist_ok=True)
    else:
        tmp = tempfile.mkdtemp(prefix="fs_bench_")
        cfg.base_dir = tmp

    print("=" * 60)
    print("  🗂  Filesystem Mini-Benchmark")
    print("=" * 60)
    print(f"  Рабочая директория : {cfg.base_dir}")
    print(f"  Файлов (мелких)    : {cfg.file_count}  ×  {cfg.file_size_kb} КБ")
    print(f"  Большой файл       : {cfg.large_file_mb} МБ  ×  {cfg.sequential_writes} прогонов")
    print(f"  Директорий         : {cfg.dir_count}  (глубина {cfg.dir_depth})")
    print(f"  Verbose            : {'да (-v)' if cfg.verbose else 'нет (добавь -v для деталей)'}")
    print()
    print("  Запуск тестов...", flush=True)

    results = {}

    hr("1. Создание директорий")
    r = bench_mkdir(cfg)
    results["mkdir"] = r
    print_result(r)

    hr("2. Создание и запись мелких файлов")
    r = bench_create_files(cfg)
    results["create"] = r
    print_result(r)

    hr("3. Чтение мелких файлов + верификация SHA-256")
    r = bench_read_files(cfg, results["create"]["paths"],
                         results["create"]["expected_hash"])
    results["read"] = r
    print_result(r)

    hr("4. Получение метаданных (stat)")
    r = bench_stat(cfg, results["create"]["paths"])
    results["stat"] = r
    print_result(r)

    hr("5. Листинг директорий (scandir)")
    r = bench_listdir(cfg)
    results["listdir"] = r
    print_result(r)

    hr("6. Последовательная запись большого файла (с fsync)")
    r = bench_sequential_write(cfg)
    results["seq_write"] = r
    print_result(r)

    hr("7. Последовательное чтение большого файла + верификация SHA-256")
    r = bench_sequential_read(cfg, results["seq_write"]["file_hashes"])
    results["seq_read"] = r
    print_result(r)

    hr("8. Случайная запись (random write)")
    r = bench_random_write(cfg)
    results["rand_write"] = r
    print_result(r)

    # Итоговая сводка
    hr("ИТОГО", 60)
    total = sum(v.get("elapsed", 0) for v in results.values())
    result_line("Общее время всех тестов", f"{total:.2f}", "сек")

    checks = {k: v for k, v in results.items() if "integrity" in v}
    all_ok = all("✓" in v["integrity"] for v in checks.values())
    print()
    print(f"  Верификация данных: {'✓ все проверки пройдены' if all_ok else '✗ ОБНАРУЖЕНЫ ОШИБКИ!'}")
    for key, v in checks.items():
        print(f"    [{key}]  {v['integrity']}")

    print()
    print("  Файлы оставлены в:", cfg.base_dir)
    print("=" * 60)

    if not all_ok:
        sys.exit(1)


if __name__ == "__main__":
    main()