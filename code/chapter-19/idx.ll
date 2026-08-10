; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

define void @f(ptr %p) {
entry:
  %b = alloca i64, align 8
  %a = alloca i64, align 8
  %p1 = alloca ptr, align 8
  store ptr %p, ptr %p1, align 8
  %ptrload = load ptr, ptr %p1, align 8
  %elemptr = getelementptr inbounds i64, ptr %ptrload, i64 0
  %elemload = load i64, ptr %elemptr, align 8
  store i64 %elemload, ptr %a, align 8
  %ptrload2 = load ptr, ptr %p1, align 8
  %elemptr3 = getelementptr inbounds i64, ptr %ptrload2, i64 1
  %elemload4 = load i64, ptr %elemptr3, align 8
  store i64 %elemload4, ptr %b, align 8
  ret void
}
