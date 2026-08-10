; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

define void @f() {
entry:
  %p = alloca ptr, align 8
  %x = alloca i64, align 8
  store i64 42, ptr %x, align 8
  store ptr %x, ptr %p, align 8
  ret void
}
