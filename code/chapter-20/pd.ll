; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

define void @f(ptr %p, ptr %q) {
entry:
  %d = alloca i64, align 8
  %q2 = alloca ptr, align 8
  %p1 = alloca ptr, align 8
  store ptr %p, ptr %p1, align 8
  store ptr %q, ptr %q2, align 8
  %q3 = load ptr, ptr %q2, align 8
  %p4 = load ptr, ptr %p1, align 8
  %0 = ptrtoint ptr %q3 to i64
  %1 = ptrtoint ptr %p4 to i64
  %2 = sub i64 %0, %1
  %ptrdiff = sdiv exact i64 %2, ptrtoint (ptr getelementptr (i64, ptr null, i32 1) to i64)
  store i64 %ptrdiff, ptr %d, align 8
  ret void
}
