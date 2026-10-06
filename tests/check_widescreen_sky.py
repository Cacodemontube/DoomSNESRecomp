#!/usr/bin/env python3
"""Check a 682x224 private E1M1 sky-scene export against native sky samples.

The replay probe supplies the real-core surface coverage mask and the common
panorama oracle. No game images or ROM data are part of the test source.
"""

import argparse
from pathlib import Path
import sys

from check_widescreen_shading import CENTER_WORLD, center_difference, read_ppm


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="composed scene P6 capture")
    parser.add_argument("--oracle", type=Path, required=True,
                        help="corrected probe's .sky-oracle.ppm")
    parser.add_argument("--mask", type=Path, required=True,
                        help="corrected probe's .sky-mask.ppm")
    parser.add_argument("--reference", type=Path,
                        help="native or before P6 image whose center world must stay exact")
    args = parser.parse_args(argv)
    try:
        image, oracle, mask = [read_ppm(path)
                               for path in (args.image, args.oracle, args.mask)]
        reference = read_ppm(args.reference) if args.reference else None
        for item in (image, oracle, mask, reference):
            if item and (item.width, item.height) != (682, 224):
                raise ValueError("this private E1M1 scene requires 682x224 images")
        left, top, right, bottom = CENTER_WORLD
        counts, changed, maximum = [0, 0], [0, 0], [0, 0]
        for y in range(image.height):
            for x in range(image.width):
                coverage = mask.pixel(x, y)
                if coverage == (0, 0, 0):
                    continue
                if coverage != (255, 255, 255):
                    raise ValueError("sky mask must contain only black/white coverage pixels")
                if not 20 <= x < 662 or not top <= y < bottom:
                    raise ValueError("sky coverage lies outside the composed world viewport")
                region = int(not left <= x < right)
                counts[region] += 1
                delta = [abs(a - b) for a, b in
                         zip(image.pixel(x, y), oracle.pixel(x, y))]
                changed[region] += int(any(delta))
                maximum[region] = max(maximum[region], *delta)
        if min(counts) < 500:
            raise ValueError("fixture must expose at least 500 native and 500 side sky pixels")
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    passed = True
    for name, count, difference, delta in zip(
            ("native", "side"), counts, changed, maximum):
        exact = difference == 0
        passed &= exact
        print(f"{'PASS' if exact else 'FAIL'} {name} common sky samples: "
              f"{difference}/{count} pixels changed, maximum RGB delta={delta}")
    if reference:
        difference, delta = center_difference(image, reference)
        exact = difference == 0
        passed &= exact
        print(f"{'PASS' if exact else 'FAIL'} reference center world x233-448/y23-166: "
              f"{difference}/31104 pixels changed, maximum RGB delta={delta}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
