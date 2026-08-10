; ModuleID = 'PyxcJIT'
source_filename = "PyxcJIT"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"

define i64 @sum4([4 x i64] %a) {
entry:
  %a1 = alloca [4 x i64], align 8
  store [4 x i64] %a, ptr %a1, align 8
  %arraydecay = getelementptr inbounds [4 x i64], ptr %a1, i64 0, i64 0
  %elemptr = getelementptr inbounds i64, ptr %arraydecay, i64 0
  %elemload = load i64, ptr %elemptr, align 8
  %arraydecay2 = getelementptr inbounds [4 x i64], ptr %a1, i64 0, i64 0
  %elemptr3 = getelementptr inbounds i64, ptr %arraydecay2, i64 1
  %elemload4 = load i64, ptr %elemptr3, align 8
  %addtmp = add i64 %elemload, %elemload4
  %arraydecay5 = getelementptr inbounds [4 x i64], ptr %a1, i64 0, i64 0
  %elemptr6 = getelementptr inbounds i64, ptr %arraydecay5, i64 2
  %elemload7 = load i64, ptr %elemptr6, align 8
  %addtmp8 = add i64 %addtmp, %elemload7
  %arraydecay9 = getelementptr inbounds [4 x i64], ptr %a1, i64 0, i64 0
  %elemptr10 = getelementptr inbounds i64, ptr %arraydecay9, i64 3
  %elemload11 = load i64, ptr %elemptr10, align 8
  %addtmp12 = add i64 %addtmp8, %elemload11
  ret i64 %addtmp12
}
