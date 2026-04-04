; ModuleID = '/Users/alankar/Documents/opensource/portfolio-dev/pyxc-llvm-tutorial/code/bench/fib.cpp'
source_filename = "/Users/alankar/Documents/opensource/portfolio-dev/pyxc-llvm-tutorial/code/bench/fib.cpp"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "arm64-apple-macosx26.0.0"

; Function Attrs: mustprogress nofree norecurse nounwind ssp memory(inaccessiblemem: readwrite) uwtable(sync)
define noundef i32 @main() local_unnamed_addr #0 {
  %1 = alloca i64, align 8
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %1)
  %2 = tail call fastcc noundef i64 @_ZL3fibx(i64 noundef 20)
  store volatile i64 %2, ptr %1, align 8, !tbaa !5
  %3 = load volatile i64, ptr %1, align 8, !tbaa !5
  %4 = trunc i64 %3 to i32
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %1)
  ret i32 %4
}

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(i64 immarg, ptr captures(none)) #1

; Function Attrs: mustprogress nofree nosync nounwind ssp willreturn memory(none) uwtable(sync)
define internal fastcc noundef i64 @_ZL3fibx(i64 noundef %0) unnamed_addr #2 {
  %2 = icmp slt i64 %0, 2
  br i1 %2, label %11, label %3

3:                                                ; preds = %1, %3
  %4 = phi i64 [ %8, %3 ], [ %0, %1 ]
  %5 = phi i64 [ %9, %3 ], [ 0, %1 ]
  %6 = add nsw i64 %4, -1
  %7 = tail call fastcc noundef i64 @_ZL3fibx(i64 noundef %6)
  %8 = add nsw i64 %4, -2
  %9 = add nsw i64 %7, %5
  %10 = icmp samesign ult i64 %4, 4
  br i1 %10, label %11, label %3

11:                                               ; preds = %3, %1
  %12 = phi i64 [ 0, %1 ], [ %9, %3 ]
  %13 = phi i64 [ %0, %1 ], [ %8, %3 ]
  %14 = add nsw i64 %13, %12
  ret i64 %14
}

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(i64 immarg, ptr captures(none)) #1

attributes #0 = { mustprogress nofree norecurse nounwind ssp memory(inaccessiblemem: readwrite) uwtable(sync) "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #1 = { mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #2 = { mustprogress nofree nosync nounwind ssp willreturn memory(none) uwtable(sync) "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }

!llvm.module.flags = !{!0, !1, !2, !3}
!llvm.ident = !{!4}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"uwtable", i32 1}
!3 = !{i32 7, !"frame-pointer", i32 1}
!4 = !{!"clang version 21.1.6 (https://github.com/llvm/llvm-project.git a832a5222e489298337fbb5876f8dcaf072c5cca)"}
!5 = !{!6, !6, i64 0}
!6 = !{!"long long", !7, i64 0}
!7 = !{!"omnipotent char", !8, i64 0}
!8 = !{!"Simple C++ TBAA"}
