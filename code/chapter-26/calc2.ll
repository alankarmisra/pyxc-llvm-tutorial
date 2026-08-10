; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

%struct.Calc = type { i64 }

define i64 @Calc.add(ptr %self, i64 %x, i64 %y) {
entry:
  %y3 = alloca i64, align 8
  %x2 = alloca i64, align 8
  %self1 = alloca ptr, align 8
  store ptr %self, ptr %self1, align 8
  store i64 %x, ptr %x2, align 8
  store i64 %y, ptr %y3, align 8
  %x4 = load i64, ptr %x2, align 8
  %y5 = load i64, ptr %y3, align 8
  %addtmp = add i64 %x4, %y5
  ret i64 %addtmp
}

define i64 @__pyxc.user_main() {
entry:
  %c = alloca %struct.Calc, align 8
  store %struct.Calc zeroinitializer, ptr %c, align 8
  %calltmp = call i64 @Calc.add(ptr %c, i64 3, i64 4)
  ret i64 %calltmp
}

define i32 @main() {
entry:
  %0 = call i64 @__pyxc.user_main()
  %1 = trunc i64 %0 to i32
  ret i32 %1
}
