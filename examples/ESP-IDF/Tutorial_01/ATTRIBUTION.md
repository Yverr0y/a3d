# Third-party assets in Tutorial_01

## low-poly_car.glb

**This asset is CC-BY 4.0, which requires attribution wherever it is
distributed — including in a firmware image that embeds it.** `low-poly_car.a3d`
is generated from this file and inherits the same terms, and Tutorial_01 links
it into the application binary, so anything built from this directory and
shipped carries the obligation with it.

| | |
|---|---|
| Title | Low-poly Car |
| Author | Devvux — https://sketchfab.com/Devvux |
| Licence | [CC-BY 4.0](https://creativecommons.org/licenses/by/4.0/legalcode) |
| Source | https://sketchfab.com/3d-models/low-poly-car-9bf2773a3e4d4818a06489a75fe500ed |

None of that was looked up: it is in the file. glTF carries it in
`asset.extras`, and Sketchfab writes it there on export —

```bash
python3 - <<'EOF'
import json, struct
d = open("low-poly_car.glb", "rb").read()
n, _ = struct.unpack("<II", d[12:20])
print(json.dumps(json.loads(d[20:20 + n])["asset"], indent=1))
EOF
```

— which is worth knowing before you embed somebody else's model in a product.
`tools/a3d_export.py` does not copy it into the `.a3d`, so it has to be written
down somewhere, and this is that somewhere.

## Rebuilding the container

```bash
python3 tools/a3d_export.py --check examples/ESP-IDF/Tutorial_01/low-poly_car.glb
```

Needs Pillow, or the texture is skipped with a warning and the car imports
plain white.

The importer prints one warning for this model that is expected and harmless:

```
material 'material_0' is alphaMode MASK; a3d has no alpha blending and will
draw it SOLID. Pass --drop-blend to leave those primitives out
```

Drawing it solid is the right answer here — nothing in this car is meant to be
cut out — so the flag is deliberately not passed.
