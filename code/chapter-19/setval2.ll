; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

define void @set_value(ptr %p, i64 %v) {
entry:
  %v2 = alloca i64, align 8
  %p1 = alloca ptr, align 8
  store ptr %p, ptr %p1, align 8
  store i64 %v, ptr %v2, align 8
  %ptrload = load ptr, ptr %p1, align 8
  %elemptr = getelementptr inbounds i64, ptr %ptrload, i64 0
  %v3 = load i64, ptr %v2, align 8
  store i64 %v3, ptr %elemptr, align 8
  ret void
}

define i64 @__pyxc.user_main() {
entry:
  %x = alloca i64, align 8
  store i64 5, ptr %x, align 8
  call void @set_value(ptr %x, i64 100)
  ret i64 0
}

define i32 @main() {
entry:
  %0 = call i64 @__pyxc.user_main()
  %1 = trunc i64 %0 to i32
  ret i32 %1
}
