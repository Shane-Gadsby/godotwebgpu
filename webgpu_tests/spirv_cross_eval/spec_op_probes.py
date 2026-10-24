#!/usr/bin/env python3
"""Generates one tiny compute module per OpSpecConstantOp kind (plus the composite / array / workgroup-size shapes
spirv_preprocess::spec_constants_overridable() freezes), for webgpu_notes/work/results/04-spec-constants.

Each module declares scalar spec constants (ids 0..6), computes one OpSpecConstantOp from them, and writes the value
(as u32 bits) to o.v[0]. spec_op_check.mjs converts them with `tint_convert_cli --raw` (no preprocessing, so the
backend's own spec-constant lowering is what is tested), builds a pipeline with the default and with alternate
constants in Chrome, and compares against the expected values in manifest.json (computed here, 32-bit wrap).

Usage: spec_op_probes.py <out_dir>   (needs spirv-as)
"""

import json
import os
import subprocess
import sys

M32 = 0xFFFFFFFF


def s32(x):
    x &= M32
    return x - (1 << 32) if x & 0x80000000 else x


def trunc_div(a, b):
    q = abs(a) // abs(b)
    return -q if (a < 0) != (b < 0) else q


# spec constants: id -> (name, type, default, alternate)
SC = {
    0: ("A", "uint", 7, 100),
    1: ("B", "uint", 3, 5),
    2: ("SA", "int", -9, 20),
    3: ("SB", "int", 4, -6),
    4: ("BA", "bool", True, False),
    5: ("BB", "bool", False, True),
    6: ("FA", "float", 2.5, 4.0),
}
NAMES = {v[0]: k for k, v in SC.items()}


def val(vals, n):
    return vals[NAMES[n]]


# (opname, result type, operand names, python fn on (values by id) -> expected raw u32)
def u(x):
    return x & M32


OPS = []


def op(name, rtype, args, fn):
    OPS.append((name, rtype, args, fn))


op("IAdd_u", "uint", ["A", "B"], lambda v: u(val(v, "A") + val(v, "B")))
op("ISub_u", "uint", ["A", "B"], lambda v: u(val(v, "A") - val(v, "B")))
op("IMul_u", "uint", ["A", "B"], lambda v: u(val(v, "A") * val(v, "B")))
op("UDiv", "uint", ["A", "B"], lambda v: u(val(v, "A") // val(v, "B")))
op("UMod", "uint", ["A", "B"], lambda v: u(val(v, "A") % val(v, "B")))
op("ShiftLeftLogical", "uint", ["A", "B"], lambda v: u(val(v, "A") << val(v, "B")))
op("ShiftRightLogical", "uint", ["A", "B"], lambda v: u(val(v, "A") >> val(v, "B")))
op("BitwiseAnd", "uint", ["A", "B"], lambda v: u(val(v, "A") & val(v, "B")))
op("BitwiseOr", "uint", ["A", "B"], lambda v: u(val(v, "A") | val(v, "B")))
op("BitwiseXor", "uint", ["A", "B"], lambda v: u(val(v, "A") ^ val(v, "B")))
op("Not", "uint", ["A"], lambda v: u(~val(v, "A")))
op("IAdd_i", "int", ["SA", "SB"], lambda v: u(val(v, "SA") + val(v, "SB")))
op("ISub_i", "int", ["SA", "SB"], lambda v: u(val(v, "SA") - val(v, "SB")))
op("IMul_i", "int", ["SA", "SB"], lambda v: u(val(v, "SA") * val(v, "SB")))
op("SNegate", "int", ["SA"], lambda v: u(-val(v, "SA")))
op("SDiv", "int", ["SA", "SB"], lambda v: u(trunc_div(val(v, "SA"), val(v, "SB"))))
op("SRem", "int", ["SA", "SB"], lambda v: u(val(v, "SA") - val(v, "SB") * trunc_div(val(v, "SA"), val(v, "SB"))))
op("SMod", "int", ["SA", "SB"], lambda v: u(val(v, "SA") % val(v, "SB")))  # python %: sign of the divisor, like SMod
op("ShiftRightArithmetic", "int", ["SA", "B"], lambda v: u(val(v, "SA") >> val(v, "B")))
op("IEqual", "bool", ["A", "B"], lambda v: int(val(v, "A") == val(v, "B")))
op("INotEqual", "bool", ["A", "B"], lambda v: int(val(v, "A") != val(v, "B")))
op("ULessThan", "bool", ["A", "B"], lambda v: int(val(v, "A") < val(v, "B")))
op("ULessThanEqual", "bool", ["A", "B"], lambda v: int(val(v, "A") <= val(v, "B")))
op("UGreaterThan", "bool", ["A", "B"], lambda v: int(val(v, "A") > val(v, "B")))
op("UGreaterThanEqual", "bool", ["A", "B"], lambda v: int(val(v, "A") >= val(v, "B")))
op("SLessThan", "bool", ["SA", "SB"], lambda v: int(val(v, "SA") < val(v, "SB")))
op("SLessThanEqual", "bool", ["SA", "SB"], lambda v: int(val(v, "SA") <= val(v, "SB")))
op("SGreaterThan", "bool", ["SA", "SB"], lambda v: int(val(v, "SA") > val(v, "SB")))
op("SGreaterThanEqual", "bool", ["SA", "SB"], lambda v: int(val(v, "SA") >= val(v, "SB")))
op("LogicalAnd", "bool", ["BA", "BB"], lambda v: int(val(v, "BA") and val(v, "BB")))
op("LogicalOr", "bool", ["BA", "BB"], lambda v: int(val(v, "BA") or val(v, "BB")))
op("LogicalEqual", "bool", ["BA", "BB"], lambda v: int(val(v, "BA") == val(v, "BB")))
op("LogicalNotEqual", "bool", ["BA", "BB"], lambda v: int(val(v, "BA") != val(v, "BB")))
op("LogicalNot", "bool", ["BA"], lambda v: int(not val(v, "BA")))
op("Select", "uint", ["BA", "A", "B"], lambda v: val(v, "A") if val(v, "BA") else val(v, "B"))
# uint = IAdd int_spec 0: how glslang spells uint(int_spec) / bitcast; mixed signedness operands.
op("IAdd_mixed", "uint", ["SA", "B"], lambda v: u(val(v, "SA") + val(v, "B")))


def f32_bits(f):
    import struct

    return struct.unpack("<I", struct.pack("<f", f))[0]


op("FConvert_f32", "float", ["FA"], lambda v: f32_bits(val(v, "FA")))  # same width: a no-op the spec allows

TYPES = """
%void = OpTypeVoid
%fn = OpTypeFunction %void
%uint = OpTypeInt 32 0
%int = OpTypeInt 32 1
%bool = OpTypeBool
%float = OpTypeFloat 32
%v3uint = OpTypeVector %uint 3
%uint_0 = OpConstant %uint 0
%uint_1 = OpConstant %uint 1
%uint_2 = OpConstant %uint 2
%rta = OpTypeRuntimeArray %uint
%Out = OpTypeStruct %rta
%pOut = OpTypePointer StorageBuffer %Out
%o = OpVariable %pOut StorageBuffer
%pU = OpTypePointer StorageBuffer %uint
"""


def spec_decls(extra=""):
    lines = []
    for i, (n, t, d, _) in SC.items():
        if t == "bool":
            lines.append(f"%{n} = OpSpecConstant{'True' if d else 'False'} %bool")
        elif t == "float":
            lines.append(f"%{n} = OpSpecConstant %float {d}")
        else:
            lines.append(f"%{n} = OpSpecConstant %{t} {d}")
    return "\n".join(lines)


def header(wg="OpExecutionMode %main LocalSize 1 1 1", extra_decor="", extra_cap=""):
    decor = "\n".join(f"OpDecorate %{n} SpecId {i}" for i, (n, _, _, _) in SC.items())
    return f"""OpCapability Shader
{extra_cap}OpMemoryModel Logical GLSL450
OpEntryPoint GLCompute %main "main"
{wg}
OpDecorate %rta ArrayStride 4
OpDecorate %Out Block
OpMemberDecorate %Out 0 Offset 0
OpDecorate %o DescriptorSet 0
OpDecorate %o Binding 0
{decor}
{extra_decor}"""


def store_result(res, rtype):
    if rtype == "uint":
        conv = f"%r = OpCopyObject %uint %{res}"
    elif rtype == "int":
        conv = f"%r = OpBitcast %uint %{res}"
    elif rtype == "float":
        conv = f"%r = OpBitcast %uint %{res}"
    else:
        conv = f"%r = OpSelect %uint %{res} %uint_1 %uint_0"
    return f"""%main = OpFunction %void None %fn
%entry = OpLabel
{conv}
%ptr = OpAccessChain %pU %o %uint_0 %uint_0
OpStore %ptr %r
OpReturn
OpFunctionEnd
"""


def module_for_op(name, rtype, args):
    opname = name.split("_")[0]
    body = f"%res = OpSpecConstantOp %{rtype} {opname} " + " ".join(f"%{a}" for a in args)
    return header() + TYPES + spec_decls() + "\n" + body + "\n" + store_result("res", rtype)


def module_composite(kind):
    # vec3 built from spec constants: GLSL's `const vec3 V = vec3(A, B, 1)`.
    comp = "%vc = OpSpecConstantComposite %v3uint %A %B %uint_1"
    if kind == "composite_extract":
        body = comp + "\n%res = OpSpecConstantOp %uint CompositeExtract %vc 1"
    elif kind == "vector_shuffle":
        body = (
            comp
            + "\n%vs = OpSpecConstantOp %v3uint VectorShuffle %vc %vc 2 1 0\n%res = OpSpecConstantOp %uint CompositeExtract %vs 2"
        )
    elif kind == "composite_insert":
        body = (
            comp
            + "\n%ci = OpSpecConstantOp %v3uint CompositeInsert %A %vc 2\n%res = OpSpecConstantOp %uint CompositeExtract %ci 2"
        )
    elif kind == "composite_use_runtime":
        # the composite is used by runtime code, as GLSL's `vec3(F,..)` is
        body = comp + "\n"
        main = """%main = OpFunction %void None %fn
%entry = OpLabel
%x = OpCompositeExtract %uint %vc 0
%y = OpCompositeExtract %uint %vc 1
%r = OpIAdd %uint %x %y
%ptr = OpAccessChain %pU %o %uint_0 %uint_0
OpStore %ptr %r
OpReturn
OpFunctionEnd
"""
        return header() + TYPES + spec_decls() + "\n" + body + main
    return header() + TYPES + spec_decls() + "\n" + body + "\n" + store_result("res", "uint")


def module_shared_array():
    # workgroup array sized by a spec constant (GLSL: shared uint s[N])
    decor = ""
    body = """%parr = OpTypeArray %uint %B
%pwa = OpTypePointer Workgroup %parr
%pwu = OpTypePointer Workgroup %uint
%s = OpVariable %pwa Workgroup
%main = OpFunction %void None %fn
%entry = OpLabel
%e0 = OpAccessChain %pwu %s %uint_0
OpStore %e0 %A
%l = OpLoad %uint %e0
%ptr = OpAccessChain %pU %o %uint_0 %uint_0
OpStore %ptr %l
OpReturn
OpFunctionEnd
"""
    return header(extra_decor=decor) + TYPES + spec_decls() + "\n" + body


def module_local_array():
    body = """%parr = OpTypeArray %uint %B
%pfa = OpTypePointer Function %parr
%pfu = OpTypePointer Function %uint
%main = OpFunction %void None %fn
%entry = OpLabel
%s = OpVariable %pfa Function
%e0 = OpAccessChain %pfu %s %uint_0
OpStore %e0 %A
%l = OpLoad %uint %e0
%ptr = OpAccessChain %pU %o %uint_0 %uint_0
OpStore %ptr %l
OpReturn
OpFunctionEnd
"""
    return header() + TYPES + spec_decls() + "\n" + body


def module_wg_size():
    # LocalSize from spec constants (GLSL: local_size_x_id = 0, local_size_y_id = 1)
    wg = ""
    decor = "OpDecorate %wgs BuiltIn WorkgroupSize"
    body = "%wgs = OpSpecConstantComposite %v3uint %A %uint_1 %uint_1\n"
    main = """%main = OpFunction %void None %fn
%entry = OpLabel
%x = OpCompositeExtract %uint %wgs 0
%ptr = OpAccessChain %pU %o %uint_0 %uint_0
OpStore %ptr %x
OpReturn
OpFunctionEnd
"""
    return header(wg=wg, extra_decor=decor) + TYPES + spec_decls() + "\n" + body + main


def expected_for(fn):
    d = {i: SC[i][2] for i in SC}
    a = {i: SC[i][3] for i in SC}
    return fn(d), fn(a)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    manifest = {}
    consts_default = {str(i): (1 if SC[i][2] is True else 0 if SC[i][2] is False else SC[i][2]) for i in SC}
    consts_alt = {str(i): (1 if SC[i][3] is True else 0 if SC[i][3] is False else SC[i][3]) for i in SC}
    mods = {}
    for name, rtype, args, fn in OPS:
        mods[f"op_{name}"] = (module_for_op(name, rtype, args), expected_for(fn), "scalar op")
    mods["composite_extract"] = (
        module_composite("composite_extract"),
        expected_for(lambda v: v[1]),
        "composite of spec scalars",
    )
    mods["vector_shuffle"] = (
        module_composite("vector_shuffle"),
        expected_for(lambda v: 1 if False else 1),
        "composite of spec scalars",
    )
    mods["composite_insert"] = (
        module_composite("composite_insert"),
        expected_for(lambda v: v[0]),
        "composite of spec scalars",
    )
    mods["composite_use_runtime"] = (
        module_composite("composite_use_runtime"),
        expected_for(lambda v: v[0] + v[1]),
        "composite of spec scalars",
    )
    mods["shared_array_sized_by_spec"] = (module_shared_array(), expected_for(lambda v: v[0]), "array size")
    mods["local_array_sized_by_spec"] = (module_local_array(), expected_for(lambda v: v[0]), "array size")
    mods["workgroup_size_from_spec"] = (module_wg_size(), expected_for(lambda v: v[0]), "workgroup size")
    # vector_shuffle result element 2 of (C,B,A): the shuffle picks components 2 1 0 of (A,B,1) -> (1,B,A); [2] = A
    mods["vector_shuffle"] = (mods["vector_shuffle"][0], expected_for(lambda v: v[0]), "composite of spec scalars")
    for name, (asm, (exp_d, exp_a), cat) in mods.items():
        asm_path = os.path.join(out, name + ".spvasm")
        spv_path = os.path.join(out, name + ".spv")
        open(asm_path, "w").write(asm)
        r = subprocess.run(
            ["spirv-as", "--target-env", "vulkan1.1", asm_path, "-o", spv_path], capture_output=True, text=True
        )
        if r.returncode:
            print(f"spirv-as failed for {name}: {r.stderr}", file=sys.stderr)
            sys.exit(1)
        manifest[name] = {"category": cat, "expectDefault": exp_d, "expectAlt": exp_a}
    # composite expectations by constants dict: A=id0, B=id1
    manifest["_consts"] = {"default": consts_default, "alt": consts_alt}
    json.dump(manifest, open(os.path.join(out, "manifest.json"), "w"), indent=1)
    print(f"{len(mods)} modules in {out}")


if __name__ == "__main__":
    main()
