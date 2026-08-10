; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define double @sq(double %x) local_unnamed_addr #0 !dbg !4 {
entry:
    #dbg_value(double %x, !9, !DIExpression(), !10)
  %multmp = fmul double %x, %x, !dbg !11
  ret double %multmp, !dbg !11
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C, file: !1, producer: "pyxc", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug)
!1 = !DIFile(filename: "sq.pyxc", directory: ".")
!2 = !{i32 2, !"Dwarf Version", i32 4}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = distinct !DISubprogram(name: "sq", scope: !1, file: !1, line: 1, type: !5, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !8)
!5 = !DISubroutineType(types: !6)
!6 = !{!7, !7}
!7 = !DIBasicType(name: "double", size: 64, encoding: DW_ATE_float)
!8 = !{!9}
!9 = !DILocalVariable(name: "x", arg: 1, scope: !4, file: !1, line: 1, type: !7)
!10 = !DILocation(line: 0, scope: !4)
!11 = !DILocation(line: 1, column: 1, scope: !4)
