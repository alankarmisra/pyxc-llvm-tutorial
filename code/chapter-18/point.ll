; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

%struct.Point = type { i64, i64 }

declare void @printd(double)

define i64 @__pyxc.user_main() {
entry:
  %p = alloca %struct.Point, align 8
  store %struct.Point zeroinitializer, ptr %p, align 8
  %fieldptr = getelementptr inbounds nuw %struct.Point, ptr %p, i32 0, i32 0
  store i64 3, ptr %fieldptr, align 8
  %fieldptr1 = getelementptr inbounds nuw %struct.Point, ptr %p, i32 0, i32 1
  store i64 4, ptr %fieldptr1, align 8
  %fieldptr2 = getelementptr inbounds nuw %struct.Point, ptr %p, i32 0, i32 0
  %fieldload = load i64, ptr %fieldptr2, align 8
  %fieldptr3 = getelementptr inbounds nuw %struct.Point, ptr %p, i32 0, i32 1
  %fieldload4 = load i64, ptr %fieldptr3, align 8
  %addtmp = add i64 %fieldload, %fieldload4
  %sitofp = sitofp i64 %addtmp to double
  call void @printd(double %sitofp)
  ret i64 0
}

define i32 @main() {
entry:
  %0 = call i64 @__pyxc.user_main()
  %1 = trunc i64 %0 to i32
  ret i32 %1
}
