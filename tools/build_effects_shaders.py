#!/usr/bin/env python3
"""Embed the GPU effects shaders: native/engine/gpu_effects_shaders.inc.

gpu_effects.hlsl goes in as text (Direct3D 11 compiles it at run time) and as
SPIR-V and DXIL for SDL_GPU's Vulkan and Direct3D 12, compiled here by dxc.
gpu_effects.metal goes in as text (Metal compiles MSL source at run time).

    py -3 tools/build_effects_shaders.py           # regenerate (needs dxc)
    py -3 tools/build_effects_shaders.py --check   # CI: is the .inc up to date and whole?

dxc comes with the Windows SDK (Windows Kits/10/bin/<version>/x64/dxc.exe,
with dxil.dll beside it so the DXIL is signed) or the Vulkan SDK; DXC=<path>
overrides the search. --check needs no compiler: the .inc records a hash of
both sources, and a source edited without regenerating fails it.

THE KERNELS ARE THE ONES gpu_effects.cpp NAMES (kEntryNames), read from there,
and the .inc carries one table of them by name (kEffectsBlobs). The list used
to be written out here as well, and when Circuit Bend added bendMain to the
engine and not to this list, Vulkan and Direct3D 12 got no code for it, its
pipeline failed, and with it every GPU effect, transition and source on those
renderers (0.99.428 and 0.99.429: Linux drew all of them on the CPU). --check
now fails when a kernel the engine names has no embedded code, or no SPIR-V
(the Windows SDK's dxc writes none, and its embed passed the hash check).
"""
import glob
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / "native" / "engine"
HLSL = ENGINE / "gpu_effects.hlsl"
MSL = ENGINE / "gpu_effects.metal"
OUT = ENGINE / "gpu_effects_shaders.inc"
ENGINE_CPP = ENGINE / "gpu_effects.cpp"


def engine_entries():
    """The kernels the engine names, in its order (gpu_effects.cpp kEntryNames)."""
    text = ENGINE_CPP.read_text(encoding="utf-8")
    found = re.search(r"kEntryNames\[E_COUNT\]\s*=\s*\{(.*?)\};", text, re.S)
    if not found:
        sys.exit("build_effects_shaders: no kEntryNames in gpu_effects.cpp")
    return re.findall(r'"(\w+)"', found.group(1))


ENTRIES = tuple(engine_entries())


def source_hash():
    digest = hashlib.sha256()
    for path in (HLSL, MSL):
        digest.update(path.read_bytes().replace(b"\r\n", b"\n"))
    return digest.hexdigest()


def find_dxc():
    if os.environ.get("DXC"):
        return os.environ["DXC"]
    kits = sorted(glob.glob(r"C:\Program Files (x86)\Windows Kits\10\bin\10.*\x64\dxc.exe"))
    if kits:
        return kits[-1]
    return shutil.which("dxc")


def compile_hlsl(dxc, entry, spirv):
    with tempfile.TemporaryDirectory() as work:
        out = Path(work) / "shader.bin"
        command = [dxc, "-nologo", "-T", "cs_6_0", "-E", entry, "-O3", "-Fo", str(out), str(HLSL)]
        command[1:1] = ["-spirv", "-fspv-target-env=vulkan1.0"] if spirv else ["-D", "SDL_GPU_D3D12"]
        result = subprocess.run(command, capture_output=True, text=True)
        if spirv and "SPIR-V CodeGen not available" in result.stderr:
            return None   # the Windows SDK's dxc is built without it
        if result.returncode != 0:
            sys.exit(f"dxc failed for {entry} ({'SPIR-V' if spirv else 'DXIL'}):\n{result.stderr}")
        return out.read_bytes()


def stem_of(entry):
    return entry[0].upper() + entry[1:-4]   # columnsMain -> Columns


def byte_array(name, data):
    if data is None:
        # No SPIR-V from this dxc: an empty array, and Vulkan stays on the CPU.
        return (f"[[maybe_unused]] static constexpr bool {name}Missing = true;\n"
                f"[[maybe_unused]] static const unsigned char {name}[] = {{0}};")
    lines = [f"[[maybe_unused]] static constexpr bool {name}Missing = false;", f"[[maybe_unused]] static const unsigned char {name}[] = {{"]
    for i in range(0, len(data), 20):
        lines.append("  " + ", ".join(str(b) for b in data[i:i + 20]) + ",")
    lines.append("};")
    return "\n".join(lines)


def raw_string(name, text, tag):
    # MSVC caps one literal at 16 KB, so long sources go in as adjacent pieces.
    pieces, chunk = [], 12000
    for i in range(0, len(text), chunk):
        pieces.append(f'R"{tag}({text[i:i + chunk]}){tag}"')
    return f"[[maybe_unused]] static const char {name}[] =\n" + "\n".join(pieces) + ";"


def main():
    if "--check" in sys.argv:
        text = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
        recorded = text.splitlines()[2] if text else ""
        if recorded != f"// source sha256 {source_hash()}":
            sys.exit("gpu_effects_shaders.inc is stale: run py -3 tools/build_effects_shaders.py")
        table = re.findall(r'\{"(\w+)", kEffects', text)
        gone = [e for e in ENTRIES if e not in table]
        if gone:
            sys.exit("gpu_effects_shaders.inc has no code for %s, which gpu_effects.cpp names: "
                     "run py -3 tools/build_effects_shaders.py" % ", ".join(gone))
        no_spirv = [e for e in ENTRIES if f"kEffects{stem_of(e)}SpirvMissing = true" in text]
        if no_spirv:
            sys.exit("gpu_effects_shaders.inc has no SPIR-V for %s, so Vulkan would get no GPU effects: "
                     "regenerate with a dxc that writes SPIR-V (DXC=<path>)" % ", ".join(no_spirv))
        print("gpu effects shaders: embedded copies match their sources; all %d kernels have code" % len(ENTRIES))
        return 0
    dxc = find_dxc()
    if not dxc:
        sys.exit("no dxc: install the Windows SDK or the Vulkan SDK, or set DXC")
    version = subprocess.run([dxc, "--version"], capture_output=True, text=True).stdout.strip() or "dxc"
    parts = [
        "// Generated by tools/build_effects_shaders.py from gpu_effects.hlsl and gpu_effects.metal. Do not edit.",
        f"// {version.splitlines()[0] if version else 'dxc'}",
        f"// source sha256 {source_hash()}",
        "// clang-format off",
        raw_string("kEffectsHlsl", HLSL.read_text(encoding="utf-8").replace("\r\n", "\n"), "HLSL"),
        raw_string("kEffectsMsl", MSL.read_text(encoding="utf-8").replace("\r\n", "\n"), "MSL"),
    ]
    missing = False
    for entry in ENTRIES:
        stem = stem_of(entry)
        spirv = compile_hlsl(dxc, entry, True)
        missing = missing or spirv is None
        parts.append(byte_array(f"kEffects{stem}Spirv", spirv))
        parts.append(byte_array(f"kEffects{stem}Dxil", compile_hlsl(dxc, entry, False)))
    # One table by name: the engine looks each kernel up, so one it names and
    # this file lacks is an error it can name, not a null it compiles.
    rows = ["struct EffectsBlob {", "  const char* entry;", "  const unsigned char* spirv;", "  std::size_t spirvSize;",
            "  const unsigned char* dxil;", "  std::size_t dxilSize;", "};",
            "[[maybe_unused]] static const EffectsBlob kEffectsBlobs[] = {"]
    for entry in ENTRIES:
        stem = stem_of(entry)
        rows.append(f'  {{"{entry}", kEffects{stem}Spirv, sizeof(kEffects{stem}Spirv), '
                    f'kEffects{stem}Dxil, sizeof(kEffects{stem}Dxil)}},')
    rows.append("};")
    parts.append("\n".join(rows))
    OUT.write_text("\n".join(parts) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {OUT.relative_to(ROOT)} with {dxc}")
    if missing:
        print("NOTE: this dxc has no SPIR-V code generator, so Vulkan gets no GPU effects. Use the"
              " dxc from the Vulkan SDK or a DirectXShaderCompiler release (DXC=<path>).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
