; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

%struct.Point = type { i64, i64 }

define void @f(ptr %p, i64 %v) {
entry:
  %r = alloca i64, align 8
  %v2 = alloca i64, align 8
  %p1 = alloca ptr, align 8
  store ptr %p, ptr %p1, align 8
  store i64 %v, ptr %v2, align 8
  %ptrload = load ptr, ptr %p1, align 8
  %elemptr = getelementptr inbounds %struct.Point, ptr %ptrload, i64 0
  %fieldptr = getelementptr inbounds nuw %struct.Point, ptr %elemptr, i32 0, i32 0
  %fieldload = load i64, ptr %fieldptr, align 8
  store i64 %fieldload, ptr %r, align 8
  %ptrload3 = load ptr, ptr %p1, align 8
  %elemptr4 = getelementptr inbounds %struct.Point, ptr %ptrload3, i64 0
  %fieldptr5 = getelementptr inbounds nuw %struct.Point, ptr %elemptr4, i32 0, i32 0
  %v6 = load i64, ptr %v2, align 8
  store i64 %v6, ptr %fieldptr5, align 8
  ret void
}
