# Render tests

Fixtures that exercise every widget and edge case on a real device.

```sh
cyd-canvas pair <host> --pin '…'              # once
python tests/render/run.py                    # automatic: API contract + each case shown 4 s
python tests/render/run.py --interactive      # you judge each step against its "expect" line
python tests/render/run.py --only t05 --hold 8
python tests/render/run.py --cleanup          # remove the test screens afterwards
```

| Case | Covers |
|---|---|
| t01 | label fonts and colours |
| t02 | `value` formats, negatives, missing key, **hostile `fmt`** (intentionally violates the schema; the device must fall back to `%.1f`) |
| t03 | `bar` ranges, clamping, threshold edges |
| t04 | `arc` with negative ranges, partial updates |
| t05 | line chart, window scrolling past `points` |
| t06 | multi-series line chart, bar chart |
| t07 | `table`: objects, arrays, long text, null/missing cells, row cap |
| t08 | `image`: opaque, alpha, missing asset |
| t09 | `led` with numbers/booleans |
| t10 | light background |
| t11 | 64 widgets |
| t12 | every widget before any data |
| t13 | the always-on ✕ button over screen content |
| t14 | expiry (`--slow`) |
| t15 | critical priority: takes over the display, ack, re-raise (automatic checks in `run.py`) |

`manifest.json` holds each step's data and a plain-language expectation. `invalid/` holds
screens the device must reject (HTTP 400). Add a case by adding a screen, a manifest entry,
and (if it needs new behaviour) an `expect` line a person can check in two seconds.
