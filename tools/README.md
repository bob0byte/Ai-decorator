# Level pack analysis pipeline

Turns a zip of `.gmd` levels into the learned style data embedded in `src/StyleData.hpp`.

1. `decode.py <gmd dir> <out dir>` - decode every .gmd into JSON (header, colors, objects)
2. `reference.py` - object id reference (category, family, texture, hitbox) from gdclone's object.json + trigger/orb/pad/portal tables
3. `compact.py` - levels into numpy arrays
4. `analyze.py` - per level: attach every deco to its nearest gameplay object, side/exposure/offset/rotation/scale/layer/color
5. `triggers.py`, `trigger_stats.py` - every trigger and its settings
6. `aggregate.py` - global sorted tables + level features
7. `build_styles.py` - 12 style groups + one style per level, palettes, scenery
8. `make_header.py` - writes src/StyleData.hpp
