; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

%struct.Point = type { i64, i64 }

define void @f() {
entry:
  %px = alloca ptr, align 8
  %pt = alloca %struct.Point, align 8
  store %struct.Point zeroinitializer, ptr %pt, align 8
  %fieldptr = getelementptr inbounds nuw %struct.Point, ptr %pt, i32 0, i32 0
  store ptr %fieldptr, ptr %px, align 8
  ret void
}
