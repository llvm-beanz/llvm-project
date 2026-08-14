//===- CFGGen.cpp - Seeded generator for CFG-shaped shaders ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/CFGGen.h"

#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#include <random>
#include <utility>

using namespace feme::cpu;
using namespace llvm;

namespace {

/// One not-yet-terminated basic block being built up: \p Name is fixed at
/// creation (so forward references to it from an as-yet-unemitted
/// terminator are always valid text), and \p Body accumulates its
/// non-terminator instructions until `closeBlock` appends a terminator and
/// files it away.
struct OpenBlock {
  std::string Name;
  std::string Body;
};

/// Builds one `generateCFGIR` result. See CFGGen.h's file comment for the
/// shape this emits and why an `alloca`-backed accumulator sidesteps
/// hand-placing `phi` nodes for arbitrarily-shaped (including irreducible)
/// control flow.
class CFGGenerator {
  const CFGGenOptions &Opts;
  std::mt19937_64 Rng;
  unsigned NextBlockId = 0;
  unsigned NextName = 0;
  unsigned NextTmp = 0;
  /// Every closed block's full text (`"name:\n  ...\n  <terminator>\n"`),
  /// in the order each was closed. The first block closed is always the
  /// function's own entry block (see `generate`), which is also the first
  /// one ever created -- exactly LLVM IR's requirement that a function's
  /// entry block come first in its text.
  SmallVector<std::string, 16> Blocks;

  std::string newName(StringRef Base) {
    return (Base + "." + Twine(NextName++)).str();
  }
  std::string newTmp() { return ("%t" + Twine(NextTmp++)).str(); }

  bool chance(double P) {
    return std::uniform_real_distribution<double>(0.0, 1.0)(Rng) < P;
  }
  unsigned randInt(unsigned Lo, unsigned Hi) {
    return std::uniform_int_distribution<unsigned>(Lo, Hi)(Rng);
  }

  OpenBlock newOpen(StringRef Base) { return OpenBlock{newName(Base), ""}; }

  /// Appends this block's own id to the accumulator: `acc = acc *
  /// 2654435761 + id` (Knuth's multiplicative hash constant), so the final
  /// accumulator value is a trace of every block an invocation visited.
  void appendFold(OpenBlock &B) {
    unsigned Id = NextBlockId++;
    std::string Old = newTmp(), Mul = newTmp(), New = newTmp();
    B.Body += "  " + Old + " = load i32, ptr %acc\n";
    B.Body += "  " + Mul + " = mul i32 " + Old + ", 2654435761\n";
    B.Body += "  " + New + " = add i32 " + Mul + ", " + Twine(Id).str() + "\n";
    B.Body += "  store i32 " + New + ", ptr %acc\n";
  }

  /// Appends an `i1` condition to \p B, either derived from the dispatch
  /// thread id (divergent) or the group id (uniform: a wave never spans
  /// more than one group) -- see `feme::cpu::WaveTTIImpl`'s classification
  /// of each.
  std::string appendCondition(OpenBlock &B) {
    bool Divergent = Opts.AllowDivergent && chance(0.5);
    std::string Masked = newTmp(), Cmp = newTmp();
    unsigned K = randInt(0, 3);
    B.Body +=
        "  " + Masked + " = and i32 " + (Divergent ? "%tid" : "%gid") + ", 3\n";
    B.Body +=
        "  " + Cmp + " = icmp eq i32 " + Masked + ", " + Twine(K).str() + "\n";
    return Cmp;
  }

  void closeBlock(OpenBlock &B, const Twine &Terminator) {
    Blocks.push_back(B.Name + ":\n" + B.Body + "  " + Terminator.str() + "\n");
  }

  /// Emits an `if`/`else` that reconverges immediately: \p B branches on a
  /// fresh condition to two fresh blocks, each of which (after its own
  /// nested construct, if any) branches to a fresh merge block, which is
  /// returned still open.
  OpenBlock genIf(OpenBlock B, unsigned Depth, unsigned &Budget) {
    std::string Cond = appendCondition(B);
    OpenBlock T = newOpen("if.then");
    OpenBlock F = newOpen("if.else");
    closeBlock(B,
               "br i1 " + Cond + ", label %" + T.Name + ", label %" + F.Name);
    OpenBlock TEnd = genOneConstruct(std::move(T), Depth - 1, Budget);
    OpenBlock FEnd = genOneConstruct(std::move(F), Depth - 1, Budget);
    OpenBlock End = newOpen("if.end");
    closeBlock(TEnd, "br label %" + End.Name);
    closeBlock(FEnd, "br label %" + End.Name);
    return End;
  }

  /// Emits a counted loop (trip count 2-4, a compile-time constant so a
  /// generated shader always terminates) with a header/body/latch/exit
  /// shape, and, each with independent probability, a divergent `break`
  /// (skips straight to \p Exit) and a divergent `continue` (skips straight
  /// to \p Latch) inside the body -- see "loop-break"/"loop-continue" in
  /// the named-shape corpus this mirrors.
  OpenBlock genLoop(OpenBlock B, unsigned Depth, unsigned &Budget) {
    OpenBlock Header = newOpen("loop.header");
    closeBlock(B, "br label %" + Header.Name);

    std::string CounterPhi = newTmp();
    std::string Inc = newTmp();
    unsigned TripCount = randInt(2, 4);

    OpenBlock Body = newOpen("loop.body");
    OpenBlock Latch = newOpen("loop.latch");
    OpenBlock Exit = newOpen("loop.exit");

    std::string Cond = newTmp();
    Header.Body += "  " + CounterPhi + " = phi i32 [ 0, %" + B.Name + " ], [ " +
                   Inc + ", %" + Latch.Name + " ]\n";
    Header.Body += "  " + Cond + " = icmp slt i32 " + CounterPhi + ", " +
                   Twine(TripCount).str() + "\n";
    closeBlock(Header, "br i1 " + Cond + ", label %" + Body.Name + ", label %" +
                           Exit.Name);

    appendFold(Body);
    if (Opts.AllowLoops && chance(0.4)) {
      std::string BreakCond = appendCondition(Body);
      OpenBlock Cont = newOpen("loop.body.postbreak");
      closeBlock(Body, "br i1 " + BreakCond + ", label %" + Exit.Name +
                           ", label %" + Cont.Name);
      Body = std::move(Cont);
      appendFold(Body);
    }
    if (Opts.AllowLoops && chance(0.4)) {
      std::string ContinueCond = appendCondition(Body);
      OpenBlock Work = newOpen("loop.body.postcontinue");
      closeBlock(Body, "br i1 " + ContinueCond + ", label %" + Latch.Name +
                           ", label %" + Work.Name);
      Body = std::move(Work);
      appendFold(Body);
    }
    if (Depth > 0 && Budget > 0)
      Body = genOneConstruct(std::move(Body), Depth - 1, Budget);
    closeBlock(Body, "br label %" + Latch.Name);

    Latch.Body += "  " + Inc + " = add i32 " + CounterPhi + ", 1\n";
    closeBlock(Latch, "br label %" + Header.Name);

    return Exit;
  }

  /// Emits the irreducible shape "irreducible-two-entry" in the named-shape
  /// corpus mirrors: two blocks, each directly reachable from \p B and each
  /// able to branch to the other, with no single block dominating both --
  /// exactly what `FixIrreducible` exists to resolve before `StructurizeCFG`
  /// runs.
  ///
  /// `ACond`/`BCond` are each derived from `%tid`/`%gid` (see
  /// `appendCondition`), which do not change across a hop between \p A and
  /// \p Bb, so either condition alone could be `false` for every hop a
  /// given invocation ever takes, bouncing between the two blocks forever.
  /// A shared bounce counter (an `alloca` like `appendFold`'s accumulator)
  /// bounds that: each block also exits once the counter reaches
  /// `MaxBounces`, so the shape stays irreducible (\p A and \p Bb remain
  /// mutually reachable, dominated by neither) while still guaranteeing
  /// termination -- the same guarantee `genLoop`'s constant trip count
  /// gives its own cycle.
  OpenBlock genIrreducible(OpenBlock B) {
    constexpr unsigned MaxBounces = 4;

    std::string EntryCond = appendCondition(B);
    OpenBlock A = newOpen("irred.a");
    OpenBlock Bb = newOpen("irred.b");
    OpenBlock Exit = newOpen("irred.exit");
    std::string CounterPtr = newTmp();
    B.Body += "  " + CounterPtr + " = alloca i32\n";
    B.Body += "  store i32 0, ptr " + CounterPtr + "\n";
    closeBlock(B, "br i1 " + EntryCond + ", label %" + A.Name + ", label %" +
                      Bb.Name);

    // Folds \p Block's id in, bumps the shared bounce counter, and returns
    // the (already fresh) exit condition to branch on: the block's own
    // random condition, forced to `true` once the counter reaches the cap.
    auto genHop = [&](OpenBlock &Block) -> std::string {
      appendFold(Block);
      std::string Count = newTmp(), Inc = newTmp(), AtCap = newTmp();
      Block.Body += "  " + Count + " = load i32, ptr " + CounterPtr + "\n";
      Block.Body += "  " + Inc + " = add i32 " + Count + ", 1\n";
      Block.Body += "  store i32 " + Inc + ", ptr " + CounterPtr + "\n";
      Block.Body += "  " + AtCap + " = icmp sge i32 " + Inc + ", " +
                    Twine(MaxBounces).str() + "\n";
      std::string RandCond = appendCondition(Block);
      std::string ExitCond = newTmp();
      Block.Body +=
          "  " + ExitCond + " = or i1 " + RandCond + ", " + AtCap + "\n";
      return ExitCond;
    };

    std::string ACond = genHop(A);
    closeBlock(A, "br i1 " + ACond + ", label %" + Exit.Name + ", label %" +
                      Bb.Name);

    std::string BCond = genHop(Bb);
    closeBlock(Bb, "br i1 " + BCond + ", label %" + Exit.Name + ", label %" +
                       A.Name);

    return Exit;
  }

  /// Folds \p B's own id in, then, with some probability (and while both
  /// the depth and construct budget allow it), picks one nested construct
  /// kind uniformly among those \p Opts enables. Returns the still-open
  /// block control continues at.
  OpenBlock genOneConstruct(OpenBlock B, unsigned Depth, unsigned &Budget) {
    appendFold(B);
    if (Depth == 0 || Budget == 0 || !chance(0.6))
      return B;
    --Budget;

    SmallVector<unsigned, 3> Kinds{0}; // 0 = if, always available
    if (Opts.AllowLoops)
      Kinds.push_back(1);
    if (Opts.AllowUnstructured)
      Kinds.push_back(2);
    switch (Kinds[randInt(0, Kinds.size() - 1)]) {
    case 0:
      return genIf(std::move(B), Depth, Budget);
    case 1:
      return genLoop(std::move(B), Depth, Budget);
    case 2:
      return genIrreducible(std::move(B));
    }
    llvm_unreachable("unhandled construct kind");
  }

public:
  CFGGenerator(const CFGGenOptions &Opts) : Opts(Opts), Rng(Opts.Seed) {}

  std::string generate() {
    OpenBlock Entry = newOpen("entry");
    Entry.Body += "  %acc = alloca i32\n";
    Entry.Body += "  store i32 0, ptr %acc\n";
    Entry.Body += "  %tid = call i32 @llvm.dx.thread.id(i32 0)\n";
    Entry.Body += "  %gid = call i32 @llvm.dx.group.id(i32 0)\n";
    Entry.Body += "  %h = call target(\"dx.RawBuffer\", i8, 1, 0) "
                  "@llvm.dx.resource.handlefromheap(i32 0, i1 false)\n";

    unsigned Budget = Opts.MaxConstructs;
    OpenBlock Cur = std::move(Entry);
    while (Budget > 0)
      Cur = genOneConstruct(std::move(Cur), Opts.MaxDepth, Budget);

    std::string AccVal = newTmp(), Offset = newTmp();
    Cur.Body += "  " + AccVal + " = load i32, ptr %acc\n";
    Cur.Body += "  " + Offset + " = mul i32 %tid, 4\n";
    Cur.Body += "  call void @llvm.dx.resource.store.rawbuffer.i32(target("
                "\"dx.RawBuffer\", i8, 1, 0) %h, i32 " +
                Offset + ", i32 poison, i32 " + AccVal + ")\n";
    closeBlock(Cur, "ret void");

    std::string Result;
    raw_string_ostream OS(Result);
    OS << "define void @main() #0 {\n";
    for (const std::string &Block : Blocks)
      OS << Block;
    OS << "}\n"
       << "declare i32 @llvm.dx.thread.id(i32)\n"
       << "declare i32 @llvm.dx.group.id(i32)\n"
       << "declare target(\"dx.RawBuffer\", i8, 1, 0) "
          "@llvm.dx.resource.handlefromheap(i32, i1)\n"
       << "declare void @llvm.dx.resource.store.rawbuffer.i32(\n"
       << "    target(\"dx.RawBuffer\", i8, 1, 0), i32, i32, i32)\n"
       << "attributes #0 = { \"hlsl.shader\"=\"compute\" "
          "\"hlsl.numthreads\"=\"4,1,1\" }\n";
    return Result;
  }
};

} // namespace

std::string feme::cpu::generateCFGIR(const CFGGenOptions &Opts) {
  return CFGGenerator(Opts).generate();
}
