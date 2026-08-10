; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

declare ptr @malloc(i64)

define void @f() {
entry:
  %p = alloca ptr, align 8
  %raw = alloca ptr, align 8
  %calltmp = call ptr @malloc(i64 8)
  store ptr %calltmp, ptr %raw, align 8
  %raw1 = load ptr, ptr %raw, align 8
  store ptr %raw1, ptr %p, align 8
  ret void
}
