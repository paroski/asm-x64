/**
 * Copyright (c) 2009-2016 Andrew J. Paroski
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/**
 * Copyright (c) 2010-2016 Facebook, Inc. (http://www.facebook.com)
 *
 * This source file is subject to version 3.01 of the PHP license,
 * that is bundled with this package in the file LICENSE, and is
 * available through the world-wide-web at the following url:
 *   http://www.php.net/license/3_01.txt
 *
 * If you did not receive a copy of the PHP license and are unable to
 * obtain it through the world-wide-web, please send a note to
 * license@php.net so we can mail you a copy immediately.
 */

#include "asm-x64.h"

void X64Assembler::emitModrmSibAndDisp(int r, const MemoryRef& mr)
{
  int br = int(mr.base);
  int ir = int(mr.index);
  int s = mr.scale;
  int disp = mr.disp;
  bool ripRelative = mr.ripRelative;
  assert(ir != int(reg::rsp));
  // Determine the size of the displacement. If the disp == 0 and 'br' is
  // not rbp or r13, then we don't need to emit a displacement at all.
  int dispSize =
    (disp == 0 && br != int(reg::rbp) && br != int(reg::r13)) ? sz::nosize :
    signedValueFits(disp, sz::byte) ? sz::byte : sz::dword;
  // Set 'mod' based on the size of the displacement
  int mod = (dispSize == sz::nosize) ? 0 : (dispSize == sz::byte) ? 1 : 2;
  // Indicate RIP-relative mode if appropriate by setting mod = 0 and
  // br = rbp. RIP-relative mode always uses 4 bytes for the displacement.
  if (ripRelative) {
    assert(br == int(reg::noreg));
    assert(ir == int(reg::noreg));
    mod = 0;
    br = int(reg::rbp);
    dispSize = sz::dword;
  }
  // We need a SIB byte if any of the following conditions are true:
  //   1. The base register is rsp or r12.
  //   2. We're doing a baseless disp access that is not RIP-relative.
  //   3. We're using an index register.
  bool sibIsNeeded =
    br == int(reg::rsp) || br == int(reg::r12) ||
    br == int(reg::noreg) || ir != int(reg::noreg);
  // Handle special cases for 'br'
  if (br == int(reg::noreg)) {
    // If 'reg::noreg' was specified for 'br', we use the encoding for the
    // rbp register, and we must set mod=0 and "upgrade" to a DWORD-sized
    // displacement
    br = int(reg::rbp);
    dispSize = sz::dword;
    mod = 0;
  }
  // Emit modr/m
  emitModrm(mod, r, sibIsNeeded ? int(reg::rsp) : br);
  // Emit the SIB byte if needed
  if (sibIsNeeded) {
    // s:                               0  1  2   3  4   5   6   7  8
    static const int scaleLookup[] = { -1, 0, 1, -1, 2, -1, -1, -1, 3 };
    assert(s > 0 && s <= 8);
    int scale = scaleLookup[s];
    assert(scale != -1);
    // If 'reg::noreg' was specified for 'ir', we use
    // the encoding for the rsp register
    ir = (ir != int(reg::noreg)) ? ir : int(reg::rsp);
    byte((scale << 6) | ((ir & 7) << 3) | (br & 7));
  }
  // Emit displacement if needed
  if (dispSize == sz::dword) {
    dword(disp);
  } else if (dispSize == sz::byte) {
    byte(disp & 0xFF);
  }
}

void patchJcc(CodeAddress instrAddr, CodeAddress targetAddr)
{
  assert(instrAddr[0] == 0x0F && (instrAddr[1] & 0xF0) == 0x80);
  int64_t delta = targetAddr - (instrAddr + 6); // 2 for opcode, 4 for offset
  assert(signedValueFits(delta, sz::dword));
  *(int32_t*)(instrAddr + 2) = static_cast<int32_t>(delta);
}

void patchJcc8(CodeAddress instrAddr, CodeAddress targetAddr)
{
  assert((instrAddr[0] & 0xF0) == 0x70);
  int64_t delta = targetAddr - (instrAddr + 2); // 1 for opcode, 4 for offset
  assert(signedValueFits(delta, sz::byte));
  *(int8_t*)(instrAddr + 1) = static_cast<int8_t>(delta);
}

void patchJmp(CodeAddress instrAddr, CodeAddress targetAddr)
{
  assert(instrAddr[0] == 0xE9);
  int64_t delta = targetAddr - (instrAddr + 5); // 1 for opcode, 4 for offset
  assert(signedValueFits(delta, sz::dword));
  *(int32_t*)(instrAddr + 1) = static_cast<int32_t>(delta);
}

void patchJmp8(CodeAddress instrAddr, CodeAddress targetAddr)
{
  assert(instrAddr[0] == 0xEB);
  int64_t delta = targetAddr - (instrAddr + 2); // 1 for opcode, 1 for offset
  assert(signedValueFits(delta, sz::byte));
  *(int8_t*)(instrAddr + 1) = static_cast<int8_t>(delta);
}

void patchCall(CodeAddress instrAddr, CodeAddress targetAddr)
{
  assert(instrAddr[0] == 0xE8);
  int64_t delta = targetAddr - (instrAddr + 5); // 1 for opcode, 4 for offset
  assert(signedValueFits(delta, sz::dword));
  *(int32_t*)(instrAddr + 1) = static_cast<int32_t>(delta);
}

#define X64_INSTR_NO_ARGS(INSTR_NAME) \
  void X64Assembler::INSTR_NAME () \
  { \
    emitNoArgs(instr_ ## INSTR_NAME); \
  }

#define X64_INSTR_I(INSTR_NAME) \
  void X64Assembler::INSTR_NAME (int64_t imm) \
  { \
    emitI(instr_ ## INSTR_NAME, imm); \
  }

#define X64_INSTR_R_Q(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## q (Reg64 r) \
  { \
    emitR(instr_ ## INSTR_NAME, rn(r)); \
  }

#define X64_INSTR_R_D(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## d (Reg32 r) \
  { \
    emitR(instr_ ## INSTR_NAME, rn(r), sz::dword); \
  }

#define X64_INSTR_R_W(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## w (Reg16 r) \
  { \
    emitR(instr_ ## INSTR_NAME, rn(r), sz::word); \
  }

#define X64_INSTR_R_B(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## b (Reg8 r) \
  { \
    emitR(instr_ ## INSTR_NAME, rn(r), sz::byte); \
  }

#define X64_INSTR_M_Q(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## q (const MemoryRef& mr) \
  { \
    emitM(instr_ ## INSTR_NAME, mr); \
  }

#define X64_INSTR_M_D(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## d (const MemoryRef& mr) \
  { \
    emitM(instr_ ## INSTR_NAME, mr, sz::dword); \
  }

#define X64_INSTR_M_W(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## w (const MemoryRef& mr) \
  { \
    emitM(instr_ ## INSTR_NAME, mr, sz::word); \
  }

#define X64_INSTR_M_B(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## b (const MemoryRef& mr) \
  { \
    emitM(instr_ ## INSTR_NAME, mr, sz::byte); \
  }

#define X64_INSTR_RR_Q(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## q (Reg64 rsrc, Reg64 rdest) \
  { \
    emitRR(instr_ ## INSTR_NAME, rsrc, rdest); \
  }

#define X64_INSTR_RR_D(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## d (Reg32 rsrc, Reg32 rdest) \
  { \
    emitRR(instr_ ## INSTR_NAME, rsrc, rdest, sz::dword); \
  }

#define X64_INSTR_RR_W(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## w (Reg16 rsrc, Reg16 rdest) \
  { \
    emitRR(instr_ ## INSTR_NAME, rsrc, rdest, sz::word); \
  }

#define X64_INSTR_RR_B(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## b (Reg8 rsrc, Reg8 rdest) \
  { \
    emitRR(instr_ ## INSTR_NAME, rsrc, rdest, sz::byte); \
  }

#define X64_INSTR_RR_XMM(INSTR_NAME) \
  void X64Assembler::INSTR_NAME (RegXMM rsrc, RegXMM rdest) \
  { \
    emitRR(instr_ ## INSTR_NAME, rsrc, rdest); \
  }

#define X64_INSTR_IR_Q(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## q (int64_t imm, Reg64 r) \
  { \
    emitIR(instr_ ## INSTR_NAME, imm, r); \
  }

#define X64_INSTR_IR_D(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## d (int64_t imm, Reg32 r) \
  { \
    emitIR(instr_ ## INSTR_NAME, imm, r, sz::dword); \
  }

#define X64_INSTR_IR_W(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## w (int64_t imm, Reg16 r) \
  { \
    emitIR(instr_ ## INSTR_NAME, imm, r, sz::word); \
  }

#define X64_INSTR_IR_B(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## b (int64_t imm, Reg8 r) \
  { \
    emitIR(instr_ ## INSTR_NAME, imm, r, sz::byte); \
  }

#define X64_INSTR_IR_XMM(INSTR_NAME) \
  void X64Assembler::INSTR_NAME (int64_t imm, RegXMM r) \
  { \
    emitIR(instr_ ## INSTR_NAME, imm, r); \
  }

#define X64_INSTR_IM_Q(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## q (int64_t imm, const MemoryRef& mr) \
  { \
    emitIM(instr_ ## INSTR_NAME, imm, mr); \
  }

#define X64_INSTR_IM_D(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## d (int64_t imm, const MemoryRef& mr) \
  { \
    emitIM(instr_ ## INSTR_NAME, imm, mr, sz::dword); \
  }

#define X64_INSTR_IM_W(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## w (int64_t imm, const MemoryRef& mr) \
  { \
    emitIM(instr_ ## INSTR_NAME, imm, mr, sz::word); \
  }

#define X64_INSTR_IM_B(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## b (int64_t imm, const MemoryRef& mr) \
  { \
    emitIM(instr_ ## INSTR_NAME, imm, mr, sz::byte); \
  }

#define X64_INSTR_RM_Q(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## q (Reg64 r, const MemoryRef& mr) \
  { \
    emitRM(instr_ ## INSTR_NAME, r, mr); \
  }

#define X64_INSTR_RM_D(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## d (Reg32 r, const MemoryRef& mr) \
  { \
    emitRM(instr_ ## INSTR_NAME, r, mr, sz::dword); \
  }

#define X64_INSTR_RM_W(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## w (Reg16 r, const MemoryRef& mr) \
  { \
    emitRM(instr_ ## INSTR_NAME, r, mr, sz::word); \
  }

#define X64_INSTR_RM_B(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## b (Reg8 r, const MemoryRef& mr) \
  { \
    emitRM(instr_ ## INSTR_NAME, r, mr, sz::byte); \
  }

#define X64_INSTR_RM_XMM(INSTR_NAME) \
  void X64Assembler::INSTR_NAME (RegXMM r, const MemoryRef& mr) \
  { \
    emitRM(instr_ ## INSTR_NAME, r, mr); \
  }

#define X64_INSTR_MR_Q(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## q (const MemoryRef& mr, Reg64 r) \
  { \
    emitMR(instr_ ## INSTR_NAME, mr, r); \
  }

#define X64_INSTR_MR_D(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## d (const MemoryRef& mr, Reg32 r) \
  { \
    emitMR(instr_ ## INSTR_NAME, mr, r, sz::dword); \
  }

#define X64_INSTR_MR_W(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## w (const MemoryRef& mr, Reg16 r) \
  { \
    emitMR(instr_ ## INSTR_NAME, mr, r, sz::word); \
  }

#define X64_INSTR_MR_B(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## b (const MemoryRef& mr, Reg8 r) \
  { \
    emitMR(instr_ ## INSTR_NAME, mr, r, sz::byte); \
  }

#define X64_INSTR_MR_XMM(INSTR_NAME) \
  void X64Assembler::INSTR_NAME (const MemoryRef& mr, RegXMM r) \
  { \
    emitMR(instr_ ## INSTR_NAME, mr, r); \
  }

#define X64_INSTR_IRR_Q(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## q (int64_t imm, Reg64 rsrc, Reg64 rdest) \
  { \
    emitIRR(instr_ ## INSTR_NAME, imm, rsrc, rdest); \
  }

#define X64_INSTR_IRR_D(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## d (int64_t imm, Reg32 rsrc, Reg32 rdest) \
  { \
    emitIRR(instr_ ## INSTR_NAME, imm, rsrc, rdest, sz::dword); \
  }

#define X64_INSTR_IRR_W(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## w (int64_t imm, Reg16 rsrc, Reg16 rdest) \
  { \
    emitIRR(instr_ ## INSTR_NAME, imm, rsrc, rdest, sz::word); \
  }

#define X64_INSTR_IRR_B(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## b (int64_t imm, Reg8 rsrc, Reg8 rdest) \
  { \
    emitIRR(instr_ ## INSTR_NAME, imm, rsrc, rdest, sz::byte); \
  }

#define X64_INSTR_IRR_XMM(INSTR_NAME) \
  void X64Assembler::INSTR_NAME (int64_t imm, RegXMM rsrc, RegXMM rdest) \
  { \
    emitIRR(instr_ ## INSTR_NAME, imm, rsrc, rdest); \
  }

#define X64_INSTR_IRM_Q(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## q (int64_t imm, Reg64 r, const MemoryRef& mr) \
  { \
    emitIRM(instr_ ## INSTR_NAME, imm, r, mr); \
  }

#define X64_INSTR_IRM_D(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## d (int64_t imm, Reg32 r, const MemoryRef& mr) \
  { \
    emitIRM(instr_ ## INSTR_NAME, imm, r, mr, sz::dword); \
  }

#define X64_INSTR_IRM_W(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## w (int64_t imm, Reg16 r, const MemoryRef& mr) \
  { \
    emitIRM(instr_ ## INSTR_NAME, imm, r, mr, sz::word); \
  }

#define X64_INSTR_IRM_B(INSTR_NAME) \
  void X64Assembler::INSTR_NAME ## b (int64_t imm, Reg8 r, const MemoryRef& mr) \
  { \
    emitIRM(instr_ ## INSTR_NAME, imm, r, mr, sz::byte); \
  }

#define X64_INSTR_IMR_XMM(INSTR_NAME) \
  void X64Assembler::INSTR_NAME (int64_t imm, const MemoryRef& mr, RegXMM r) \
  { \
    emitIMR(instr_ ## INSTR_NAME, imm, mr, r); \
  }

X64_INSTRS()

#undef X64_INSTR_NO_ARGS
#undef X64_INSTR_I
#undef X64_INSTR_R_Q
#undef X64_INSTR_R_D
#undef X64_INSTR_R_W
#undef X64_INSTR_R_B
#undef X64_INSTR_M_Q
#undef X64_INSTR_M_D
#undef X64_INSTR_M_W
#undef X64_INSTR_M_B
#undef X64_INSTR_RR_Q
#undef X64_INSTR_RR_D
#undef X64_INSTR_RR_W
#undef X64_INSTR_RR_B
#undef X64_INSTR_RR_XMM
#undef X64_INSTR_IR_Q
#undef X64_INSTR_IR_D
#undef X64_INSTR_IR_W
#undef X64_INSTR_IR_B
#undef X64_INSTR_IR_XMM
#undef X64_INSTR_IM_Q
#undef X64_INSTR_IM_D
#undef X64_INSTR_IM_W
#undef X64_INSTR_IM_B
#undef X64_INSTR_RM_Q
#undef X64_INSTR_RM_D
#undef X64_INSTR_RM_W
#undef X64_INSTR_RM_B
#undef X64_INSTR_RM_XMM
#undef X64_INSTR_MR_Q
#undef X64_INSTR_MR_D
#undef X64_INSTR_MR_W
#undef X64_INSTR_MR_B
#undef X64_INSTR_MR_XMM
#undef X64_INSTR_IRR_Q
#undef X64_INSTR_IRR_D
#undef X64_INSTR_IRR_W
#undef X64_INSTR_IRR_B
#undef X64_INSTR_IRR_XMM
#undef X64_INSTR_IRM_Q
#undef X64_INSTR_IRM_D
#undef X64_INSTR_IRM_W
#undef X64_INSTR_IRM_B
#undef X64_INSTR_IMR_XMM

void X64Assembler::cdq()
{
  emitNoArgs(instr_cqo, sz::dword);
}

void X64Assembler::cwde()
{
  emitNoArgs(instr_cdqe, sz::dword);
}

void X64Assembler::cwd()
{
  emitNoArgs(instr_cqo, sz::word);
}

void X64Assembler::cbw()
{
  emitNoArgs(instr_cdqe, sz::word);
}

void X64Assembler::pushfw()
{
  emitNoArgs(instr_pushf, sz::word);
}

void X64Assembler::popfw()
{
  emitNoArgs(instr_popf, sz::word);
}

void X64Assembler::pushw(int64_t imm)
{
  emitI(instr_push, imm, sz::word);
}

void X64Assembler::cmpxchg16b(const MemoryRef& mr)
{
  emitM(instr_cmpxchg16b, mr);
}

void X64Assembler::cmpxchg8b(const MemoryRef& mr)
{
  emitM(instr_cmpxchg16b, mr, sz::dword);
}

void X64Assembler::imulq(Reg64 rsrc, Reg64 rdest)
{
  emitRR(instr_imul2, rsrc, rdest);
}

void X64Assembler::imuld(Reg32 rsrc, Reg32 rdest)
{
  emitRR(instr_imul2, rsrc, rdest, sz::dword);
}

void X64Assembler::imulw(Reg16 rsrc, Reg16 rdest)
{
  emitRR(instr_imul2, rsrc, rdest, sz::word);
}

void X64Assembler::movzxwq(Reg16 rsrc, Reg64 rdest)
{
  // XXX We could always use movzxwd instead and potentially avoid the
  // need for the REX byte.
  emitRR(instr_movzxw, rsrc, rdest);
}

void X64Assembler::movzxwd(Reg16 rsrc, Reg32 rdest)
{
  emitRR(instr_movzxw, rsrc, rdest, sz::dword);
}

void X64Assembler::movzxbq(Reg8 rsrc, Reg64 rdest)
{
  // XXX We could always use movzxbd instead and potentially avoid the
  // need for the REX byte.
  emitRR(instr_movzxb, rsrc, rdest);
}

void X64Assembler::movzxbd(Reg8 rsrc, Reg32 rdest)
{
  emitRR(instr_movzxb, rsrc, rdest, sz::dword);
}

void X64Assembler::movzxbw(Reg8 rsrc, Reg16 rdest)
{
  emitRR(instr_movzxb, rsrc, rdest, sz::word);
}

void X64Assembler::movsxdq(Reg32 rsrc, Reg64 rdest)
{
  emitRR(instr_movsxdq, rsrc, rdest);
}

void X64Assembler::movsxwq(Reg16 rsrc, Reg64 rdest)
{
  emitRR(instr_movsxw, rsrc, rdest);
}

void X64Assembler::movsxwd(Reg16 rsrc, Reg32 rdest)
{
  emitRR(instr_movsxw, rsrc, rdest, sz::dword);
}

void X64Assembler::movsxbq(Reg8 rsrc, Reg64 rdest)
{
  emitRR(instr_movsxb, rsrc, rdest);
}

void X64Assembler::movsxbd(Reg8 rsrc, Reg32 rdest)
{
  emitRR(instr_movsxb, rsrc, rdest, sz::dword);
}

void X64Assembler::movsxbw(Reg8 rsrc, Reg16 rdest)
{
  emitRR(instr_movsxb, rsrc, rdest, sz::word);
}

void X64Assembler::movq(Reg64 rsrc, RegXMM rdest)
{
  emitRR(instr_gpr2xmm, rsrc, rdest);
}

void X64Assembler::movq(RegXMM rsrc, Reg64 rdest)
{
  emitRR(instr_xmm2gpr, rsrc, rdest);
}

void X64Assembler::movd(Reg32 rsrc, RegXMM rdest)
{
  emitRR(instr_gpr2xmm, rsrc, rdest, sz::dword);
}

void X64Assembler::movd(RegXMM rsrc, Reg32 rdest)
{
  emitRR(instr_xmm2gpr, rsrc, rdest, sz::dword);
}

void X64Assembler::cvtsi2sdq(Reg64 rsrc, RegXMM rdest)
{
  emitRR(instr_cvtsi2sd, rsrc, rdest);
}

void X64Assembler::cvtsi2sdd(Reg32 rsrc, RegXMM rdest)
{
  emitRR(instr_cvtsi2sd, rsrc, rdest, sz::dword);
}

void X64Assembler::cvttsd2siq(RegXMM rsrc, Reg64 rdest)
{
  emitRR(instr_cvttsd2si, rsrc, rdest);
}

void X64Assembler::cvttsd2sid(RegXMM rsrc, Reg32 rdest)
{
  emitRR(instr_cvttsd2si, rsrc, rdest, sz::dword);
}

void X64Assembler::movq(RegXMM r, const MemoryRef& mr)
{
  emitRM(instr_xmm2gpr, r, mr);
}

void X64Assembler::movd(RegXMM r, const MemoryRef& mr)
{
  emitRM(instr_xmm2gpr, r, mr, sz::dword);
}

void X64Assembler::imulq(const MemoryRef& mr, Reg64 r)
{
  emitMR(instr_imul2, mr, r);
}

void X64Assembler::imuld(const MemoryRef& mr, Reg32 r)
{
  emitMR(instr_imul2, mr, r, sz::dword);
}

void X64Assembler::imulw(const MemoryRef& mr, Reg16 r)
{
  emitMR(instr_imul2, mr, r, sz::word);
}

void X64Assembler::movzxwq(const MemoryRef& mr, Reg64 r)
{
  // XXX We could always use movzxwd instead and potentially avoid the
  // need for the REX byte.
  emitMR(instr_movzxw, mr, r);
}

void X64Assembler::movzxwd(const MemoryRef& mr, Reg32 r)
{
  emitMR(instr_movzxw, mr, r, sz::dword);
}

void X64Assembler::movzxbq(const MemoryRef& mr, Reg64 r)
{
  // XXX We could always use movzxbd instead and potentially avoid the
  // need for the REX byte.
  emitMR(instr_movzxb, mr, r);
}

void X64Assembler::movzxbd(const MemoryRef& mr, Reg32 r)
{
  emitMR(instr_movzxb, mr, r, sz::dword);
}

void X64Assembler::movzxbw(const MemoryRef& mr, Reg16 r)
{
  emitMR(instr_movzxb, mr, r, sz::word);
}

void X64Assembler::movsxdq(const MemoryRef& mr, Reg64 r)
{
  emitMR(instr_movsxdq, mr, r);
}

void X64Assembler::movsxwq(const MemoryRef& mr, Reg64 r)
{
  emitMR(instr_movsxw, mr, r);
}

void X64Assembler::movsxwd(const MemoryRef& mr, Reg32 r)
{
  emitMR(instr_movsxw, mr, r, sz::dword);
}

void X64Assembler::movsxbq(const MemoryRef& mr, Reg64 r)
{
  emitMR(instr_movsxb, mr, r);
}

void X64Assembler::movsxbd(const MemoryRef& mr, Reg32 r)
{
  emitMR(instr_movsxb, mr, r, sz::dword);
}

void X64Assembler::movsxbw(const MemoryRef& mr, Reg16 r)
{
  emitMR(instr_movsxb, mr, r, sz::word);
}

void X64Assembler::movq(const MemoryRef& mr, RegXMM r)
{
  emitMR(instr_gpr2xmm, mr, r);
}

void X64Assembler::movd(const MemoryRef& mr, RegXMM r)
{
  emitMR(instr_gpr2xmm, mr, r, sz::dword);
}

void X64Assembler::cvtsi2sdq(const MemoryRef& mr, RegXMM r)
{
  emitMR(instr_cvtsi2sd, mr, r);
}

void X64Assembler::cvtsi2sdd(const MemoryRef& mr, RegXMM r)
{
  emitMR(instr_cvtsi2sd, mr, r, sz::dword);
}

void X64Assembler::cvttsd2siq(const MemoryRef& mr, Reg64 r)
{
  emitMR(instr_cvttsd2si, mr, r);
}

void X64Assembler::cvttsd2sid(const MemoryRef& mr, Reg32 r)
{
  emitMR(instr_cvttsd2si, mr, r, sz::dword);
}

void X64Assembler::imulq(int64_t imm, Reg64 rsrc, Reg64 rdest)
{
  emitIRR(instr_imul3, imm, rsrc, rdest);
}

void X64Assembler::imuld(int64_t imm, Reg32 rsrc, Reg32 rdest)
{
  emitIRR(instr_imul3, imm, rsrc, rdest, sz::dword);
}

void X64Assembler::imulw(int64_t imm, Reg16 rsrc, Reg16 rdest)
{
  emitIRR(instr_imul3, imm, rsrc, rdest, sz::word);
}

void X64Assembler::imulq(int64_t imm, const MemoryRef& mr, Reg64 r)
{
  emitIMR(instr_imul3, imm, mr, r);
}

void X64Assembler::imuld(int64_t imm, const MemoryRef& mr, Reg32 r)
{
  emitIMR(instr_imul3, imm, mr, r, sz::dword);
}

void X64Assembler::imulw(int64_t imm, const MemoryRef& mr, Reg16 r)
{
  emitIMR(instr_imul3, imm, mr, r, sz::word);
}

void X64Assembler::jcc(int jcond, int64_t imm)
{
  emitCI(instr_jcc, jcond, imm);
}

void X64Assembler::jcc8(int jcond, int64_t imm)
{
  emitCI(instr_jcc8, jcond, imm);
}

void X64Assembler::setccb(int jcond, Reg8 r)
{
  emitCR(instr_setcc, jcond, r);
}

void X64Assembler::setccb(int jcond, const MemoryRef& mr)
{
  emitCM(instr_setcc, jcond, mr);
}

void X64Assembler::cmovccq(int jcond, Reg64 rsrc, Reg64 rdest)
{
  emitCRR(instr_cmovcc, jcond, rsrc, rdest);
}

void X64Assembler::cmovccd(int jcond, Reg32 rsrc, Reg32 rdest)
{
  emitCRR(instr_cmovcc, jcond, rsrc, rdest, sz::dword);
}

void X64Assembler::cmovccw(int jcond, Reg16 rsrc, Reg16 rdest)
{
  emitCRR(instr_cmovcc, jcond, rsrc, rdest, sz::word);
}

void X64Assembler::cmovccq(int jcond, const MemoryRef& mr, Reg64 r)
{
  emitCMR(instr_cmovcc, jcond, mr, r);
}

void X64Assembler::cmovccd(int jcond, const MemoryRef& mr, Reg32 r)
{
  emitCMR(instr_cmovcc, jcond, mr, r, sz::dword);
}

void X64Assembler::cmovccw(int jcond, const MemoryRef& mr, Reg16 r)
{
  emitCMR(instr_cmovcc, jcond, mr, r, sz::word);
}
