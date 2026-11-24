import sys
import os
import unittest

sys.path.insert(0, 'tools')
import rdna2spirv
import spirv_asm

class MeshAllocSingleWriterTests(unittest.TestCase):
    def test_ms_alloc_req_single_writer_guard(self):
        """SHADER-766: s_sendmsg(MSG_GS_ALLOC_REQ) stores %ms_counts atomically to avoid data races."""
        for mesh_compute in (False, True):
            orig_mc = rdna2spirv.MESH_COMPUTE
            try:
                rdna2spirv.MESH_COMPUTE = mesh_compute
                gen = rdna2spirv.Gen("ms")
                gen.ms = {"threads": 128, "maxv": 64, "maxp": 64, "clip": 0, "cull": 0, "params": []}
                # Emit MSG_GS_ALLOC_REQ
                ins = {
                    "op": "s_sendmsg",
                    "args": [{"k": "special", "t": "sendmsg(MSG_GS_ALLOC_REQ)"}],
                    "mods": []
                }
                gen._insn(ins)

                body_text = "\n".join(gen.body)
                # 1. OpAtomicStore %ms_counts must be present
                self.assertIn("OpAtomicStore %ms_counts", body_text)

                # 2. Verify assembly into a complete module passes spirv_asm
                # Build mock header
                types_and_vars = [
                    "OpCapability Shader",
                    "OpCapability MeshShadingEXT" if not mesh_compute else "",
                    "OpMemoryModel Logical GLSL450",
                    'OpEntryPoint GLCompute %main "main" %local_index' if mesh_compute else 'OpEntryPoint MeshEXT %main "main" %local_index',
                    "OpExecutionMode %main LocalSize 128 1 1",
                    "OpDecorate %local_index BuiltIn LocalInvocationIndex",
                    "%void = OpTypeVoid",
                    "%bool = OpTypeBool",
                    "%uint = OpTypeInt 32 0",
                    "%c0 = OpConstant %uint 0",
                    "%c2 = OpConstant %uint 2",
                    "%_ptr_Input_uint = OpTypePointer Input %uint",
                    "%_ptr_Workgroup_uint = OpTypePointer Workgroup %uint",
                    "%_ptr_Function_uint = OpTypePointer Function %uint",
                    "%local_index = OpVariable %_ptr_Input_uint Input",
                    "%ms_counts = OpVariable %_ptr_Workgroup_uint Workgroup",
                    "%m0 = OpVariable %_ptr_Function_uint Function",
                    "%fn_t = OpTypeFunction %void",
                    "%main = OpFunction %void None %fn_t",
                    "%entry = OpLabel"
                ]
                spv_text = "\n".join([line for line in types_and_vars if line]) + "\n" + body_text + "\nOpReturn\nOpFunctionEnd\n"
                bin_data = spirv_asm.assemble(spv_text)
                self.assertGreater(len(bin_data), 0)
            finally:
                rdna2spirv.MESH_COMPUTE = orig_mc

if __name__ == '__main__':
    unittest.main()
