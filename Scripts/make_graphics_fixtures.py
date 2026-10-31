import base64
import json
import struct
import zlib
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "Source" / "Tests" / "Fixtures" / "Graphics"
OUT.mkdir(parents=True, exist_ok=True)


def make_png(size=8):
    raw = b""
    for y in range(size):
        raw += b"\x00"
        for x in range(size):
            if (x // 2 + y // 2) % 2 == 0:
                raw += bytes([255, 32, 32, 255])
            else:
                raw += bytes([255, 255, 255, 255])
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


PNG = make_png()
(OUT / "checker.png").write_bytes(PNG)

faces = [
    ((0, 0, -1), [(-.5, -.5, -.5), (-.5, .5, -.5), (.5, .5, -.5), (.5, -.5, -.5)]),
    ((0, 0, 1), [(.5, -.5, .5), (.5, .5, .5), (-.5, .5, .5), (-.5, -.5, .5)]),
    ((-1, 0, 0), [(-.5, -.5, .5), (-.5, .5, .5), (-.5, .5, -.5), (-.5, -.5, -.5)]),
    ((1, 0, 0), [(.5, -.5, -.5), (.5, .5, -.5), (.5, .5, .5), (.5, -.5, .5)]),
    ((0, 1, 0), [(-.5, .5, -.5), (-.5, .5, .5), (.5, .5, .5), (.5, .5, -.5)]),
    ((0, -1, 0), [(-.5, -.5, .5), (-.5, -.5, -.5), (.5, -.5, -.5), (.5, -.5, .5)]),
]
positions, normals, uvs, indices = [], [], [], []
for normal, corners in faces:
    base = len(positions)
    for i, corner in enumerate(corners):
        positions.append(corner)
        normals.append(normal)
        uvs.append([(0, 0), (0, 1), (1, 1), (1, 0)][i])
    indices += [base, base + 1, base + 2, base, base + 2, base + 3]

pos_bytes = b"".join(struct.pack("<3f", *p) for p in positions)
norm_bytes = b"".join(struct.pack("<3f", *n) for n in normals)
uv_bytes = b"".join(struct.pack("<2f", *t) for t in uvs)
idx_bytes = b"".join(struct.pack("<H", i) for i in indices)

mins = [min(p[i] for p in positions) for i in range(3)]
maxs = [max(p[i] for p in positions) for i in range(3)]


def align4(b, pad=b"\x00"):
    return b + pad * ((4 - len(b) % 4) % 4)


def build_gltf(embed_image: bool):
    bin_parts = []
    views = []
    def add_view(data, target=None):
        offset = sum(len(p) for p in bin_parts)
        bin_parts.append(align4(data))
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
        if target:
            view["target"] = target
        views.append(view)
        return len(views) - 1

    v_pos = add_view(pos_bytes, 34962)
    v_norm = add_view(norm_bytes, 34962)
    v_uv = add_view(uv_bytes, 34962)
    v_idx = add_view(idx_bytes, 34963)
    image_entry = None
    if embed_image:
        v_img = add_view(PNG)
        image_entry = {"bufferView": v_img, "mimeType": "image/png", "name": "checker"}
    else:
        image_entry = {"uri": "checker.png", "name": "checker"}

    binary = b"".join(bin_parts)
    doc = {
        "asset": {"version": "2.0", "generator": "Shalltear fixture generator (self-made)"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "Cube"}],
        "meshes": [{"name": "Cube", "primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
            "indices": 3, "material": 0}]}],
        "accessors": [
            {"bufferView": v_pos, "componentType": 5126, "count": len(positions), "type": "VEC3", "min": mins, "max": maxs},
            {"bufferView": v_norm, "componentType": 5126, "count": len(normals), "type": "VEC3"},
            {"bufferView": v_uv, "componentType": 5126, "count": len(uvs), "type": "VEC2"},
            {"bufferView": v_idx, "componentType": 5123, "count": len(indices), "type": "SCALAR"},
        ],
        "bufferViews": views,
        "buffers": [{"byteLength": len(binary)}],
        "materials": [{"name": "CheckerMat", "pbrMetallicRoughness": {
            "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
            "baseColorTexture": {"index": 0},
            "metallicFactor": 0.0, "roughnessFactor": 0.8}}],
        "textures": [{"source": 0}],
        "images": [image_entry],
    }
    return doc, binary


doc, binary = build_gltf(embed_image=True)
json_bytes = align4(json.dumps(doc, separators=(",", ":")).encode(), b" ")
binary = align4(binary)
glb = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(json_bytes) + 8 + len(binary))
glb += struct.pack("<II", len(json_bytes), 0x4E4F534A) + json_bytes
glb += struct.pack("<II", len(binary), 0x004E4942) + binary
(OUT / "cube.glb").write_bytes(glb)

doc, binary = build_gltf(embed_image=False)
doc["buffers"][0]["uri"] = "data:application/octet-stream;base64," + base64.b64encode(binary).decode()
(OUT / "cube_external.gltf").write_text(json.dumps(doc, indent=1), newline="\n")

print("fixtures written to", OUT)
print("glb size:", len(glb), "png size:", len(PNG))
