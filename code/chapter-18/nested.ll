; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

%struct.Outer = type { %struct.Inner }
%struct.Inner = type { i64 }

declare void @printd(double)

define i64 @__pyxc.user_main() {
entry:
  %o = alloca %struct.Outer, align 8
  store %struct.Outer zeroinitializer, ptr %o, align 8
  %fieldptr = getelementptr inbounds nuw %struct.Outer, ptr %o, i32 0, i32 0
  %fieldptr1 = getelementptr inbounds nuw %struct.Inner, ptr %fieldptr, i32 0, i32 0
  store i64 9, ptr %fieldptr1, align 8
  %fieldptr2 = getelementptr inbounds nuw %struct.Outer, ptr %o, i32 0, i32 0
  %fieldptr3 = getelementptr inbounds nuw %struct.Inner, ptr %fieldptr2, i32 0, i32 0
  %fieldload = load i64, ptr %fieldptr3, align 8
  %sitofp = sitofp i64 %fieldload to double
  call void @printd(double %sitofp)
  ret i64 0
}

define i32 @main() {
entry:
  %0 = call i64 @__pyxc.user_main()
  %1 = trunc i64 %0 to i32
  ret i32 %1
}
