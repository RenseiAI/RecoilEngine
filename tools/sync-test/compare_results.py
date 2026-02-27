#!/usr/bin/env python3
"""
STREFLOP Float Test Result Comparator

Reads two binary result files produced by streflop-float-test and reports
matching/mismatching results with ULP difference analysis.

Usage:
    python3 compare_results.py <reference.bin> <test.bin>
"""

import struct
import sys
import math
from collections import defaultdict


def read_results(path):
    """Read a binary result file. Returns (header_dict, list_of_records)."""
    with open(path, "rb") as f:
        data = f.read()

    offset = 0

    # Magic
    magic = data[offset:offset + 4]
    offset += 4
    if magic != b"SFLT":
        raise ValueError(f"{path}: bad magic {magic!r}, expected b'SFLT'")

    # Version
    version = struct.unpack_from("<I", data, offset)[0]
    offset += 4
    if version != 1:
        raise ValueError(f"{path}: unsupported version {version}")

    # Mode string (null-terminated)
    end = data.index(b"\x00", offset)
    mode = data[offset:end].decode("utf-8")
    offset = end + 1

    # Arch string (null-terminated)
    end = data.index(b"\x00", offset)
    arch = data[offset:end].decode("utf-8")
    offset = end + 1

    # Test count
    count = struct.unpack_from("<I", data, offset)[0]
    offset += 4

    # Records: id(4) + prec(1) + result(8) = 13 bytes each
    records = {}
    for _ in range(count):
        test_id = struct.unpack_from("<I", data, offset)[0]
        offset += 4
        prec = chr(data[offset])
        offset += 1
        result_bits = struct.unpack_from("<Q", data, offset)[0]
        offset += 8
        records[test_id] = (prec, result_bits)

    header = {"mode": mode, "arch": arch, "version": version, "count": count}
    return header, records


def ulp_diff_f32(bits_a, bits_b):
    """Compute ULP distance between two float32 bit patterns."""
    # Mask to 32 bits
    a = bits_a & 0xFFFFFFFF
    b = bits_b & 0xFFFFFFFF

    # Handle NaN
    if (a & 0x7F800000) == 0x7F800000 and (a & 0x007FFFFF) != 0:
        return None  # NaN
    if (b & 0x7F800000) == 0x7F800000 and (b & 0x007FFFFF) != 0:
        return None  # NaN

    # Convert to signed magnitude for ULP comparison
    if a & 0x80000000:
        a = 0x80000000 - (a & 0x7FFFFFFF)
    else:
        a = a + 0x80000000
    if b & 0x80000000:
        b = 0x80000000 - (b & 0x7FFFFFFF)
    else:
        b = b + 0x80000000

    return abs(a - b)


def ulp_diff_f64(bits_a, bits_b):
    """Compute ULP distance between two float64 bit patterns."""
    a = bits_a & 0xFFFFFFFFFFFFFFFF
    b = bits_b & 0xFFFFFFFFFFFFFFFF

    # Handle NaN
    if (a & 0x7FF0000000000000) == 0x7FF0000000000000 and (a & 0x000FFFFFFFFFFFFF) != 0:
        return None
    if (b & 0x7FF0000000000000) == 0x7FF0000000000000 and (b & 0x000FFFFFFFFFFFFF) != 0:
        return None

    if a & 0x8000000000000000:
        a = 0x8000000000000000 - (a & 0x7FFFFFFFFFFFFFFF)
    else:
        a = a + 0x8000000000000000
    if b & 0x8000000000000000:
        b = 0x8000000000000000 - (b & 0x7FFFFFFFFFFFFFFF)
    else:
        b = b + 0x8000000000000000

    return abs(a - b)


def bits_to_float(bits):
    """Convert uint32 bit pattern to float."""
    return struct.unpack("<f", struct.pack("<I", bits & 0xFFFFFFFF))[0]


def bits_to_double(bits):
    """Convert uint64 bit pattern to double."""
    return struct.unpack("<d", struct.pack("<Q", bits & 0xFFFFFFFFFFFFFFFF))[0]


def read_text_categories(path):
    """Try to read the .txt file to get category/operation info per test ID.
    Returns dict: test_id -> (cat, op)"""
    txt_path = path.rsplit(".", 1)[0] + ".txt"
    info = {}
    try:
        with open(txt_path, "r") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                parts = line.split()
                if len(parts) >= 4:
                    try:
                        tid = int(parts[0])
                        prec = parts[1]
                        cat = parts[2]
                        op = parts[3]
                        info[tid] = (cat, op)
                    except (ValueError, IndexError):
                        pass
    except FileNotFoundError:
        pass
    return info


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <reference.bin> <test.bin>")
        print()
        print("Compares two STREFLOP float test result files and reports")
        print("matching/mismatching results with ULP difference analysis.")
        sys.exit(1)

    ref_path = sys.argv[1]
    test_path = sys.argv[2]

    print(f"Reference: {ref_path}")
    print(f"Test:      {test_path}")
    print()

    ref_header, ref_records = read_results(ref_path)
    test_header, test_records = read_results(test_path)

    print(f"Reference: {ref_header['mode']} on {ref_header['arch']} ({ref_header['count']} tests)")
    print(f"Test:      {test_header['mode']} on {test_header['arch']} ({test_header['count']} tests)")
    print()

    # Try to load category info from text files
    ref_info = read_text_categories(ref_path)
    test_info = read_text_categories(test_path)
    cat_info = {**test_info, **ref_info}  # prefer reference info

    # Compare common test IDs
    common_ids = sorted(set(ref_records.keys()) & set(test_records.keys()))
    ref_only = sorted(set(ref_records.keys()) - set(test_records.keys()))
    test_only = sorted(set(test_records.keys()) - set(ref_records.keys()))

    if ref_only:
        print(f"WARNING: {len(ref_only)} tests only in reference (IDs: {ref_only[:10]}{'...' if len(ref_only) > 10 else ''})")
    if test_only:
        print(f"WARNING: {len(test_only)} tests only in test (IDs: {test_only[:10]}{'...' if len(test_only) > 10 else ''})")

    matching = 0
    mismatched = 0
    nan_both = 0
    mismatches_by_cat = defaultdict(list)

    for tid in common_ids:
        ref_prec, ref_bits = ref_records[tid]
        test_prec, test_bits = test_records[tid]

        if ref_prec != test_prec:
            cat, op = cat_info.get(tid, ("?", "?"))
            mismatches_by_cat[cat].append({
                "id": tid, "op": op, "reason": "precision mismatch",
                "ref_bits": ref_bits, "test_bits": test_bits,
            })
            mismatched += 1
            continue

        if ref_bits == test_bits:
            matching += 1
            continue

        # Check if both are NaN
        prec = ref_prec
        if prec == "F":
            ref_is_nan = (ref_bits & 0x7F800000) == 0x7F800000 and (ref_bits & 0x007FFFFF) != 0
            test_is_nan = (test_bits & 0x7F800000) == 0x7F800000 and (test_bits & 0x007FFFFF) != 0
        else:
            ref_is_nan = (ref_bits & 0x7FF0000000000000) == 0x7FF0000000000000 and (ref_bits & 0x000FFFFFFFFFFFFF) != 0
            test_is_nan = (test_bits & 0x7FF0000000000000) == 0x7FF0000000000000 and (test_bits & 0x000FFFFFFFFFFFFF) != 0

        if ref_is_nan and test_is_nan:
            nan_both += 1
            matching += 1  # NaN payloads may differ; count as match
            continue

        # Genuine mismatch
        mismatched += 1
        cat, op = cat_info.get(tid, ("?", "?"))

        if prec == "F":
            ulp = ulp_diff_f32(ref_bits, test_bits)
            ref_val = bits_to_float(ref_bits)
            test_val = bits_to_float(test_bits)
            info = {
                "id": tid, "op": op,
                "ref_hex": f"{ref_bits & 0xFFFFFFFF:08X}",
                "test_hex": f"{test_bits & 0xFFFFFFFF:08X}",
                "ref_val": f"{ref_val:.9g}",
                "test_val": f"{test_val:.9g}",
                "ulp": ulp,
            }
        else:
            ulp = ulp_diff_f64(ref_bits, test_bits)
            ref_val = bits_to_double(ref_bits)
            test_val = bits_to_double(test_bits)
            info = {
                "id": tid, "op": op,
                "ref_hex": f"{ref_bits:016X}",
                "test_hex": f"{test_bits:016X}",
                "ref_val": f"{ref_val:.17g}",
                "test_val": f"{test_val:.17g}",
                "ulp": ulp,
            }
        mismatches_by_cat[cat].append(info)

    # --- Summary ---
    total = len(common_ids)
    print(f"{'='*60}")
    print(f"COMPARISON SUMMARY")
    print(f"{'='*60}")
    print(f"  Common tests:    {total}")
    print(f"  Matching:        {matching} ({100*matching/total:.1f}%)" if total else "  Matching:        0")
    print(f"  Mismatched:      {mismatched} ({100*mismatched/total:.1f}%)" if total else "  Mismatched:      0")
    if nan_both:
        print(f"  NaN-both:        {nan_both} (counted as match)")
    print()

    if not mismatched:
        print("RESULT: BIT-EXACT MATCH")
        return 0

    # Breakdown by category
    print(f"MISMATCHES BY CATEGORY:")
    print(f"{'-'*60}")
    for cat in sorted(mismatches_by_cat.keys()):
        items = mismatches_by_cat[cat]
        ulps = [m["ulp"] for m in items if m.get("ulp") is not None]
        print(f"\n  [{cat}] {len(items)} mismatches")
        if ulps:
            print(f"    ULP range: {min(ulps)} .. {max(ulps)}")
            print(f"    ULP mean:  {sum(ulps)/len(ulps):.1f}")
            print(f"    ULP median: {sorted(ulps)[len(ulps)//2]}")

        # Show first 10 mismatches per category
        for m in items[:10]:
            ulp_str = f"ULP={m['ulp']}" if m.get("ulp") is not None else "ULP=N/A"
            print(f"    ID {m['id']:>6}  {m.get('op','?'):<14s}  "
                  f"ref={m.get('ref_hex','?')}  test={m.get('test_hex','?')}  "
                  f"{ulp_str}")
            if "ref_val" in m:
                print(f"           ref_val={m['ref_val']}  test_val={m['test_val']}")
        if len(items) > 10:
            print(f"    ... and {len(items) - 10} more")

    print()
    print(f"RESULT: {mismatched} MISMATCHES FOUND")
    return 1 if mismatched else 0


if __name__ == "__main__":
    sys.exit(main())
