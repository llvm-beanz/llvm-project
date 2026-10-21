; RUN: feme-opt --llvm -passes=feme-cpu-linearize,verify -S %s | FileCheck %s

; Roadmap ENV5(c)(4): a deep, 24-way switch-decode chain inside a
; hull-stage `main.patchconstant` entry point (the `struct16arr3_tessc`
; shape from
; `dEQP-VK.spirv_assembly.instruction.graphics.float16.opcompositeextract.
; struct16arr3_tessc`) exposed two separate bugs in
; `foldRedundantFlowBlock`'s relay-chain handling, both triggered by this
; same CFG shape but only the second reachable once the first was fixed.
;
; Bug 1 (dominance violation): the relay-chain walk (used by both the
; read-only precheck and, historically, the mutating forwarding pass) only
; checked whether a candidate relay block (e.g. `Flow73`) had an empty phi
; list to decide it was a transparent pass-through safe to walk past. It
; never checked whether that relay had exactly one real predecessor.
; `Flow73` here has two real predecessors (`Flow55` and the
; `Flow67.Flow73_crit_edge` relay) but no phi of its own (every value that
; happened to flow through it historically coincided), so the walk
; incorrectly treated it as transparent and forwarded a value defined only
; in `Flow68` straight through to `Flow66`'s phi as though it dominated
; both of `Flow73`'s predecessors -- an actual "Instruction does not
; dominate all uses!" verifier failure. Fixed by additionally requiring
; `Merge->getSinglePredecessor()` (exactly one real predecessor) before
; continuing the walk.
;
; Bug 2 (use-after-erase abort), only reachable once bug 1 was fixed: the
; *mutating* forwarding loop used to independently recompute this same
; relay-chain walk a second time, after this same loop iteration's own
; redirect step (`PredTerm->setSuccessor(S, Target)`) had already retargeted
; one predecessor's terminator to point at `Target`. That redirect can
; transiently give `Target` a second real predecessor it didn't have when
; the read-only pass walked it (since the block being folded, `BB`, still
; exists and still branches there until erased at the very end of the
; function), which -- combined with the new single-predecessor guard from
; bug 1's fix -- makes the *recomputed* mutating-loop walk stop one hop too
; early at a no-phi relay, silently skipping the forward of a value defined
; in `BB` into the real downstream merge phi. That phi keeps referencing
; the value defined in `BB`, which is destroyed when `BB` is erased,
; producing a "Uses remain when a value is destroyed!" / `UNREACHABLE
; executed at llvm/lib/IR/Value.cpp` hard abort (not a clean verifier
; diagnostic). Fixed by having the mutating loop directly reuse the
; read-only pass's own, already-correct `Merges[I]`/`IncomingBlocks[I]`
; results (computed before any mutation in this same call) instead of
; recomputing them against the now-partially-mutated CFG.
;
; Block/value names and shape preserved verbatim (only unused resource
; globals/declares and metadata trimmed) from the real failing case,
; isolated via `FEME_DUMP_IR_PRELINEARIZE=1` against the case above's
; compiled hull-stage `main.patchconstant`; see `Roadmap.md`'s
; `ENV5(c)(4)` entry and `VulkanCTSReport.md`'s `L486` entry for the full
; writeup.

; CHECK-LABEL: define void @main.patchconstant(
; CHECK-NOT: unsupported
; ModuleID = '<in-memory object>'
source_filename = "LLVMDialectModule"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32"

define void @main.patchconstant() #3 {
  %1 = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
  %2 = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
  %3 = call i32 @feme.stage.input.load.i32(i32 1, i32 0, i32 0, i32 0)
  %4 = icmp eq i32 %2, 0
  %5 = icmp eq i32 %3, 0
  %6 = and i1 %4, %5
  br i1 %6, label %7, label %.Flow88_crit_edge

.Flow88_crit_edge:                                ; preds = %0
  br label %Flow88

7:                                                ; preds = %0
  br label %8

Flow88:                                           ; preds = %.Flow88_crit_edge, %10
  br label %spirv_fn_136.exit

8:                                                ; preds = %Flow87._crit_edge, %7
  %.0 = phi i32 [ %12, %Flow87._crit_edge ], [ 0, %7 ]
  %9 = icmp slt i32 %.0, 24
  br i1 %9, label %11, label %.Flow87_crit_edge

.Flow87_crit_edge:                                ; preds = %8
  br label %Flow87

10:                                               ; preds = %Flow87
  br label %Flow88

11:                                               ; preds = %8
  br label %NodeBlock43

Flow87:                                           ; preds = %.Flow87_crit_edge, %spirv_fn_170.exit
  %12 = phi i32 [ %75, %spirv_fn_170.exit ], [ poison, %.Flow87_crit_edge ]
  %13 = phi i1 [ false, %spirv_fn_170.exit ], [ true, %.Flow87_crit_edge ]
  br i1 %13, label %10, label %Flow87._crit_edge

Flow87._crit_edge:                                ; preds = %Flow87
  br label %8

NodeBlock43:                                      ; preds = %11
  %Pivot44 = icmp sge i32 %.0, 12
  br i1 %Pivot44, label %NodeBlock41, label %NodeBlock43.Flow64_crit_edge

NodeBlock43.Flow64_crit_edge:                     ; preds = %NodeBlock43
  br label %Flow64

NodeBlock41:                                      ; preds = %NodeBlock43
  %Pivot42 = icmp sge i32 %.0, 18
  br i1 %Pivot42, label %NodeBlock39, label %NodeBlock41.Flow65_crit_edge

NodeBlock41.Flow65_crit_edge:                     ; preds = %NodeBlock41
  br label %Flow65

Flow64:                                           ; preds = %NodeBlock43.Flow64_crit_edge, %Flow66
  %14 = phi i1 [ %18, %Flow66 ], [ false, %NodeBlock43.Flow64_crit_edge ]
  %15 = phi i1 [ false, %Flow66 ], [ true, %NodeBlock43.Flow64_crit_edge ]
  br i1 %15, label %NodeBlock19, label %Flow64.Flow74_crit_edge

Flow64.Flow74_crit_edge:                          ; preds = %Flow64
  br label %Flow74

NodeBlock39:                                      ; preds = %NodeBlock41
  %Pivot40 = icmp sge i32 %.0, 21
  br i1 %Pivot40, label %NodeBlock37, label %NodeBlock39.Flow52_crit_edge

NodeBlock39.Flow52_crit_edge:                     ; preds = %NodeBlock39
  br label %Flow52

NodeBlock37:                                      ; preds = %NodeBlock39
  %Pivot38 = icmp sge i32 %.0, 22
  br i1 %Pivot38, label %NodeBlock35, label %NodeBlock37.Flow46_crit_edge

NodeBlock37.Flow46_crit_edge:                     ; preds = %NodeBlock37
  br label %Flow46

NodeBlock35:                                      ; preds = %NodeBlock37
  %Pivot36 = icmp sge i32 %.0, 23
  br i1 %Pivot36, label %74, label %NodeBlock35.Flow_crit_edge

NodeBlock35.Flow_crit_edge:                       ; preds = %NodeBlock35
  br label %Flow

Flow52:                                           ; preds = %NodeBlock39.Flow52_crit_edge, %Flow47
  %16 = phi i1 [ false, %Flow47 ], [ true, %NodeBlock39.Flow52_crit_edge ]
  br i1 %16, label %NodeBlock33, label %Flow52.Flow53_crit_edge

Flow52.Flow53_crit_edge:                          ; preds = %Flow52
  br label %Flow53

NodeBlock33:                                      ; preds = %Flow52
  %Pivot34 = icmp sge i32 %.0, 19
  br i1 %Pivot34, label %NodeBlock31, label %NodeBlock33.Flow50_crit_edge

NodeBlock33.Flow50_crit_edge:                     ; preds = %NodeBlock33
  br label %Flow50

NodeBlock31:                                      ; preds = %NodeBlock33
  %Pivot32 = icmp sge i32 %.0, 20
  br i1 %Pivot32, label %69, label %NodeBlock31.Flow48_crit_edge

NodeBlock31.Flow48_crit_edge:                     ; preds = %NodeBlock31
  br label %Flow48

Flow65:                                           ; preds = %NodeBlock41.Flow65_crit_edge, %Flow53
  %17 = phi i1 [ false, %Flow53 ], [ true, %NodeBlock41.Flow65_crit_edge ]
  br i1 %17, label %NodeBlock29, label %Flow65.Flow66_crit_edge

Flow65.Flow66_crit_edge:                          ; preds = %Flow65
  br label %Flow66

NodeBlock29:                                      ; preds = %Flow65
  %Pivot30 = icmp sge i32 %.0, 14
  br i1 %Pivot30, label %NodeBlock27, label %NodeBlock29.Flow67_crit_edge

NodeBlock29.Flow67_crit_edge:                     ; preds = %NodeBlock29
  br label %Flow67

Flow66:                                           ; preds = %Flow65.Flow66_crit_edge, %Flow73
  %18 = phi i1 [ %19, %Flow73 ], [ false, %Flow65.Flow66_crit_edge ]
  br label %Flow64

NodeBlock27:                                      ; preds = %NodeBlock29
  %Pivot28 = icmp sge i32 %.0, 15
  br i1 %Pivot28, label %NodeBlock25, label %NodeBlock27.Flow68_crit_edge

NodeBlock27.Flow68_crit_edge:                     ; preds = %NodeBlock27
  br label %Flow68

Flow67:                                           ; preds = %NodeBlock29.Flow67_crit_edge, %Flow72
  %19 = phi i1 [ %21, %Flow72 ], [ false, %NodeBlock29.Flow67_crit_edge ]
  %20 = phi i1 [ false, %Flow72 ], [ true, %NodeBlock29.Flow67_crit_edge ]
  br i1 %20, label %NodeBlock21, label %Flow67.Flow73_crit_edge

Flow67.Flow73_crit_edge:                          ; preds = %Flow67
  br label %Flow73

NodeBlock25:                                      ; preds = %NodeBlock27
  %Pivot26 = icmp sge i32 %.0, 16
  br i1 %Pivot26, label %LeafBlock23, label %NodeBlock25.Flow69_crit_edge

NodeBlock25.Flow69_crit_edge:                     ; preds = %NodeBlock25
  br label %Flow69

Flow68:                                           ; preds = %NodeBlock27.Flow68_crit_edge, %Flow71
  %21 = phi i1 [ %62, %Flow71 ], [ false, %NodeBlock27.Flow68_crit_edge ]
  %22 = phi i1 [ false, %Flow71 ], [ true, %NodeBlock27.Flow68_crit_edge ]
  br i1 %22, label %59, label %Flow68.Flow72_crit_edge

Flow68.Flow72_crit_edge:                          ; preds = %Flow68
  br label %Flow72

LeafBlock23:                                      ; preds = %NodeBlock25
  %SwitchLeaf24 = icmp eq i32 %.0, 16
  br i1 %SwitchLeaf24, label %64, label %LeafBlock23.Flow70_crit_edge

LeafBlock23.Flow70_crit_edge:                     ; preds = %LeafBlock23
  br label %Flow70

Flow72:                                           ; preds = %Flow68.Flow72_crit_edge, %59
  br label %Flow67

NodeBlock21:                                      ; preds = %Flow67
  %Pivot22 = icmp sge i32 %.0, 13
  br i1 %Pivot22, label %58, label %NodeBlock21.Flow54_crit_edge

NodeBlock21.Flow54_crit_edge:                     ; preds = %NodeBlock21
  br label %Flow54

Flow73:                                           ; preds = %Flow67.Flow73_crit_edge, %Flow55
  br label %Flow66

NodeBlock19:                                      ; preds = %Flow64
  %Pivot20 = icmp sge i32 %.0, 6
  br i1 %Pivot20, label %NodeBlock17, label %NodeBlock19.Flow75_crit_edge

NodeBlock19.Flow75_crit_edge:                     ; preds = %NodeBlock19
  br label %Flow75

Flow74:                                           ; preds = %Flow64.Flow74_crit_edge, %Flow81
  %23 = phi i1 [ %31, %Flow81 ], [ false, %Flow64.Flow74_crit_edge ]
  %24 = phi i1 [ %32, %Flow81 ], [ %14, %Flow64.Flow74_crit_edge ]
  br i1 %24, label %37, label %Flow74.Flow86_crit_edge

Flow74.Flow86_crit_edge:                          ; preds = %Flow74
  br label %Flow86

NodeBlock17:                                      ; preds = %NodeBlock19
  %Pivot18 = icmp sge i32 %.0, 8
  br i1 %Pivot18, label %NodeBlock15, label %NodeBlock17.Flow76_crit_edge

NodeBlock17.Flow76_crit_edge:                     ; preds = %NodeBlock17
  br label %Flow76

Flow75:                                           ; preds = %NodeBlock19.Flow75_crit_edge, %Flow80
  %25 = phi i1 [ %27, %Flow80 ], [ %14, %NodeBlock19.Flow75_crit_edge ]
  %26 = phi i1 [ false, %Flow80 ], [ true, %NodeBlock19.Flow75_crit_edge ]
  br i1 %26, label %NodeBlock7, label %Flow75.Flow81_crit_edge

Flow75.Flow81_crit_edge:                          ; preds = %Flow75
  br label %Flow81

NodeBlock15:                                      ; preds = %NodeBlock17
  %Pivot16 = icmp sge i32 %.0, 10
  br i1 %Pivot16, label %NodeBlock13, label %NodeBlock15.Flow77_crit_edge

NodeBlock15.Flow77_crit_edge:                     ; preds = %NodeBlock15
  br label %Flow77

Flow76:                                           ; preds = %NodeBlock17.Flow76_crit_edge, %Flow78
  %27 = phi i1 [ %51, %Flow78 ], [ %14, %NodeBlock17.Flow76_crit_edge ]
  %28 = phi i1 [ false, %Flow78 ], [ true, %NodeBlock17.Flow76_crit_edge ]
  br i1 %28, label %NodeBlock9, label %Flow76.Flow80_crit_edge

Flow76.Flow80_crit_edge:                          ; preds = %Flow76
  br label %Flow80

NodeBlock13:                                      ; preds = %NodeBlock15
  %Pivot14 = icmp sge i32 %.0, 11
  br i1 %Pivot14, label %55, label %NodeBlock13.Flow56_crit_edge

NodeBlock13.Flow56_crit_edge:                     ; preds = %NodeBlock13
  br label %Flow56

Flow77:                                           ; preds = %NodeBlock15.Flow77_crit_edge, %Flow57
  %29 = phi i1 [ false, %Flow57 ], [ true, %NodeBlock15.Flow77_crit_edge ]
  br i1 %29, label %LeafBlock11, label %Flow77.Flow78_crit_edge

Flow77.Flow78_crit_edge:                          ; preds = %Flow77
  br label %Flow78

LeafBlock11:                                      ; preds = %Flow77
  %SwitchLeaf12 = icmp eq i32 %.0, 8
  br i1 %SwitchLeaf12, label %52, label %LeafBlock11.Flow79_crit_edge

LeafBlock11.Flow79_crit_edge:                     ; preds = %LeafBlock11
  br label %Flow79

Flow79:                                           ; preds = %LeafBlock11.Flow79_crit_edge, %52
  %30 = phi i1 [ false, %52 ], [ true, %LeafBlock11.Flow79_crit_edge ]
  br label %Flow78

NodeBlock9:                                       ; preds = %Flow76
  %Pivot10 = icmp sge i32 %.0, 7
  br i1 %Pivot10, label %50, label %NodeBlock9.Flow58_crit_edge

NodeBlock9.Flow58_crit_edge:                      ; preds = %NodeBlock9
  br label %Flow58

Flow80:                                           ; preds = %Flow76.Flow80_crit_edge, %Flow59
  br label %Flow75

NodeBlock7:                                       ; preds = %Flow75
  %Pivot8 = icmp sge i32 %.0, 3
  br i1 %Pivot8, label %NodeBlock5, label %NodeBlock7.Flow82_crit_edge

NodeBlock7.Flow82_crit_edge:                      ; preds = %NodeBlock7
  br label %Flow82

Flow81:                                           ; preds = %Flow75.Flow81_crit_edge, %Flow83
  %31 = phi i1 [ %40, %Flow83 ], [ false, %Flow75.Flow81_crit_edge ]
  %32 = phi i1 [ %41, %Flow83 ], [ %25, %Flow75.Flow81_crit_edge ]
  br label %Flow74

NodeBlock5:                                       ; preds = %NodeBlock7
  %Pivot6 = icmp sge i32 %.0, 4
  br i1 %Pivot6, label %NodeBlock3, label %NodeBlock5.Flow62_crit_edge

NodeBlock5.Flow62_crit_edge:                      ; preds = %NodeBlock5
  br label %Flow62

NodeBlock3:                                       ; preds = %NodeBlock5
  %Pivot4 = icmp sge i32 %.0, 5
  br i1 %Pivot4, label %47, label %NodeBlock3.Flow60_crit_edge

NodeBlock3.Flow60_crit_edge:                      ; preds = %NodeBlock3
  br label %Flow60

Flow82:                                           ; preds = %NodeBlock7.Flow82_crit_edge, %Flow63
  %33 = phi i1 [ false, %Flow63 ], [ true, %NodeBlock7.Flow82_crit_edge ]
  br i1 %33, label %NodeBlock, label %Flow82.Flow83_crit_edge

Flow82.Flow83_crit_edge:                          ; preds = %Flow82
  br label %Flow83

NodeBlock:                                        ; preds = %Flow82
  %Pivot = icmp sge i32 %.0, 2
  br i1 %Pivot, label %42, label %NodeBlock.Flow84_crit_edge

NodeBlock.Flow84_crit_edge:                       ; preds = %NodeBlock
  br label %Flow84

Flow84:                                           ; preds = %NodeBlock.Flow84_crit_edge, %42
  %34 = phi i1 [ false, %42 ], [ true, %NodeBlock.Flow84_crit_edge ]
  br i1 %34, label %LeafBlock, label %Flow84.Flow85_crit_edge

Flow84.Flow85_crit_edge:                          ; preds = %Flow84
  br label %Flow85

LeafBlock:                                        ; preds = %Flow84
  %SwitchLeaf = icmp ne i32 %.0, 0
  br label %Flow85

Flow85:                                           ; preds = %Flow84.Flow85_crit_edge, %LeafBlock
  %35 = phi i1 [ true, %LeafBlock ], [ false, %Flow84.Flow85_crit_edge ]
  %36 = phi i1 [ %SwitchLeaf, %LeafBlock ], [ %25, %Flow84.Flow85_crit_edge ]
  br label %Flow83

37:                                               ; preds = %Flow74
  br label %Flow86

Flow86:                                           ; preds = %Flow74.Flow86_crit_edge, %37
  %38 = phi i1 [ false, %37 ], [ %23, %Flow74.Flow86_crit_edge ]
  br i1 %38, label %39, label %Flow86.spirv_fn_170.exit_crit_edge

Flow86.spirv_fn_170.exit_crit_edge:               ; preds = %Flow86
  br label %spirv_fn_170.exit

39:                                               ; preds = %Flow86
  br label %spirv_fn_170.exit

Flow83:                                           ; preds = %Flow82.Flow83_crit_edge, %Flow85
  %40 = phi i1 [ %35, %Flow85 ], [ false, %Flow82.Flow83_crit_edge ]
  %41 = phi i1 [ %36, %Flow85 ], [ %25, %Flow82.Flow83_crit_edge ]
  br label %Flow81

42:                                               ; preds = %NodeBlock
  br label %Flow84

Flow62:                                           ; preds = %NodeBlock5.Flow62_crit_edge, %Flow61
  %43 = phi i1 [ false, %Flow61 ], [ true, %NodeBlock5.Flow62_crit_edge ]
  br i1 %43, label %44, label %Flow62.Flow63_crit_edge

Flow62.Flow63_crit_edge:                          ; preds = %Flow62
  br label %Flow63

44:                                               ; preds = %Flow62
  br label %Flow63

Flow60:                                           ; preds = %NodeBlock3.Flow60_crit_edge, %47
  %45 = phi i1 [ false, %47 ], [ true, %NodeBlock3.Flow60_crit_edge ]
  br i1 %45, label %46, label %Flow60.Flow61_crit_edge

Flow60.Flow61_crit_edge:                          ; preds = %Flow60
  br label %Flow61

46:                                               ; preds = %Flow60
  br label %Flow61

47:                                               ; preds = %NodeBlock3
  br label %Flow60

Flow58:                                           ; preds = %NodeBlock9.Flow58_crit_edge, %50
  %48 = phi i1 [ false, %50 ], [ true, %NodeBlock9.Flow58_crit_edge ]
  br i1 %48, label %49, label %Flow58.Flow59_crit_edge

Flow58.Flow59_crit_edge:                          ; preds = %Flow58
  br label %Flow59

49:                                               ; preds = %Flow58
  br label %Flow59

50:                                               ; preds = %NodeBlock9
  br label %Flow58

Flow78:                                           ; preds = %Flow77.Flow78_crit_edge, %Flow79
  %51 = phi i1 [ %30, %Flow79 ], [ %14, %Flow77.Flow78_crit_edge ]
  br label %Flow76

52:                                               ; preds = %LeafBlock11
  br label %Flow79

Flow56:                                           ; preds = %NodeBlock13.Flow56_crit_edge, %55
  %53 = phi i1 [ false, %55 ], [ true, %NodeBlock13.Flow56_crit_edge ]
  br i1 %53, label %54, label %Flow56.Flow57_crit_edge

Flow56.Flow57_crit_edge:                          ; preds = %Flow56
  br label %Flow57

54:                                               ; preds = %Flow56
  br label %Flow57

55:                                               ; preds = %NodeBlock13
  br label %Flow56

Flow54:                                           ; preds = %NodeBlock21.Flow54_crit_edge, %58
  %56 = phi i1 [ false, %58 ], [ true, %NodeBlock21.Flow54_crit_edge ]
  br i1 %56, label %57, label %Flow54.Flow55_crit_edge

Flow54.Flow55_crit_edge:                          ; preds = %Flow54
  br label %Flow55

57:                                               ; preds = %Flow54
  br label %Flow55

58:                                               ; preds = %NodeBlock21
  br label %Flow54

Flow71:                                           ; preds = %Flow69.Flow71_crit_edge, %61
  br label %Flow68

59:                                               ; preds = %Flow68
  br label %Flow72

Flow70:                                           ; preds = %LeafBlock23.Flow70_crit_edge, %64
  %60 = phi i1 [ false, %64 ], [ true, %LeafBlock23.Flow70_crit_edge ]
  br label %Flow69

61:                                               ; preds = %Flow69
  br label %Flow71

Flow69:                                           ; preds = %NodeBlock25.Flow69_crit_edge, %Flow70
  %62 = phi i1 [ %60, %Flow70 ], [ false, %NodeBlock25.Flow69_crit_edge ]
  %63 = phi i1 [ false, %Flow70 ], [ true, %NodeBlock25.Flow69_crit_edge ]
  br i1 %63, label %61, label %Flow69.Flow71_crit_edge

Flow69.Flow71_crit_edge:                          ; preds = %Flow69
  br label %Flow71

64:                                               ; preds = %LeafBlock23
  br label %Flow70

Flow50:                                           ; preds = %NodeBlock33.Flow50_crit_edge, %Flow49
  %65 = phi i1 [ false, %Flow49 ], [ true, %NodeBlock33.Flow50_crit_edge ]
  br i1 %65, label %66, label %Flow50.Flow51_crit_edge

Flow50.Flow51_crit_edge:                          ; preds = %Flow50
  br label %Flow51

66:                                               ; preds = %Flow50
  br label %Flow51

Flow48:                                           ; preds = %NodeBlock31.Flow48_crit_edge, %69
  %67 = phi i1 [ false, %69 ], [ true, %NodeBlock31.Flow48_crit_edge ]
  br i1 %67, label %68, label %Flow48.Flow49_crit_edge

Flow48.Flow49_crit_edge:                          ; preds = %Flow48
  br label %Flow49

68:                                               ; preds = %Flow48
  br label %Flow49

69:                                               ; preds = %NodeBlock31
  br label %Flow48

Flow46:                                           ; preds = %NodeBlock37.Flow46_crit_edge, %Flow45
  %70 = phi i1 [ false, %Flow45 ], [ true, %NodeBlock37.Flow46_crit_edge ]
  br i1 %70, label %71, label %Flow46.Flow47_crit_edge

Flow46.Flow47_crit_edge:                          ; preds = %Flow46
  br label %Flow47

71:                                               ; preds = %Flow46
  br label %Flow47

Flow:                                             ; preds = %NodeBlock35.Flow_crit_edge, %74
  %72 = phi i1 [ false, %74 ], [ true, %NodeBlock35.Flow_crit_edge ]
  br i1 %72, label %73, label %Flow.Flow45_crit_edge

Flow.Flow45_crit_edge:                            ; preds = %Flow
  br label %Flow45

73:                                               ; preds = %Flow
  br label %Flow45

74:                                               ; preds = %NodeBlock35
  br label %Flow

Flow45:                                           ; preds = %Flow.Flow45_crit_edge, %73
  br label %Flow46

Flow47:                                           ; preds = %Flow46.Flow47_crit_edge, %71
  br label %Flow52

Flow49:                                           ; preds = %Flow48.Flow49_crit_edge, %68
  br label %Flow50

Flow51:                                           ; preds = %Flow50.Flow51_crit_edge, %66
  br label %Flow53

Flow53:                                           ; preds = %Flow52.Flow53_crit_edge, %Flow51
  br label %Flow65

Flow55:                                           ; preds = %Flow54.Flow55_crit_edge, %57
  br label %Flow73

Flow57:                                           ; preds = %Flow56.Flow57_crit_edge, %54
  br label %Flow77

Flow59:                                           ; preds = %Flow58.Flow59_crit_edge, %49
  br label %Flow80

Flow61:                                           ; preds = %Flow60.Flow61_crit_edge, %46
  br label %Flow62

Flow63:                                           ; preds = %Flow62.Flow63_crit_edge, %44
  br label %Flow82

spirv_fn_170.exit:                                ; preds = %Flow86.spirv_fn_170.exit_crit_edge, %39
  %75 = add i32 %.0, 1
  br label %Flow87

spirv_fn_136.exit:                                ; preds = %Flow88
  %76 = icmp eq i32 %1, 0
  br i1 %76, label %78, label %spirv_fn_136.exit._crit_edge

spirv_fn_136.exit._crit_edge:                     ; preds = %spirv_fn_136.exit
  br label %77

77:                                               ; preds = %spirv_fn_136.exit._crit_edge, %78
  ret void

78:                                               ; preds = %spirv_fn_136.exit
  call void @feme.stage.output.store.f32(i32 2, i32 0, i32 0, float 1.000000e+00, i32 0)
  call void @feme.stage.output.store.f32(i32 2, i32 1, i32 0, float 1.000000e+00, i32 0)
  call void @feme.stage.output.store.f32(i32 2, i32 2, i32 0, float 1.000000e+00, i32 0)
  call void @feme.stage.output.store.f32(i32 3, i32 0, i32 0, float 1.000000e+00, i32 0)
  br label %77
}

declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)

declare float @feme.stage.input.load.f32(i32, i32, i32, i32)

declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)

attributes #3 = { "feme.cpu.wavesize"="4" "feme.entry.name"="main.patchconstant" "feme.shader.stage"="hull" "feme.tessellation.output_control_points"="3" "hlsl.shader"="hull" }

