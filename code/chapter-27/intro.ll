; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

%struct.Point = type { i64, i64 }

declare void @printd(double)

define void @Point.__init__(ptr %self, i64 %px, i64 %py) {
entry:
  %py3 = alloca i64, align 8
  %px2 = alloca i64, align 8
  %self1 = alloca ptr, align 8
  store ptr %self, ptr %self1, align 8
  store i64 %px, ptr %px2, align 8
  store i64 %py, ptr %py3, align 8
  %self.ptr = load ptr, ptr %self1, align 8
  %fieldptr = getelementptr inbounds nuw %struct.Point, ptr %self.ptr, i32 0, i32 0
  %px4 = load i64, ptr %px2, align 8
  store i64 %px4, ptr %fieldptr, align 8
  %self.ptr5 = load ptr, ptr %self1, align 8
  %fieldptr6 = getelementptr inbounds nuw %struct.Point, ptr %self.ptr5, i32 0, i32 1
  %py7 = load i64, ptr %py3, align 8
  store i64 %py7, ptr %fieldptr6, align 8
  ret void
}

define i64 @Point.sum(ptr %self) {
entry:
  %self1 = alloca ptr, align 8
  store ptr %self, ptr %self1, align 8
  %self.ptr = load ptr, ptr %self1, align 8
  %fieldptr = getelementptr inbounds nuw %struct.Point, ptr %self.ptr, i32 0, i32 0
  %fieldload = load i64, ptr %fieldptr, align 8
  %self.ptr2 = load ptr, ptr %self1, align 8
  %fieldptr3 = getelementptr inbounds nuw %struct.Point, ptr %self.ptr2, i32 0, i32 1
  %fieldload4 = load i64, ptr %fieldptr3, align 8
  %addtmp = add i64 %fieldload, %fieldload4
  ret i64 %addtmp
}

define i64 @__pyxc.user_main() {
entry:
  %p = alloca %struct.Point, align 8
  %ctor.tmp = alloca %struct.Point, align 8
  store %struct.Point zeroinitializer, ptr %ctor.tmp, align 8
  call void @Point.__init__(ptr %ctor.tmp, i64 3, i64 4)
  %ctor.obj = load %struct.Point, ptr %ctor.tmp, align 8
  store %struct.Point %ctor.obj, ptr %p, align 8
  %calltmp = call i64 @Point.sum(ptr %p)
  %sitofp = sitofp i64 %calltmp to double
  call void @printd(double %sitofp)
  ret i64 0
}

define i32 @main() {
entry:
  %0 = call i64 @__pyxc.user_main()
  %1 = trunc i64 %0 to i32
  ret i32 %1
}
