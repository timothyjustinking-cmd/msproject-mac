#!/usr/bin/env python3
"""UI-parity measurement harness for the Qt shell (docs/parity.md).

The Qt shell must look like MS Project.  "Looks different" is not a bug report;
this script turns it into numbers.  It reads a reference PNG (MS Project's client
area, captured by ``scripts/msproj_bench.py``) and a candidate PNG (a Qt render
from ``app_qt --render``) and measures the same structural features in both:

    sheet column widths     vertical grid lines in the task sheet (px + fraction)
    row height              pitch of the horizontal row rules
    header height           height of the column-caption band
    splitter                x of the table/chart divider, as a fraction of width
    day cell width          pitch of the Gantt day cells (weekend shading or grid)
    timescale tiers         number of text tiers in the Gantt header
    dominant colours        header fill, grid line, weekend shade, task bar

Everything is measured from pixels only.  No text is read, no image is copied,
no MS Project UI string is quoted.  The reference stays where it is (the
git-ignored ``corpus/bench/``); this file never writes to it.

Usage
-----
    python scripts/parity_report.py REFERENCE.png [CANDIDATE.png]
    python scripts/parity_report.py REF.png CAND.png --json build/parity.json
    python scripts/parity_report.py --selftest
    python scripts/parity_report.py --compare REF.png CAND.png

With one image it prints the measured baseline and writes JSON on request.  With
two images (or ``--compare``) it prints the delta table and exits 1 if any
comparable measure is outside its tolerance.  ``--selftest`` renders synthetic
images under ``build/parity-selftest/`` with known geometry and asserts the
measured numbers to +-1 px, so the harness is proved without the reference.

The design rules are documented in ``docs/parity.md``: measures that are pixel
metrics (row height, header height, column widths, day width) are compared in
absolute pixels, because the Qt window is a smaller viewport, not a scaled
screenshot; measures that are layout proportions (splitter) and colours are
compared as fractions / RGB deltas.  A measure that either image cannot supply
is reported as not found rather than guessed.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SELFTEST_DIR = os.path.join(ROOT, "build", "parity-selftest")

# ---------------------------------------------------------------- constants --

# A pixel is "structural" (a grid line or border) when it is at least this much
# darker than the image background; "glyph" is text-level darkness.
STRUCT_DELTA = 16
SOFT_DELTA = 6
GLYPH_DELTA = 70

MIN_COLUMN_FRACTION = 0.80      # of the row band a vertical line must span
MIN_LINE_FRACTION = 0.35        # of the image height a long divider spans
MAX_LINE_WIDTH = 5              # grid lines are 1-3 px; anything wider is a band
MAX_SPLITTER_WIDTH = 8

# Comparison rules.  ``tolerance`` is in the unit of the measure.
MEASURES = [
    ("row_height_px", "row height", "abs", 1.0,
     "pixel metric: an integer row pitch; 1 px is rounding, 2 px is a choice"),
    ("header_height_px", "header height", "abs", 2.0,
     "pixel metric: caption band height; 2 px covers font-metric rounding"),
    ("splitter_fraction", "splitter x / width", "abs", 0.02,
     "layout proportion; 2% of width is narrower than the divider grip"),
    ("day_width_px", "Gantt day cell width", "abs", 2.0,
     "pixel metric: the timescale must be at the same zoom"),
    ("timescale_tier_count", "timescale tiers", "exact", 0.0,
     "integer: week+day = 2; a missing tier is a structural difference"),
    ("sheet_column_widths_px", "sheet column widths", "list_px", 4.0,
     "pixel metrics; only comparable when both images have the same columns"),
    ("colour.header", "header colour", "rgb", 10.0,
     "flat fill; 10/255 is below a visible step"),
    ("colour.grid", "grid colour", "rgb", 14.0,
     "1 px antialiased lines shift a few levels"),
    ("colour.weekend", "weekend colour", "rgb", 10.0,
     "flat fill"),
    ("colour.task_bar", "task bar colour", "rgb", 24.0,
     "bars are gradients/antialiased; wider tolerance"),
]


# ---------------------------------------------------------------- utilities --

def _runs(mask):
    """Return [(start, end_exclusive), ...] for the True runs of a 1-D mask."""
    m = np.concatenate(([False], np.asarray(mask, dtype=bool), [False]))
    edges = np.flatnonzero(np.diff(m.astype(np.int8)))
    return [(int(a), int(b)) for a, b in zip(edges[::2], edges[1::2])]


def _merge_runs(runs, gap=2):
    """Merge runs separated by at most ``gap`` pixels."""
    out = []
    for start, end in runs:
        if out and start - out[-1][1] <= gap:
            out[-1] = (out[-1][0], end)
        else:
            out.append((start, end))
    return out


def _packed(colours):
    colours = np.asarray(colours, dtype=np.uint32)
    return (colours[:, 0] << 16) | (colours[:, 1] << 8) | colours[:, 2]


def _unpack(value):
    value = int(value)
    return ((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF)


def _modal(colours):
    """Most common colour of an (N, 3) array -> ((r, g, b), share)."""
    colours = np.asarray(colours)
    if colours.size == 0:
        return None, 0.0
    packed = _packed(colours.reshape(-1, 3))
    values, counts = np.unique(packed, return_counts=True)
    index = int(np.argmax(counts))
    return _unpack(values[index]), float(counts[index]) / float(packed.size)


def _modal_of_region(img, y0, y1, x0, x1):
    y0, y1 = max(0, int(y0)), min(img.shape[0], int(y1))
    x0, x1 = max(0, int(x0)), min(img.shape[1], int(x1))
    if y1 - y0 < 1 or x1 - x0 < 1:
        return None
    return _modal(img[y0:y1, x0:x1])[0]


def _plain(value):
    """Convert numpy scalars/arrays to plain Python for json.dumps."""
    if isinstance(value, dict):
        return {key: _plain(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [_plain(item) for item in value]
    if isinstance(value, (np.integer,)):
        return int(value)
    if isinstance(value, (np.floating,)):
        return float(value)
    if isinstance(value, np.ndarray):
        return _plain(value.tolist())
    return value


def _colour_delta(a, b):
    """Largest per-channel absolute difference between two RGB triples."""
    return max(abs(int(x) - int(y)) for x, y in zip(a, b))


def load_rgb(path):
    """Read an image as an (h, w, 3) uint8 array, flattening alpha onto white."""
    image = Image.open(path)
    if image.mode in ("RGBA", "LA") or "transparency" in image.info:
        image = image.convert("RGBA")
        canvas = Image.new("RGBA", image.size, (255, 255, 255, 255))
        image = Image.alpha_composite(canvas, image)
    return np.asarray(image.convert("RGB"), dtype=np.uint8)


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


# ---------------------------------------------------------------- measures --

class Analysis:
    """Pixel measurements of one image.  Never raises on blank/odd images."""

    def __init__(self, path):
        self.path = os.path.abspath(path)
        self.img = load_rgb(path)
        self.height, self.width, _ = self.img.shape
        self.lum = (0.299 * self.img[:, :, 0]
                    + 0.587 * self.img[:, :, 1]
                    + 0.114 * self.img[:, :, 2]).astype(np.float32)
        self.background, self.background_share = _modal(
            self.img.reshape(-1, 3)[::1])
        self.bg_lum = float(0.299 * self.background[0]
                            + 0.587 * self.background[1]
                            + 0.114 * self.background[2])
        self.sat = (self.img.max(axis=2).astype(np.int16)
                    - self.img.min(axis=2).astype(np.int16))

        self.struct = self.lum < self.bg_lum - STRUCT_DELTA
        self.soft = self.lum < self.bg_lum - SOFT_DELTA
        self.glyph = self.lum < self.bg_lum - GLYPH_DELTA

        self.notes = []
        self.measures = {}
        self.layout = {}

        self.bands = []
        self.vlines = []
        self.soft_lines = []
        if self.width < 24 or self.height < 24:
            # Too small to contain a sheet, a header and a chart.  Say so
            # instead of guessing from a handful of pixels.
            self.notes.append(
                f"image is {self.width}x{self.height}; too small to measure")
            for key, unit in (("row_height_px", "px"),
                              ("header_height_px", "px"),
                              ("splitter_fraction", "fraction of image width"),
                              ("day_width_px", "px"),
                              ("sheet_column_widths_px", "px"),
                              ("timescale_tier_count", "count"),
                              ("colour.header", "rgb"), ("colour.grid", "rgb"),
                              ("colour.weekend", "rgb"),
                              ("colour.task_bar", "rgb")):
                self._set(key, None, unit, found=False)
            return

        self._find_vertical_structures()
        self._find_shaded_bands()
        self._find_day_width()
        self._find_splitter()
        self._find_rules_and_header()
        self._find_sheet_columns()
        self._refine_header_top()
        self._find_timescale()
        self._find_colours()

    # -- helpers -----------------------------------------------------------

    def _set(self, key, value, unit, found=True):
        self.measures[key] = {"found": bool(found), "unit": unit,
                              "value": value}
        note = f"{key}: not found"
        self.notes = [item for item in self.notes if item != note]
        if not found:
            self.notes.append(note)

    def _vertical_structures(self, mask, min_fraction, max_width):
        fraction = mask.mean(axis=0)
        out = []
        for start, end in _runs(fraction >= min_fraction):
            if end - start > max_width:
                continue
            weights = fraction[start:end] + 1e-6
            centre = float(np.average(np.arange(start, end), weights=weights))
            out.append({"x": centre, "width": int(end - start),
                        "span": float(weights.max())})
        return out

    def _find_vertical_structures(self):
        # Lines that run most of the image: grid lines, borders, splitter.
        self.vlines = self._vertical_structures(
            self.struct, MIN_LINE_FRACTION, MAX_LINE_WIDTH)
        # Lighter dividers (a Fusion splitter handle can be 239 on 255).
        self.soft_lines = self._vertical_structures(
            self.soft, MIN_LINE_FRACTION, MAX_SPLITTER_WIDTH)

    # -- Gantt shading / day width ----------------------------------------

    def _find_shaded_bands(self):
        shade = ((self.lum <= self.bg_lum - 5)
                 & (self.lum >= self.bg_lum - 30)
                 & (self.sat < 14))
        self.shade = shade
        fraction = shade.mean(axis=0)
        runs = _merge_runs(_runs(fraction >= 0.35), gap=2)
        bands = []
        for start, end in runs:
            width = end - start
            if width < 5 or width > 0.20 * self.width:
                continue
            # Sample only the shaded pixels: the band columns also contain the
            # unshaded header and the scrollbar strip above/below the chart.
            pixels = self.img[:, start:end][shade[:, start:end]]
            colour, _ = _modal(pixels)
            bands.append({"start": start, "end": end, "width": width,
                          "colour": list(colour) if colour else None})
        self.bands = bands
        self.layout["weekend_bands_px"] = [[b["start"], b["end"]] for b in bands]

    def _band_period(self):
        """Weekend period from the *ends* of the shaded bands (clipping-safe)."""
        if len(self.bands) < 2:
            return None
        ends = [b["end"] - 1 for b in self.bands]
        diffs = [b - a for a, b in zip(ends, ends[1:]) if b > a]
        if not diffs:
            return None
        median = float(np.median(diffs))
        candidates = [d for d in diffs if d <= 1.5 * median]
        return float(max(candidates)) if candidates else None

    def _gantt_line_pitch(self):
        """Smallest repeated spacing of full-height lines in the chart."""
        right = [line["x"] for line in self.vlines
                 if self.layout.get("chart_left_px") is not None
                 and line["x"] >= self.layout["chart_left_px"] + 2]
        if len(right) < 5:
            return None
        diffs = [int(round(b - a)) for a, b in zip(right, right[1:])]
        candidates = [d for d in diffs if 6 <= d <= 60]
        if len(candidates) < 4:
            return None
        values, counts = np.unique(candidates, return_counts=True)
        index = int(np.argmax(counts))
        if counts[index] < 4:
            return None
        return float(values[index])

    def _find_day_width(self):
        period = self._band_period()
        day = None
        source = None
        if period:
            day = period / 7.0
            source = "weekend bands"
            # A full weekend band is two days wide; use it as a sanity check.
            full = [b["width"] for b in self.bands
                    if abs(b["width"] - 2 * day) <= max(3.0, 0.15 * day)]
            if not full:
                self.notes.append(
                    "weekend band width does not match a 2-day weekend; "
                    "day width is from band periodicity only")
        if day is None:
            day = self._gantt_line_pitch()
            source = "Gantt grid lines" if day else None
        if day is None or not (4.0 <= day <= 80.0):
            self._set("day_width_px", None, "px", found=False)
            self._set("day_width_fraction", None, "fraction", found=False)
            return

        self.day_width = float(day)
        self.day_source = source
        self._set("day_width_px", round(float(day), 2), "px")
        self._set("day_width_fraction", round(float(day) / self.width, 5),
                  "fraction of image width")
        self.layout["day_width_source"] = source

    def _chart_left(self):
        """Left edge of the Gantt canvas, from the first shaded band."""
        day = getattr(self, "day_width", None)
        if not self.bands or not day:
            return None
        first = self.bands[0]
        if first["width"] < 1.6 * day:
            edge = float(first["start"])          # clipped by the chart edge
        else:
            edge = float(first["start"]) - 5.0 * day  # five weekdays before
        # Snap to the chart's own border line if one is within 2 px.
        nearby = [line["x"] for line in self.vlines
                  if abs(line["x"] - edge) <= 2.0]
        if nearby:
            edge = min(nearby, key=lambda x: abs(x - edge))
        return edge

    # -- splitter ----------------------------------------------------------

    def _find_splitter(self):
        chart_left = self._chart_left()
        self.layout["chart_left_px"] = (round(chart_left, 2)
                                        if chart_left is not None else None)
        splitter = None
        sheet_right = None
        if chart_left is not None:
            # A light divider (e.g. a Fusion grip) may only clear SOFT_DELTA;
            # strong grid lines are preferred because a shaded band merges with
            # a light divider into one wide run and is then discarded.
            candidates = list(self.vlines)
            for line in self.soft_lines:
                if not any(abs(line["x"] - other["x"]) < 1.0
                           for other in candidates):
                    candidates.append(line)
            window = [line["x"] for line in candidates
                      if chart_left - 10.0 <= line["x"] <= chart_left + 2.0]
            if window:
                splitter = (min(window) + max(window)) / 2.0
                sheet_right = min(window)
            else:
                splitter = chart_left
                sheet_right = chart_left
        else:
            # No shading (e.g. shading switched off): the widest gap between
            # long vertical lines is the table/chart boundary.
            xs = [line["x"] for line in self.vlines]
            best = None
            for left, right in zip(xs, xs[1:]):
                gap = right - left
                if gap >= 0.20 * self.width and left <= 0.9 * self.width:
                    if best is None or gap > best[2]:
                        best = (left, right, gap)
            if best:
                splitter = best[0]
                sheet_right = best[0]
                self.notes.append("splitter inferred from the widest line gap "
                                  "(no weekend shading)")
            elif xs:
                splitter = max(xs)
                sheet_right = splitter
                self.notes.append("splitter inferred from the rightmost line "
                                  "(no weekend shading)")

        if splitter is None:
            self._set("splitter_fraction", None, "fraction", found=False)
            self.layout["splitter_px"] = None
            self.layout["sheet_right_px"] = None
            return

        self.splitter = float(splitter)
        self.sheet_right = float(sheet_right)
        self.layout["splitter_px"] = round(self.splitter, 2)
        self.layout["sheet_right_px"] = round(self.sheet_right, 2)
        self._set("splitter_fraction", round(self.splitter / self.width, 5),
                  "fraction of image width")
        self._set("splitter_px", round(self.splitter, 2), "px")

    # -- rules, header, rows ----------------------------------------------

    @staticmethod
    def _thin_runs(rule, profile):
        """Keep only the darkest rows of a candidate run when it has contrast.

        A header fill can pass the same uniformity/dip tests as a border at its
        edges; only the border rows (clearly darker than the fill) survive.  A
        genuine 1-2 px rule has no such contrast and is kept whole.
        """
        out = np.zeros_like(rule)
        for start, end in _runs(rule):
            rows = profile[start:end]
            darkest = float(rows.min())
            if float(np.median(rows)) - darkest < 8.0:
                out[start:end] = True
                continue
            out[start:end] = rows <= darkest + 6.0
        return out

    def _find_rules_and_header(self):
        sheet_right = self.layout.get("sheet_right_px")
        if sheet_right is None:
            sheet_right = self.width * 0.5
        x0 = int(max(1, 0.05 * self.width))
        x1 = int(max(x0 + 4, sheet_right - 4))
        # A rule is: uniform across the sheet width (median-based, so a selection
        # border crossing one row does not disqualify it), darker than the
        # background, and a dip against the row 3 px above or below.  A tinted
        # header fill also dips once at its top edge, so runs taller than a
        # border are reduced to their darkest rows (the border) or dropped.
        segment = self.lum[:, x0:x1]
        median_profile = np.median(segment, axis=1)
        near = (np.abs(segment - median_profile[:, None]) <= 8).mean(axis=1) >= 0.9
        rule = np.zeros(self.height, dtype=bool)
        if self.height > 8:
            dip = ((median_profile[3:self.height - 3] + 6
                    <= median_profile[0:self.height - 6])
                   | (median_profile[3:self.height - 3] + 6
                      <= median_profile[6:self.height]))
            rule[3:self.height - 3] = (dip
                                       & near[3:self.height - 3]
                                       & (median_profile[3:self.height - 3]
                                          <= self.bg_lum - 8))
        rule = self._thin_runs(rule, median_profile)
        groups = _merge_runs(_runs(rule), gap=4)
        ys = [start for start, _ in groups]
        diffs = [b - a for a, b in zip(ys, ys[1:])]
        usable = [int(round(d)) for d in diffs if 8 <= d <= 80]

        def longest_chain(pitch):
            best_length, best_start = 0, 0
            index = 0
            while index < len(ys):
                cursor = index
                while (cursor + 1 < len(ys)
                       and abs((ys[cursor + 1] - ys[cursor]) - pitch) <= 1):
                    cursor += 1
                if cursor - index + 1 > best_length:
                    best_length, best_start = cursor - index + 1, index
                if cursor == index:
                    index += 1
                else:
                    index = cursor + 1
            return best_length, best_start

        # Pick the spacing that explains the most rules; ties go to the one that
        # occurs most often.  A 3-row table only offers three gaps, so a mode
        # with a minimum count would reject it -- hence the chain search.
        pitch = None
        best_score = None
        for candidate in sorted(set(usable)):
            length, start = longest_chain(candidate)
            score = (length, usable.count(candidate))
            if best_score is None or score > best_score:
                best_score, pitch = score, candidate
        if pitch is not None and best_score[0] < 3:
            pitch = None
        if pitch is None:
            self._set("row_height_px", None, "px", found=False)
            self._set("header_height_px", None, "px", found=False)
            self.layout["row_pitch_px"] = None
            self.rules = groups
            return

        # The body is the longest chain of evenly spaced rules.
        best_len, chain_start = longest_chain(pitch)
        if best_len < 3:
            self._set("row_height_px", None, "px", found=False)
            self._set("header_height_px", None, "px", found=False)
            self.layout["row_pitch_px"] = None
            self.rules = groups
            return

        body_top = ys[chain_start]
        body_bottom = ys[chain_start + best_len - 1]
        # The header sits directly above the body; its own top rule is the
        # nearest rule 1.2-3.5 rows above the body top.
        header_top = body_top
        for start, end in reversed(groups[:chain_start]):
            gap = body_top - start
            if 1.2 * pitch <= gap <= 3.5 * pitch:
                header_top = start
                break
        else:
            if body_top <= 2.0 * pitch:
                header_top = 0

        self.pitch = pitch
        self.body_top = body_top
        self.body_bottom = body_bottom
        self.header_top = header_top
        self.header_bottom = body_top
        self._set("row_height_px", int(pitch), "px")
        self._set("row_height_fraction", round(pitch / self.height, 5),
                  "fraction of image height")
        if header_top < body_top:
            self._set("header_height_px", int(body_top - header_top), "px")
        else:
            self._set("header_height_px", None, "px", found=False)
        self.layout.update({
            "header_top_px": int(header_top),
            "header_bottom_px": int(body_top),
            "body_top_px": int(body_top),
            "body_bottom_px": int(body_bottom),
            "row_pitch_px": int(pitch),
        })

    # -- sheet columns -----------------------------------------------------

    def _find_sheet_columns(self):
        if "sheet_right_px" not in self.layout or self.layout["sheet_right_px"] is None:
            self._set("sheet_column_widths_px", None, "px", found=False)
            self._set("sheet_column_widths_fraction", None, "fraction", found=False)
            self.layout["column_count"] = None
            return
        right = self.layout["sheet_right_px"]
        top = self.layout.get("body_top_px")
        bottom = self.layout.get("body_bottom_px")
        if top is None or bottom is None or bottom - top < 8:
            top, bottom = int(0.25 * self.height), int(0.85 * self.height)

        # A grid line must differ from the row's own background: the selected
        # row is filled with a tint, and a line inside it is lighter than the
        # fill, not darker than white.  The row median is that background.
        rows = self.img[int(top):int(bottom)].astype(np.int16)
        row_background = np.median(rows, axis=1).astype(np.int16)
        difference = np.abs(rows - row_background[:, None, :]).max(axis=2)
        fraction = (difference >= STRUCT_DELTA).mean(axis=0)
        boundaries = []
        for start, end in _runs(fraction >= MIN_COLUMN_FRACTION):
            if end - start > MAX_LINE_WIDTH:
                continue
            centre = float(np.average(np.arange(start, end),
                                      weights=fraction[start:end] + 1e-6))
            if centre < 2.0 or centre > right - 1.0:
                continue
            boundaries.append(centre)
        boundaries.sort()

        # The table's own left border is the first detected grid line.  A table
        # flush with the image edge draws no left border, so its first column
        # (left of the first line) is deliberately not counted.
        sheet_left = boundaries[0] if boundaries else 0.0
        inner = boundaries[1:] if boundaries else []
        inner = [b for b in inner if b > sheet_left + 4.0 and b < right - 4.0]

        edges = [sheet_left] + inner + [float(right)]
        widths = [round(b - a, 2) for a, b in zip(edges, edges[1:])]
        span = float(right) - sheet_left
        if not widths or span <= 0:
            self._set("sheet_column_widths_px", None, "px", found=False)
            self._set("sheet_column_widths_fraction", None, "fraction", found=False)
            self.layout["column_count"] = None
            return

        self.sheet_left = sheet_left
        self._set("sheet_column_widths_px", widths, "px")
        self._set("sheet_column_widths_fraction",
                  [round(w / span, 5) for w in widths],
                  "fraction of sheet width")
        self.layout["sheet_left_px"] = round(sheet_left, 2)
        self.layout["column_count"] = len(widths)
        self.layout["sheet_column_boundaries_px"] = [round(e, 2) for e in edges]
        self.column_edges = boundaries

    def _line_start(self, x):
        """Top of the longest contiguous dark run at a grid-line column."""
        column = int(round(x))
        strip = self.struct[:, max(0, column - 1):column + 2].any(axis=1)
        runs = _runs(strip)
        if not runs:
            return None
        longest = max(runs, key=lambda run: run[1] - run[0])
        if longest[1] - longest[0] < 8:
            return None
        return longest[0]

    def _refine_header_top(self):
        """The grid lines start at the header's top rule; use that, not a rule.

        A tinted header fill makes the top rule and the fill one contiguous dark
        run, so the rule scan cannot separate them.  The vertical grid lines
        always start at the header top, which is unambiguous.
        """
        boundaries = getattr(self, "column_edges", None)
        body_top = self.layout.get("body_top_px")
        if not boundaries or body_top is None:
            return
        starts = [start for start in (self._line_start(x) for x in boundaries)
                  if start is not None]
        if not starts:
            return
        candidate = int(round(float(np.median(starts))))
        pitch = self.layout.get("row_pitch_px") or 0
        current = self.layout.get("header_top_px")
        if current is None:
            current = body_top
        # Only extend the header upwards: if a top rule was found, the grid
        # lines cannot start above it.
        if (0 <= candidate < current
                and (body_top - candidate) >= max(6, pitch)):
            self.layout["header_top_px"] = candidate
            self._set("header_height_px", int(body_top - candidate), "px")

    # -- timescale ---------------------------------------------------------

    def _find_timescale(self):
        left = self.layout.get("chart_left_px")
        top = self.layout.get("header_top_px")
        bottom = self.layout.get("header_bottom_px")
        if left is None or top is None or bottom is None or bottom - top < 6:
            self._set("timescale_tier_count", None, "count", found=False)
            self._set("timescale_rules_px", None, "px", found=False)
            return
        x0 = int(min(self.width - 2, left + 2))
        x1 = int(self.width - 2)
        if x1 - x0 < 20:
            self._set("timescale_tier_count", None, "count", found=False)
            self._set("timescale_rules_px", None, "px", found=False)
            return
        band = slice(int(top), int(bottom) + 1)      # include the bottom rule
        rules_fraction = self.struct[band, x0:x1].mean(axis=1)
        rule_rows = [int(top) + i for i in np.flatnonzero(rules_fraction >= 0.60)]
        self.layout["timescale_rules_px"] = rule_rows
        self._set("timescale_rules_px", rule_rows, "px")

        text = slice(int(top), int(bottom))
        glyph_fraction = self.glyph[text, x0:x1].mean(axis=1)
        # A horizontal rule is ~100% dark in this band; tier labels are a
        # fraction of the width, so rules must not set the "text" threshold.
        text_like = glyph_fraction <= 0.50
        peak = float(glyph_fraction[text_like].max()) if text_like.any() else 0.0
        # Day tick marks are <1% of the width; a tier's labels are several
        # percent.  The floor also keeps a short label from being missed.
        floor = max(0.012, 0.05 * peak)
        text_rows = text_like & (glyph_fraction >= floor)
        clusters = _merge_runs(_runs(text_rows), gap=3)
        clusters = [(a, b) for a, b in clusters if b - a >= 3]
        if clusters:
            self._set("timescale_tier_count", len(clusters), "count")
        else:
            self._set("timescale_tier_count", None, "count", found=False)

    # -- colours -----------------------------------------------------------

    def _find_colours(self):
        # Header fill: the modal colour inside the caption band.
        header = None
        if "header_top_px" in self.layout and "header_bottom_px" in self.layout:
            left = self.layout.get("sheet_left_px", 0) or 0
            right = self.layout.get("sheet_right_px") or 0
            top = self.layout["header_top_px"] + 2
            bottom = self.layout["header_bottom_px"] - 2
            if right - left > 20 and bottom - top >= 3:
                header = _modal_of_region(self.img, top, bottom,
                                          left + 2, right - 2)
        self._set("colour.header", list(header) if header else None, "rgb",
                  found=header is not None)

        # Grid: modal colour of the detected line pixels in the sheet body.
        grid = None
        boundaries = self.layout.get("sheet_column_boundaries_px") or []
        top = self.layout.get("body_top_px")
        bottom = self.layout.get("body_bottom_px")
        if boundaries and top is not None and bottom is not None:
            samples = []
            for x in boundaries:
                column = int(round(x))
                if 0 <= column < self.width:
                    column_pixels = self.img[int(top):int(bottom), column]
                    mask = self.struct[int(top):int(bottom), column]
                    samples.append(column_pixels[mask])
            if samples:
                grid, _ = _modal(np.concatenate(samples))
        self._set("colour.grid", list(grid) if grid else None, "rgb",
                  found=grid is not None)

        weekend = None
        if self.bands:
            pixels = [self.img[:, b["start"]:b["end"]].reshape(-1, 3)
                      for b in self.bands]
            weekend, _ = _modal(np.concatenate(pixels))
        self._set("colour.weekend", list(weekend) if weekend else None, "rgb",
                  found=weekend is not None)

        bar = self._find_bar_colour()
        self._set("colour.task_bar", list(bar) if bar else None, "rgb",
                  found=bar is not None)

    def _find_bar_colour(self):
        """Dominant colour of long saturated horizontal runs in the chart body.

        Isolated saturated pixels are text antialiasing or a 1 px status line;
        a task bar is a filled run at least ~2 day cells wide and 3 rows tall.
        """
        left = self.layout.get("chart_left_px")
        top = self.layout.get("body_top_px")
        if left is None or top is None:
            return None
        x0 = int(left) + 1
        x1 = int(self.width) - 2
        if x1 - x0 < 10:
            return None
        body = self.img[int(top):int(min(self.height, int(top) + 400)), x0:x1]
        sat = body.max(axis=2).astype(np.int16) - body.min(axis=2).astype(np.int16)
        saturated = sat >= 40
        min_run = max(12, int(2.0 * getattr(self, "day_width", 10.0)))
        # A bar carries its label INSIDE the fill in both products, so the
        # saturated run is interrupted by glyph pixels.  Wave 24 (W24-11):
        # merge runs separated by a small gap (half a day cell) and measure the
        # merged span; without this, a labelled 2-day bar (the port draws one)
        # was never found and the colour check silently skipped.
        gap = max(4, int(getattr(self, "day_width", 10.0) * 0.5))
        mask = np.zeros(saturated.shape, dtype=bool)
        for row in range(saturated.shape[0]):
            spans = []
            for start, end in _runs(saturated[row]):
                if spans and start - spans[-1][1] <= gap:
                    spans[-1] = (spans[-1][0], end)
                else:
                    spans.append((start, end))
            for start, end in spans:
                if end - start >= min_run:
                    mask[row, start:end] = True
        # Require a few consecutive rows so a drawn status line cannot qualify.
        keep = np.zeros(mask.shape, dtype=bool)
        for column in range(mask.shape[1]):
            for start, end in _runs(mask[:, column]):
                if end - start >= 3:
                    keep[start:end, column] = True
        if not keep.any():
            return None
        pixels = body[keep]
        colour, share = _modal(pixels)
        if share < 0.05:
            return None
        return colour

    # -- summary -----------------------------------------------------------

    def as_dict(self):
        return _plain({
            "image": {
                "path": self.path,
                "width": int(self.width),
                "height": int(self.height),
                "sha256": sha256_of(self.path),
            },
            "background": list(self.background),
            "layout": self.layout,
            "measures": self.measures,
            "notes": self.notes,
        })


# --------------------------------------------------------------- comparison --

class JsonReference:
    """A reference supplied as parity_report's own JSON (wave 23).

    The operator's ruling keeps the Project screenshot local and allows only the
    NUMBERS measured from it in the repository, so CI compares the render to
    this committed measurement file instead of a PNG.  `compare()` only needs
    `.measures`.
    """

    def __init__(self, report):
        self.measures = report.get("measures", {})
        self.path = report.get("image", {}).get("path", "(committed measures)")
        self.width = report.get("image", {}).get("width", 0)
        self.height = report.get("image", {}).get("height", 0)


def _measure_meta(key):
    for item in MEASURES:
        if item[0] == key:
            return item
    return None


def compare(reference, candidate, keys=None):
    """Build the delta table.  Returns (rows, summary).

    `keys` restricts the comparison to the named measures (wave 25: the
    auto-scheduled reference exists to pin ONE colour, and its FileNew-descended
    view geometry is not part of that claim).  None compares every measure.
    """
    rows = []
    comparable = 0
    failures = 0
    missing = 0
    for key, label, kind, tolerance, why in MEASURES:
        if keys is not None and key not in keys:
            continue
        ref_measure = reference.measures.get(key, {})
        cand_measure = candidate.measures.get(key, {})
        ref_found = bool(ref_measure.get("found"))
        cand_found = bool(cand_measure.get("found"))
        row = {"key": key, "label": label, "kind": kind,
               "tolerance": tolerance, "why": why,
               "reference": ref_measure.get("value"),
               "candidate": cand_measure.get("value")}

        if not ref_found and not cand_found:
            row.update(verdict="N/A", delta=None,
                       note="neither image supplies this measure")
            rows.append(row)
            continue
        if not ref_found:
            row.update(verdict="N/A", delta=None,
                       note="reference has no such feature; nothing to match")
            rows.append(row)
            continue
        if not cand_found:
            missing += 1
            row.update(verdict="FAIL", delta=None,
                       note="candidate does not show this feature")
            rows.append(row)
            continue

        if kind == "list_px":
            ref_list = ref_measure["value"]
            cand_list = cand_measure["value"]
            if len(ref_list) != len(cand_list):
                row.update(verdict="N/A", delta=None,
                           note=f"incomparable: {len(ref_list)} columns in "
                                f"the reference, {len(cand_list)} in the "
                                f"candidate")
                rows.append(row)
                continue
            deltas = [round(c - r, 2) for r, c in zip(ref_list, cand_list)]
            worst = max(abs(d) for d in deltas) if deltas else 0.0
            delta = worst
            ok = worst <= tolerance
            note = "per-column deltas: " + ", ".join(f"{d:+.0f}" for d in deltas)
        elif kind == "rgb":
            delta = _colour_delta(ref_measure["value"], cand_measure["value"])
            ok = delta <= tolerance
            note = ""
        elif kind == "exact":
            delta = int(cand_measure["value"]) - int(ref_measure["value"])
            ok = delta == 0
            note = ""
        else:  # abs
            delta = round(float(cand_measure["value"])
                          - float(ref_measure["value"]), 4)
            ok = abs(delta) <= tolerance
            note = ""

        comparable += 1
        if not ok:
            failures += 1
        row.update(verdict="PASS" if ok else "FAIL", delta=delta, note=note)
        rows.append(row)

    summary = {"comparable": comparable, "failures": failures,
               "missing": missing,
               "verdict": "PASS" if (failures == 0 and comparable > 0)
               else ("FAIL" if failures or missing else "NO-COMPARABLE")}
    return rows, summary


# ------------------------------------------------------------------ output --

def _fmt(value, unit):
    if value is None:
        return "-"
    if unit == "rgb":
        return "({},{},{})".format(*value)
    if isinstance(value, list):
        if len(value) > 12:
            return "[" + ", ".join(f"{v:g}" for v in value[:8]) + ", ...]"
        return "[" + ", ".join(f"{v:g}" for v in value) + "]"
    if unit == "count":
        return str(int(value))
    if isinstance(value, float):
        return f"{value:g}"
    return str(value)


def print_report(report):
    image = report["image"]
    print(f"image      {image['path']}")
    print(f"size       {image['width']} x {image['height']}   "
          f"sha256 {image['sha256'][:16]}")
    print(f"background ({report['background'][0]},{report['background'][1]},"
          f"{report['background'][2]})")
    layout = report["layout"]
    print()
    print(f"{'measure':26s} {'value':>44s}  {'fraction':>10s}")
    print("-" * 84)
    for key in ("sheet_column_widths_px", "row_height_px", "header_height_px",
                "splitter_fraction", "day_width_px", "timescale_tier_count"):
        measure = report["measures"].get(key, {})
        if not measure.get("found"):
            print(f"{key:26s} {'not found':>44s}")
            continue
        fraction = ""
        fraction_key = {"sheet_column_widths_px": "sheet_column_widths_fraction",
                        "row_height_px": "row_height_fraction",
                        "splitter_fraction": None,
                        "day_width_px": "day_width_fraction"}.get(key)
        if fraction_key:
            values = report["measures"].get(fraction_key, {}).get("value")
            if isinstance(values, list):
                fraction = "per column"
            elif values is not None:
                fraction = f"{values:.3f}"
        elif key == "splitter_fraction":
            fraction = f"{measure['value']:.3f}"
        print(f"{key:26s} {_fmt(measure['value'], measure.get('unit', '')):>44s}"
              f"  {fraction:>10s}")
    print()
    for key in ("colour.header", "colour.grid", "colour.weekend",
                "colour.task_bar"):
        measure = report["measures"].get(key, {})
        value = _fmt(measure.get("value"), "rgb") if measure.get("found") else "not found"
        print(f"{key:26s} {value:>44s}")
    if report.get("notes"):
        print()
        for note in report["notes"]:
            print(f"note: {note}")


def print_compare(rows, summary, reference, candidate):
    print(f"reference  {reference.path}  ({reference.width}x{reference.height})")
    print(f"candidate  {candidate.path}  ({candidate.width}x{candidate.height})")
    print()
    header = (f"{'measure':24s} {'reference':>14s} {'candidate':>14s} "
              f"{'delta':>10s} {'tol':>7s}  verdict")
    print(header)
    print("-" * len(header))
    for row in rows:
        unit = row["kind"]
        ref_text = _fmt(row["reference"], "rgb" if unit == "rgb" else
                        ("count" if unit == "exact" else ""))
        cand_text = _fmt(row["candidate"], "rgb" if unit == "rgb" else
                         ("count" if unit == "exact" else ""))
        if row["kind"] == "list_px":
            ref_text = f"{len(row['reference'])} cols" if row["reference"] else "-"
            cand_text = f"{len(row['candidate'])} cols" if row["candidate"] else "-"
        delta = row.get("delta")
        if isinstance(delta, float):
            delta_text = f"{delta:+.3f}"
        elif delta is None:
            delta_text = "-"
        else:
            delta_text = f"{delta:+d}"
        tolerance = row["tolerance"]
        tolerance_text = f"{tolerance:g}" if tolerance else "exact"
        print(f"{row['label']:24s} {ref_text:>14s} {cand_text:>14s} "
              f"{delta_text:>10s} {tolerance_text:>7s}  {row['verdict']}")
        if row.get("note"):
            print(f"{'':24s} {row['note']}")
    print()
    print(f"{summary['comparable']} comparable measure(s), "
          f"{summary['failures']} failing, {summary['missing']} missing")
    print(f"verdict: {summary['verdict']}")


# ----------------------------------------------------------------- selftest --

def _fill(img, x0, y0, x1, y1, colour):
    img[max(0, y0):max(0, y1), max(0, x0):max(0, x1)] = np.array(colour, dtype=np.uint8)


def _make_synthetic(path, width, height, first_column, columns, last_right,
                    header_top, header_bottom, pitch, rows, day, palette):
    """Draw a fake sheet + Gantt with exactly known geometry.

    ``first_column`` and ``columns`` are the vertical grid lines of the table,
    ``last_right`` is the right edge of the last column (the divider's left
    edge); the Gantt starts three pixels later.  The truth returned is in the
    same units the analyser reports.
    """
    img = np.full((height, width, 3), 255, dtype=np.uint8)
    grid = np.array(palette["grid"], dtype=np.uint8)
    header = palette["header"]
    weekend = palette["weekend"]
    text = (60, 60, 60)

    sheet_right = int(last_right)
    chart_left = sheet_right + 3
    body_bottom = header_bottom + rows * pitch

    # Header band and its fill.
    _fill(img, int(first_column), header_top, sheet_right, header_bottom, header)
    # Vertical column boundaries, from the header top to the last row rule.
    for x in columns:
        _fill(img, int(x), header_top, int(x) + 1, body_bottom + 1, grid)
    # Left border and the 2 px divider (a splitter spans the whole view).
    _fill(img, int(first_column), header_top, int(first_column) + 1,
          body_bottom + 1, grid)
    _fill(img, sheet_right, header_top, sheet_right + 2, height - 1, grid)
    # Horizontal rules: header top/bottom (full width, like the real header)
    # and every row rule (which in the reference spans the chart too).
    _fill(img, int(first_column), header_top, width, header_top + 1, grid)
    _fill(img, 0, header_bottom, width, header_bottom + 1, grid)
    for row in range(1, rows + 1):
        y = header_bottom + row * pitch
        _fill(img, 0, y, width, y + 1, grid)
    # Caption "glyphs" and cell text (they must not be mistaken for rules).
    for x in columns[:-1]:
        _fill(img, int(x) + 8, header_top + 10, int(x) + 10 + 14,
              header_top + 18, text)
    for row in range(min(rows, 4)):
        _fill(img, int(first_column) + 8, header_bottom + row * pitch + 6,
              int(first_column) + 50, header_bottom + row * pitch + 12, text)

    # Gantt: shaded weekend bands, period 7 days, first band clipped by the edge.
    period = 7 * day
    band_end = chart_left + int(day) - 1               # partial first weekend
    _fill(img, chart_left, header_top + 1, band_end, height - 2, weekend)
    position = band_end + 5 * int(day)                 # next weekend
    while position < width:
        _fill(img, position, header_top + 1,
              min(width, position + 2 * int(day)), height - 2, weekend)
        position += period
    # Two timescale tiers: a week label bar and day tick marks below it.
    _fill(img, chart_left + 4, header_top + 6, chart_left + 60,
          header_top + 12, text)
    for tick in range(0, 6):
        x = chart_left + tick * int(day) + int(day) - 1
        _fill(img, x, header_top + 22, x + 1, header_bottom - 2, grid)
    _fill(img, chart_left + 8, header_top + 22, chart_left + 18,
          header_top + 28, text)
    # A task bar in the first Gantt row.
    bar_x0 = chart_left + 20
    bar_x1 = min(width - 4, bar_x0 + int(4 * day))
    _fill(img, bar_x0, header_bottom + 4, bar_x1, header_bottom + 12,
          palette["bar"])

    os.makedirs(os.path.dirname(path), exist_ok=True)
    Image.fromarray(img).save(path)
    return {
        "column_widths": ([int(b - a) for a, b in zip(columns, columns[1:])]
                          + [sheet_right - int(columns[-1])]),
        "row_height": pitch,
        "header_height": header_bottom - header_top,
        "splitter_fraction": (sheet_right + 0.5) / width,
        "day_width": day,
        "tier_count": 2,
        "palette": palette,
    }


def _assert_close(label, got, want, tolerance, failures):
    if got is None:
        failures.append(f"{label}: not found (want {want})")
        return
    if isinstance(want, (list, tuple)):
        if not isinstance(got, (list, tuple)) or len(got) != len(want):
            failures.append(f"{label}: got {got}, want {list(want)}")
            return
        bad = [(g, w) for g, w in zip(got, want) if abs(float(g) - float(w)) > tolerance]
        if bad:
            failures.append(f"{label}: got {list(got)}, want {list(want)} "
                            f"(+-{tolerance}); off: {bad}")
        else:
            print(f"  ok  {label}: {list(got)} (want {list(want)} +-{tolerance})")
        return
    if abs(float(got) - float(want)) > tolerance:
        failures.append(f"{label}: got {got}, want {want} (+-{tolerance})")
    else:
        print(f"  ok  {label}: {got} (want {want} +-{tolerance})")


def selftest():
    print("parity selftest: synthetic images with known geometry")
    os.makedirs(SELFTEST_DIR, exist_ok=True)
    failures = []

    palette_a = {"header": (240, 240, 240), "grid": (200, 200, 200),
                 "weekend": (247, 247, 247), "bar": (74, 125, 189)}
    path_a = os.path.join(SELFTEST_DIR, "sheet-a.png")
    truth_a = _make_synthetic(
        path_a, 640, 400, first_column=20,
        columns=[20, 70, 130, 250, 330], last_right=418,
        header_top=40, header_bottom=80, pitch=20, rows=7, day=16,
        palette=palette_a)

    palette_b = {"header": (233, 233, 233), "grid": (190, 190, 190),
                 "weekend": (240, 240, 240), "bar": (60, 110, 200)}
    path_b = os.path.join(SELFTEST_DIR, "sheet-b.png")
    truth_b = _make_synthetic(
        path_b, 900, 560, first_column=16,
        columns=[16, 66, 126, 336, 500], last_right=538,
        header_top=32, header_bottom=78, pitch=24, rows=6, day=20,
        palette=palette_b)

    path_blank = os.path.join(SELFTEST_DIR, "blank.png")
    Image.fromarray(np.full((200, 300, 3), 255, dtype=np.uint8)).save(path_blank)
    path_flat = os.path.join(SELFTEST_DIR, "flat.png")
    Image.fromarray(np.full((150, 200, 3), 30, dtype=np.uint8)).save(path_flat)

    print("A: 640x400, pitch 20, header 40, day 16")
    a = Analysis(path_a)
    _assert_close("column widths", a.measures["sheet_column_widths_px"]["value"],
                  truth_a["column_widths"], 1, failures)
    _assert_close("row height", a.measures["row_height_px"]["value"],
                  truth_a["row_height"], 1, failures)
    _assert_close("header height", a.measures["header_height_px"]["value"],
                  truth_a["header_height"], 1, failures)
    _assert_close("splitter fraction",
                  a.measures["splitter_fraction"]["value"],
                  truth_a["splitter_fraction"], 0.002, failures)
    _assert_close("day width", a.measures["day_width_px"]["value"],
                  truth_a["day_width"], 1, failures)
    _assert_close("timescale tiers",
                  a.measures["timescale_tier_count"]["value"],
                  truth_a["tier_count"], 0, failures)
    for name, key in (("header", "colour.header"), ("grid", "colour.grid"),
                      ("weekend", "colour.weekend"),
                      ("task bar", "colour.task_bar")):
        got = a.measures[key]["value"]
        want = truth_a["palette"][{"header": "header", "grid": "grid",
                                   "weekend": "weekend",
                                   "task bar": "bar"}[name]]
        if got is None:
            failures.append(f"{name} colour: not found (want {want})")
        elif _colour_delta(got, want) > 1:
            failures.append(f"{name} colour: got {got}, want {want}")
        else:
            print(f"  ok  {name} colour: {tuple(got)}")

    print("B: 900x560, pitch 24, header 46, day 20 (a differently scaled fake)")
    b = Analysis(path_b)
    _assert_close("column widths", b.measures["sheet_column_widths_px"]["value"],
                  truth_b["column_widths"], 1, failures)
    _assert_close("row height", b.measures["row_height_px"]["value"],
                  truth_b["row_height"], 1, failures)
    _assert_close("header height", b.measures["header_height_px"]["value"],
                  truth_b["header_height"], 1, failures)
    _assert_close("splitter fraction",
                  b.measures["splitter_fraction"]["value"],
                  truth_b["splitter_fraction"], 0.002, failures)
    _assert_close("day width", b.measures["day_width_px"]["value"],
                  truth_b["day_width"], 1, failures)
    _assert_close("timescale tiers",
                  b.measures["timescale_tier_count"]["value"],
                  truth_b["tier_count"], 0, failures)

    print("blank and one-colour images must not fail")
    blank = Analysis(path_blank)
    flat = Analysis(path_flat)
    for label, analysis in (("blank", blank), ("flat", flat)):
        if analysis.measures["row_height_px"]["found"]:
            failures.append(f"{label}: claimed a row height")
        if analysis.measures["splitter_fraction"]["found"]:
            failures.append(f"{label}: claimed a splitter")
        if not analysis.notes:
            failures.append(f"{label}: no 'not found' notes recorded")
    print(f"  ok  blank: {len(blank.notes)} measures not found")
    print(f"  ok  flat:  {len(flat.notes)} measures not found")

    print("compare mode")
    rows, summary = compare(a, Analysis(path_a))
    if summary["verdict"] != "PASS":
        failures.append(f"self-compare verdict {summary['verdict']}")
    else:
        print(f"  ok  A vs A: {summary['verdict']} "
              f"({summary['comparable']} comparable)")
    rows, summary = compare(a, Analysis(path_b))
    if summary["failures"] == 0:
        failures.append("A vs B should fail at least one measure")
    else:
        print(f"  ok  A vs B: {summary['failures']} failing measure(s)")
    rows, summary = compare(blank, Analysis(path_b))
    if summary["comparable"] != 0:
        failures.append("blank reference should have nothing comparable")
    else:
        print(f"  ok  blank vs B: nothing comparable ({summary['verdict']})")

    if failures:
        print()
        print("SELFTEST FAILED")
        for item in failures:
            print(f"  {item}")
        return 1
    print()
    print("SELFTEST PASSED")
    return 0


# ------------------------------------------------------- pixel score (report) --

# Decision 12 asks for a pixel-difference score that is REPORTED, not gated
# (original icons and font metrics are ours by policy).  wave 20 claimed the
# score existed; it did not -- wave 23 adds it.  The candidate is scaled to the
# reference's size (the two windows are captured at different DPI), and the
# score is the percentage of pixels whose largest channel difference exceeds
# PIXEL_DELTA, plus the mean absolute channel difference.  Regions are split by
# the measured chrome proportions: title 4 %, ribbon 10 %, timeline 5 %, body
# 75 % (sheet left 46 %, chart right), status 6 %.
PIXEL_DELTA = 16
SCORE_REGIONS = [
    ("title", 0.00, 0.04, 0.0, 1.0),
    ("ribbon", 0.04, 0.14, 0.0, 1.0),
    ("timeline", 0.14, 0.19, 0.0, 1.0),
    ("sheet", 0.19, 0.94, 0.0, 0.46),
    ("chart", 0.19, 0.94, 0.46, 1.0),
    ("status", 0.94, 1.00, 0.0, 1.0),
]


def pixel_score(reference_path, candidate_path):
    """Per-region pixel-difference percentages.  Report only; never raises."""
    reference = Image.open(reference_path).convert("RGB")
    candidate = Image.open(candidate_path).convert("RGB").resize(
        reference.size, Image.LANCZOS)
    a = np.asarray(reference, dtype=np.int16)
    b = np.asarray(candidate, dtype=np.int16)
    diff = np.abs(a - b)
    height, width = a.shape[0], a.shape[1]
    rows = []
    for name, y0, y1, x0, x1 in SCORE_REGIONS:
        top, bottom = int(y0 * height), int(y1 * height)
        left, right = int(x0 * width), int(x1 * width)
        region = diff[top:bottom, left:right]
        if region.size == 0:
            continue
        changed = float(np.mean(np.max(region, axis=2) > PIXEL_DELTA)) * 100.0
        rows.append((name, changed, float(np.mean(region))))
    changed = float(np.mean(np.max(diff, axis=2) > PIXEL_DELTA)) * 100.0
    total = ("total", changed, float(np.mean(diff)))
    return rows, total


def print_score(rows, total, reference_path, candidate_path):
    print(f"pixel score (report only, original icons/fonts are ours)")
    print(f"  reference {os.path.basename(reference_path)}")
    print(f"  candidate {os.path.basename(candidate_path)} (scaled to reference)")
    print(f"  {'region':10s} {'changed %':>10s} {'mean abs':>10s}")
    for name, changed, mean_abs in list(rows) + [total]:
        print(f"  {name:10s} {changed:10.2f} {mean_abs:10.2f}")


# --------------------------------------------------------------------- main --

def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Measure the Qt shell against the MS Project reference.")
    parser.add_argument("images", nargs="*", metavar="PNG",
                        help="reference, and optionally a candidate")
    parser.add_argument("--compare", action="store_true",
                        help="force comparison mode (two images)")
    parser.add_argument("--json", metavar="PATH",
                        help="write the measurement as JSON ('-' for stdout)")
    parser.add_argument("--selftest", action="store_true",
                        help="run the synthetic self-check")
    parser.add_argument("--score", action="store_true",
                        help="decision-12 pixel score (report only, two images)")
    parser.add_argument("--reference-json", metavar="PATH",
                        help="compare ONE candidate image to a committed "
                             "parity_report JSON (the numbers measured from a "
                             "local Project capture; wave-23 operator ruling)")
    parser.add_argument("--quiet", action="store_true",
                        help="suppress the human table")
    parser.add_argument("--only", metavar="KEYS",
                        help="comma-separated measure keys to compare; the "
                             "structural minimum drops to the number selected "
                             "(wave 25: a colour-only reference)")
    parser.add_argument("--list-measures", action="store_true",
                        help="print the measure keys and exit")
    args = parser.parse_args(argv)

    if args.list_measures:
        for key, label, _kind, _tolerance, _why in MEASURES:
            print(f"{key:24s} {label}")
        return 0

    if args.selftest:
        return selftest()

    if not args.images:
        parser.print_usage()
        print("error: give a reference PNG, and a candidate PNG to compare",
              file=sys.stderr)
        return 2

    paths = [os.path.abspath(p) for p in args.images]
    for path in paths:
        if not os.path.exists(path):
            print(f"error: no such file: {path}", file=sys.stderr)
            return 2
        try:
            Image.open(path).verify()
        except Exception as exc:                      # noqa: BLE001
            print(f"error: cannot read {path}: {exc}", file=sys.stderr)
            return 2

    if args.reference_json:
        if len(paths) != 1:
            print("error: --reference-json compares exactly ONE candidate image",
                  file=sys.stderr)
            return 2
        try:
            report = json.loads(open(args.reference_json, encoding="utf-8").read())
        except (OSError, ValueError) as exc:               # noqa: BLE001
            print(f"error: cannot read {args.reference_json}: {exc}", file=sys.stderr)
            return 2
        only = None
        if args.only:
            only = tuple(part.strip() for part in args.only.split(",") if part.strip())
            known = {item[0] for item in MEASURES}
            unknown = [key for key in only if key not in known]
            if unknown:
                print("error: unknown measure key(s): " + ", ".join(unknown),
                      file=sys.stderr)
                return 2
        reference = JsonReference(report)
        candidate = Analysis(paths[0])
        rows, summary = compare(reference, candidate, keys=only)
        if not args.quiet:
            print_compare(rows, summary, reference, candidate)
        # Wave 23 review: a gate that passes on ONE surviving measure is not a
        # gate.  The committed reference supplies nine structural measures; a
        # candidate that fails to exhibit most of them is not comparable.  A
        # --only comparison is explicit about which measures are its claim.
        MIN_COMPARABLE = len(only) if only else 8
        if summary["comparable"] < MIN_COMPARABLE:
            print(f"error: only {summary['comparable']} comparable measure(s); "
                  f"at least {MIN_COMPARABLE} are required", file=sys.stderr)
            return 1
        # Wave 24 (W24-11): a colour the reference could not find (the wave-23
        # capture had its chart scrolled away from the bars, so task_bar was
        # "not found") must be a FAILURE, not a silently skipped measure.  All
        # four measured colours must be present in the reference AND in the
        # candidate render.
        colour_keys = tuple(key for key in ("colour.header", "colour.grid",
                                            "colour.weekend", "colour.task_bar")
                            if only is None or key in only)
        missing_colours = [
            key for key in colour_keys
            if not report.get("measures", {}).get(key, {}).get("found") or
            not candidate.measures.get(key, {}).get("found")]
        if missing_colours:
            print("error: colour measure(s) not found: " +
                  ", ".join(missing_colours), file=sys.stderr)
            return 1
        if args.json:
            payload = {"reference": args.reference_json, "comparison": rows,
                       "summary": summary}
            text = json.dumps(payload, indent=2, sort_keys=False)
            if args.json == "-":
                print(text)
            else:
                os.makedirs(os.path.dirname(os.path.abspath(args.json)),
                            exist_ok=True)
                with open(args.json, "w", encoding="utf-8") as handle:
                    handle.write(text + "\n")
                print(f"\nwrote {args.json}")
        return 0 if summary["verdict"] == "PASS" else 1

    if len(paths) == 1 and not args.compare:
        analysis = Analysis(paths[0])
        report = analysis.as_dict()
        if not args.quiet:
            print_report(report)
        if args.json:
            text = json.dumps(report, indent=2, sort_keys=False)
            if args.json == "-":
                print(text)
            else:
                os.makedirs(os.path.dirname(os.path.abspath(args.json)),
                            exist_ok=True)
                with open(args.json, "w", encoding="utf-8") as handle:
                    handle.write(text + "\n")
                print(f"\nwrote {args.json}")
        return 0

    if len(paths) != 2:
        print("error: comparison needs exactly two PNGs (reference, candidate)",
              file=sys.stderr)
        return 2

    if args.score:
        rows, total = pixel_score(paths[0], paths[1])
        print_score(rows, total, paths[0], paths[1])
        if args.json:
            payload = {"regions": [{"region": n, "changed_percent": c,
                                    "mean_abs": m} for n, c, m in rows],
                       "total": {"changed_percent": total[1],
                                 "mean_abs": total[2]},
                       "note": "report only; does not gate (decision 12)"}
            text = json.dumps(payload, indent=2)
            if args.json == "-":
                print(text)
            else:
                os.makedirs(os.path.dirname(os.path.abspath(args.json)),
                            exist_ok=True)
                with open(args.json, "w", encoding="utf-8") as handle:
                    handle.write(text + "\n")
                print(f"\nwrote {args.json}")
        return 0

    reference = Analysis(paths[0])
    candidate = Analysis(paths[1])
    rows, summary = compare(reference, candidate)
    if not args.quiet:
        print_compare(rows, summary, reference, candidate)
    if args.json:
        payload = {
            "reference": reference.as_dict(),
            "candidate": candidate.as_dict(),
            "comparison": rows,
            "summary": summary,
        }
        text = json.dumps(payload, indent=2, sort_keys=False)
        if args.json == "-":
            print(text)
        else:
            os.makedirs(os.path.dirname(os.path.abspath(args.json)),
                        exist_ok=True)
            with open(args.json, "w", encoding="utf-8") as handle:
                handle.write(text + "\n")
            print(f"\nwrote {args.json}")
    return 0 if summary["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
