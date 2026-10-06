#!/usr/bin/env python3
"""Check the canonical E1M1 field-1620, 32:9, FPS-disabled P6 capture.

This is one scene-specific image regression check, not a general seam detector.
No game image or ROM is part of the test source.
"""

import argparse
from dataclasses import dataclass
import math
from pathlib import Path
import sys


EXPECTED_SIZE = (682, 224)
SIDE_COLUMNS = range(230, 233)
CENTER_COLUMNS = range(233, 236)
FLOOR_ROWS = range(125, 135)
CEILING_LINE_COLUMNS = range(210, 221)
FLOOR_LINE_COLUMNS = range(210, 213)
# The visible native world is 216x144; PPU line 24 is output row 23.
CENTER_WORLD = (233, 23, 449, 167)  # right/bottom exclusive
WHITESPACE = b" \t\r\n\v\f"


@dataclass(frozen=True)
class Ppm:
    width: int
    height: int
    pixels: bytes

    def pixel(self, x, y):
        offset = (y * self.width + x) * 3
        return tuple(self.pixels[offset:offset + 3])


def read_ppm(path):
    """Read one 8-bit binary PPM, including comments between header tokens."""
    data = Path(path).read_bytes()
    offset = 0

    def token():
        nonlocal offset
        while offset < len(data):
            if data[offset] in WHITESPACE:
                offset += 1
            elif data[offset] == ord("#"):
                newline = data.find(b"\n", offset)
                if newline < 0:
                    raise ValueError("unterminated PPM header comment")
                offset = newline + 1
            else:
                break
        start = offset
        while offset < len(data) and data[offset] not in WHITESPACE + b"#":
            offset += 1
        if start == offset:
            raise ValueError("truncated PPM header")
        return data[start:offset]

    if token() != b"P6":
        raise ValueError("expected a binary P6 PPM from the presentation harness")
    try:
        width, height, maximum = int(token()), int(token()), int(token())
    except ValueError as error:
        raise ValueError("invalid PPM dimensions or maximum value") from error
    if width <= 0 or height <= 0 or maximum != 255:
        raise ValueError("expected positive dimensions and 8-bit RGB maximum 255")
    if offset >= len(data) or data[offset] not in WHITESPACE:
        raise ValueError("missing separator before the PPM raster")
    # Consume only the required separator; a raster byte may itself be whitespace.
    offset += 2 if data[offset:offset + 2] == b"\r\n" else 1
    pixels = data[offset:]
    expected_bytes = width * height * 3
    if len(pixels) != expected_bytes:
        raise ValueError(f"PPM raster has {len(pixels)} bytes; expected {expected_bytes}")
    return Ppm(width, height, pixels)


def require_fixture_size(image):
    if (image.width, image.height) != EXPECTED_SIZE:
        raise ValueError(
            f"this field-1620 fixture requires 682x224; got {image.width}x{image.height}"
        )


def floor_band(image, columns):
    rows = [[image.pixel(x, y) for x in columns] for y in FLOOR_ROWS]
    pixels = [pixel for row in rows for pixel in row]
    means = [sum(pixel[c] for pixel in pixels) / len(pixels) for c in range(3)]
    spread = [max(pixel[c] for pixel in pixels) - min(pixel[c] for pixel in pixels)
              for c in range(3)]
    row_means = [[sum(pixel[c] for pixel in row) / len(row) for c in range(3)]
                 for row in rows]
    return means, spread, row_means


def center_difference(image, reference):
    left, top, right, bottom = CENTER_WORLD
    changed_pixels = 0
    maximum_delta = 0
    for y in range(top, bottom):
        start = (y * image.width + left) * 3
        end = (y * image.width + right) * 3
        actual = image.pixels[start:end]
        expected = reference.pixels[start:end]
        if actual == expected:
            continue
        for x in range(0, len(actual), 3):
            delta = [abs(actual[x + c] - expected[x + c]) for c in range(3)]
            changed_pixels += int(any(delta))
            maximum_delta = max(maximum_delta, *delta)
    return changed_pixels, maximum_delta


def plane_row_check(image, name, row, reference_rows, columns, tolerance):
    """Compare strip means at equal dither parity; individual pixels can differ."""
    means = []
    for y in (row, *reference_rows):
        pixels = [image.pixel(x, y) for x in columns]
        means.append([sum(pixel[c] for pixel in pixels) / len(pixels)
                      for c in range(3)])
    actual, *references = means
    delta = [max(abs(actual[c] - reference[c]) for reference in references)
             for c in range(3)]
    passed = max(delta) <= tolerance
    status = "PASS" if passed else "FAIL"
    print(f"{status} {name} row{row}, x{columns.start}-{columns.stop - 1}, "
          f"reference rows{reference_rows}: RGB={actual}, "
          f"reference RGB={references}, max delta={delta}, tolerance={tolerance:g}")
    return passed


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="canonical 682x224 field-1620 P6 capture")
    parser.add_argument("--reference", type=Path,
                        help="same-fixture P6 capture whose entire center world must stay exact")
    parser.add_argument("--tolerance", type=float, default=2.0,
                        help="maximum per-channel seam/plane difference (default: 2 RGB levels)")
    args = parser.parse_args(argv)
    if not math.isfinite(args.tolerance) or not 0 <= args.tolerance <= 255:
        parser.error("--tolerance must be finite and between 0 and 255")
    try:
        image = read_ppm(args.image)
        require_fixture_size(image)
        reference = None
        if args.reference:
            reference = read_ppm(args.reference)
            require_fixture_size(reference)
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    side, side_spread, side_rows = floor_band(image, SIDE_COLUMNS)
    center, center_spread, center_rows = floor_band(image, CENTER_COLUMNS)
    row_delta = [max(abs(a[c] - b[c]) for a, b in zip(side_rows, center_rows))
                 for c in range(3)]
    flat = max(side_spread + center_spread) <= args.tolerance
    seam_passed = flat and max(row_delta) <= args.tolerance
    status = "PASS" if seam_passed else "FAIL"
    print(f"{status} floor seam x232/233, rows125-134: "
          f"side RGB={side}, center RGB={center}, "
          f"max row delta={row_delta}, tolerance={args.tolerance:g}")
    if not flat:
        print(f"FAIL fixture floor is not flat: side spread={side_spread}, "
              f"center spread={center_spread}; confirm the exact route, field and settings")

    ceiling_passed = plane_row_check(image, "ceiling", 35, (33, 37),
                                    CEILING_LINE_COLUMNS, args.tolerance)
    floor_passed = plane_row_check(image, "floor", 136, (134, 138),
                                  FLOOR_LINE_COLUMNS, args.tolerance)

    center_passed = True
    if reference:
        changed, maximum_delta = center_difference(image, reference)
        center_passed = changed == 0
        status = "PASS" if center_passed else "FAIL"
        print(f"{status} reference center world x233-448/y23-166: "
              f"{changed}/31104 pixels changed, maximum RGB delta={maximum_delta}")
    return 0 if seam_passed and ceiling_passed and floor_passed and center_passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
