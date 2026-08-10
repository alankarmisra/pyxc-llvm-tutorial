; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

define void @f(ptr %p) {
entry:
  %q = alloca ptr, align 8
  %p1 = alloca ptr, align 8
  store ptr %p, ptr %p1, align 8
  %p2 = load ptr, ptr %p1, align 8
  %ptrarith = getelementptr inbounds i64, ptr %p2, i64 1
  store ptr %ptrarith, ptr %q, align 8
  ret void
}
