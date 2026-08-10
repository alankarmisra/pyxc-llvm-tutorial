; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

define void @f([4 x i64] %a) {
entry:
  %x = alloca i64, align 8
  %a1 = alloca [4 x i64], align 8
  store [4 x i64] %a, ptr %a1, align 8
  %arraydecay = getelementptr inbounds [4 x i64], ptr %a1, i64 0, i64 0
  %elemptr = getelementptr inbounds i64, ptr %arraydecay, i64 0
  %elemload = load i64, ptr %elemptr, align 8
  store i64 %elemload, ptr %x, align 8
  ret void
}
