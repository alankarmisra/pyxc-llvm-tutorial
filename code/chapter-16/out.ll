; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

define double @sq(double %x) !dbg !4 {
entry:
  %x1 = alloca double, align 8
  store double %x, ptr %x1, align 8, !dbg !10
    #dbg_declare(ptr %x1, !9, !DIExpression(), !10)
  %x2 = load double, ptr %x1, align 8, !dbg !10
  %x3 = load double, ptr %x1, align 8, !dbg !10
  %multmp = fmul double %x2, %x3, !dbg !10
  ret double %multmp, !dbg !10
}

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C, file: !1, producer: "pyxc", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug)
!1 = !DIFile(filename: "sq.pyxc", directory: ".")
!2 = !{i32 2, !"Dwarf Version", i32 4}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = distinct !DISubprogram(name: "sq", scope: !1, file: !1, line: 1, type: !5, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !0, retainedNodes: !8)
!5 = !DISubroutineType(types: !6)
!6 = !{!7, !7}
!7 = !DIBasicType(name: "double", size: 64, encoding: DW_ATE_float)
!8 = !{!9}
!9 = !DILocalVariable(name: "x", arg: 1, scope: !4, file: !1, line: 1, type: !7)
!10 = !DILocation(line: 1, column: 1, scope: !4)
