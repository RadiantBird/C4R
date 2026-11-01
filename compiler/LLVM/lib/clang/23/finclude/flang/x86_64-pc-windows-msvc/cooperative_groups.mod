!mod$ v1 sum:3cb878ff8395bffb
!need$ bff84f81d3c1e37c n cudadevice
!need$ d676699be0556005 n __fortran_builtins
!need$ bff84f81d3c1e37c i cudadevice
module cooperative_groups
use __fortran_builtins,only:c_devptr=>__builtin_c_devptr
use cudadevice,only:dim3
use cudadevice,only:c_devloc
use cudadevice,only:threadidx
use cudadevice,only:blockdim
use cudadevice,only:blockidx
use cudadevice,only:griddim
use cudadevice,only:warpsize
use cudadevice,only:syncthreads_and_i4
use cudadevice,only:syncthreads_and_l4
use cudadevice,only:syncthreads_count_i4
use cudadevice,only:syncthreads_count_l4
use cudadevice,only:syncthreads_or_i4
use cudadevice,only:syncthreads_or_l4
use cudadevice,only:syncwarp
use cudadevice,only:threadfence
use cudadevice,only:threadfence_block
use cudadevice,only:threadfence_system
use cudadevice,only:__fadd_rd
use cudadevice,only:__fadd_ru
use cudadevice,only:rsqrtf
use cudadevice,only:__saturatef
use cudadevice,only:signbitf
use cudadevice,only:sincosf
use cudadevice,only:sincospif
use cudadevice,only:__cosf
use cudadevice,only:__powf
use cudadevice,only:cospif
use cudadevice,only:sinpif
use cudadevice,only:__mulhi
use cudadevice,only:__umulhi
use cudadevice,only:__mul64hi
use cudadevice,only:__umul64hi
use cudadevice,only:__int_as_float
use cudadevice,only:__float_as_int
use cudadevice,only:__double_as_longlong
use cudadevice,only:__longlong_as_double
use cudadevice,only:__clzll
use cudadevice,only:__ffsll
use cudadevice,only:__popcll
use cudadevice,only:__brevll
use cudadevice,only:atomicaddi
use cudadevice,only:atomicaddf
use cudadevice,only:atomicaddd
use cudadevice,only:atomicaddl
use cudadevice,only:atomicaddr2
use cudadevice,only:atomicaddvector_r2x2
use cudadevice,only:atomicaddvector_r4x2
use cudadevice,only:atomicadd_r4x2
use cudadevice,only:atomicadd_r4x4
use cudadevice,only:atomicsubi
use cudadevice,only:atomicsubf
use cudadevice,only:atomicsubd
use cudadevice,only:atomicsubl
use cudadevice,only:atomicmaxi
use cudadevice,only:atomicmaxf
use cudadevice,only:atomicmaxd
use cudadevice,only:atomicmaxl
use cudadevice,only:atomicmini
use cudadevice,only:atomicminf
use cudadevice,only:atomicmind
use cudadevice,only:atomicminl
use cudadevice,only:atomicandi
use cudadevice,only:atomicori
use cudadevice,only:atomicinci
use cudadevice,only:atomicdeci
use cudadevice,only:atomiccasi
use cudadevice,only:atomiccasul
use cudadevice,only:atomiccasf
use cudadevice,only:atomiccasd
use cudadevice,only:atomicexchi
use cudadevice,only:atomicexchul
use cudadevice,only:atomicexchf
use cudadevice,only:atomicexchd
use cudadevice,only:atomicxori
use cudadevice,only:clock
use cudadevice,only:clock64
use cudadevice,only:globaltimer
use cudadevice,only:match_all_syncjj
use cudadevice,only:match_all_syncjx
use cudadevice,only:match_all_syncjf
use cudadevice,only:match_all_syncjd
use cudadevice,only:match_any_syncjj
use cudadevice,only:match_any_syncjx
use cudadevice,only:match_any_syncjf
use cudadevice,only:match_any_syncjd
use cudadevice,only:__ldcg_i4
use cudadevice,only:__ldcg_i8
use cudadevice,only:__ldcg_cd
use cudadevice,only:__ldcg_r2
use cudadevice,only:__ldcg_r4
use cudadevice,only:__ldcg_r8
use cudadevice,only:__ldcg_c4
use cudadevice,only:__ldcg_c8
use cudadevice,only:__ldcg_i4x4
use cudadevice,only:__ldcg_i8x2
use cudadevice,only:__ldcg_r2x2
use cudadevice,only:__ldcg_r4x4
use cudadevice,only:__ldcg_r8x2
use cudadevice,only:__ldca_i4
use cudadevice,only:__ldca_i8
use cudadevice,only:__ldca_cd
use cudadevice,only:__ldca_r2
use cudadevice,only:__ldca_r4
use cudadevice,only:__ldca_r8
use cudadevice,only:__ldca_c4
use cudadevice,only:__ldca_c8
use cudadevice,only:__ldca_i4x4
use cudadevice,only:__ldca_i8x2
use cudadevice,only:__ldca_r2x2
use cudadevice,only:__ldca_r4x4
use cudadevice,only:__ldca_r8x2
use cudadevice,only:__ldcs_i4
use cudadevice,only:__ldcs_i8
use cudadevice,only:__ldcs_cd
use cudadevice,only:__ldcs_r2
use cudadevice,only:__ldcs_r4
use cudadevice,only:__ldcs_r8
use cudadevice,only:__ldcs_c4
use cudadevice,only:__ldcs_c8
use cudadevice,only:__ldcs_i4x4
use cudadevice,only:__ldcs_i8x2
use cudadevice,only:__ldcs_r2x2
use cudadevice,only:__ldcs_r4x4
use cudadevice,only:__ldcs_r8x2
use cudadevice,only:__ldlu_i4
use cudadevice,only:__ldlu_i8
use cudadevice,only:__ldlu_cd
use cudadevice,only:__ldlu_r2
use cudadevice,only:__ldlu_r4
use cudadevice,only:__ldlu_r8
use cudadevice,only:__ldlu_c4
use cudadevice,only:__ldlu_c8
use cudadevice,only:__ldlu_i4x4
use cudadevice,only:__ldlu_i8x2
use cudadevice,only:__ldlu_r2x2
use cudadevice,only:__ldlu_r4x4
use cudadevice,only:__ldlu_r8x2
use cudadevice,only:__ldcv_i4
use cudadevice,only:__ldcv_i8
use cudadevice,only:__ldcv_cd
use cudadevice,only:__ldcv_r2
use cudadevice,only:__ldcv_r4
use cudadevice,only:__ldcv_r8
use cudadevice,only:__ldcv_c4
use cudadevice,only:__ldcv_c8
use cudadevice,only:__ldcv_i4x4
use cudadevice,only:__ldcv_i8x2
use cudadevice,only:__ldcv_r2x2
use cudadevice,only:__ldcv_r4x4
use cudadevice,only:__ldcv_r8x2
use cudadevice,only:__stwb_i4
use cudadevice,only:__stwb_i8
use cudadevice,only:__stwb_cd
use cudadevice,only:__stwb_r2
use cudadevice,only:__stwb_r4
use cudadevice,only:__stwb_r8
use cudadevice,only:__stwb_c4
use cudadevice,only:__stwb_c8
use cudadevice,only:__stwb_i4x4
use cudadevice,only:__stwb_i8x2
use cudadevice,only:__stwb_r2x2
use cudadevice,only:__stwb_r4x4
use cudadevice,only:__stwb_r8x2
use cudadevice,only:__stcg_i4
use cudadevice,only:__stcg_i8
use cudadevice,only:__stcg_cd
use cudadevice,only:__stcg_r2
use cudadevice,only:__stcg_r4
use cudadevice,only:__stcg_r8
use cudadevice,only:__stcg_c4
use cudadevice,only:__stcg_c8
use cudadevice,only:__stcg_i4x4
use cudadevice,only:__stcg_i8x2
use cudadevice,only:__stcg_r2x2
use cudadevice,only:__stcg_r4x4
use cudadevice,only:__stcg_r8x2
use cudadevice,only:__stcs_i4
use cudadevice,only:__stcs_i8
use cudadevice,only:__stcs_cd
use cudadevice,only:__stcs_r2
use cudadevice,only:__stcs_r4
use cudadevice,only:__stcs_r8
use cudadevice,only:__stcs_c4
use cudadevice,only:__stcs_c8
use cudadevice,only:__stcs_i4x4
use cudadevice,only:__stcs_i8x2
use cudadevice,only:__stcs_r2x2
use cudadevice,only:__stcs_r4x4
use cudadevice,only:__stcs_r8x2
use cudadevice,only:__stwt_i4
use cudadevice,only:__stwt_i8
use cudadevice,only:__stwt_cd
use cudadevice,only:__stwt_r2
use cudadevice,only:__stwt_r4
use cudadevice,only:__stwt_r8
use cudadevice,only:__stwt_c4
use cudadevice,only:__stwt_c8
use cudadevice,only:__stwt_i4x4
use cudadevice,only:__stwt_i8x2
use cudadevice,only:__stwt_r2x2
use cudadevice,only:__stwt_r4x4
use cudadevice,only:__stwt_r8x2
use cudadevice,only:on_device
use cudadevice,only:barrier_arrive_cnt
use cudadevice,only:barrier_init
use cudadevice,only:barrier_try_wait
use cudadevice,only:barrier_try_wait_sleep
use cudadevice,only:fence_proxy_async
use cudadevice,only:tma_bulk_commit_group
use cudadevice,only:tma_bulk_wait_group
use cudadevice,only:tma_bulk_g2s
use cudadevice,only:tma_bulk_ldc4
use cudadevice,only:tma_bulk_ldc8
use cudadevice,only:tma_bulk_ldi4
use cudadevice,only:tma_bulk_ldi8
use cudadevice,only:tma_bulk_ldr2
use cudadevice,only:tma_bulk_ldr4
use cudadevice,only:tma_bulk_ldr8
use cudadevice,only:tma_bulk_s2g
use cudadevice,only:tma_bulk_store_c4
use cudadevice,only:tma_bulk_store_c8
use cudadevice,only:tma_bulk_store_i4
use cudadevice,only:tma_bulk_store_i8
use cudadevice,only:tma_bulk_store_r2
use cudadevice,only:tma_bulk_store_r4
use cudadevice,only:tma_bulk_store_r8
use cudadevice,only:syncthreads
use cudadevice,only:syncthreads_and
use cudadevice,only:syncthreads_count
use cudadevice,only:syncthreads_or
use cudadevice,only:__fadd_rn
use cudadevice,only:__fadd_rz
use cudadevice,only:__fmul_rn
use cudadevice,only:__fmul_rz
use cudadevice,only:__fmul_ru
use cudadevice,only:__fmul_rd
use cudadevice,only:__fmaf_rn
use cudadevice,only:__fmaf_rz
use cudadevice,only:__fmaf_ru
use cudadevice,only:__fmaf_rd
use cudadevice,only:__frcp_rn
use cudadevice,only:__frcp_rz
use cudadevice,only:__frcp_ru
use cudadevice,only:__frcp_rd
use cudadevice,only:__fsqrt_rn
use cudadevice,only:__fsqrt_rz
use cudadevice,only:__fsqrt_ru
use cudadevice,only:__fsqrt_rd
use cudadevice,only:__fdiv_rn
use cudadevice,only:__fdiv_rz
use cudadevice,only:__fdiv_ru
use cudadevice,only:__fdiv_rd
use cudadevice,only:__dadd_rn
use cudadevice,only:__dadd_rz
use cudadevice,only:__dadd_ru
use cudadevice,only:__dadd_rd
use cudadevice,only:__dmul_rn
use cudadevice,only:__dmul_rz
use cudadevice,only:__dmul_ru
use cudadevice,only:__dmul_rd
use cudadevice,only:__fma_rn
use cudadevice,only:__fma_rz
use cudadevice,only:__fma_ru
use cudadevice,only:__fma_rd
use cudadevice,only:rsqrt
use cudadevice,only:saturate
use cudadevice,only:__sad
use cudadevice,only:__usad
use cudadevice,only:signbit
use cudadevice,only:sincos
use cudadevice,only:sincospi
use cudadevice,only:__exp10f
use cudadevice,only:__expf
use cudadevice,only:__fdividef
use cudadevice,only:__log10f
use cudadevice,only:__log2f
use cudadevice,only:__logf
use cudadevice,only:__sincosf
use cudadevice,only:__sinf
use cudadevice,only:__tanf
use cudadevice,only:cospi
use cudadevice,only:sinpi
use cudadevice,only:mulhi
use cudadevice,only:umulhi
use cudadevice,only:mul64hi
use cudadevice,only:umul64hi
use cudadevice,only:int_as_float
use cudadevice,only:float_as_int
use cudadevice,only:__float2half_rn
use cudadevice,only:__float2int_rd
use cudadevice,only:__float2int_rn
use cudadevice,only:__float2int_ru
use cudadevice,only:__float2int_rz
use cudadevice,only:__float2uint_rd
use cudadevice,only:__float2uint_rn
use cudadevice,only:__float2uint_ru
use cudadevice,only:__float2uint_rz
use cudadevice,only:__float2ll_rd
use cudadevice,only:__float2ll_rn
use cudadevice,only:__float2ll_ru
use cudadevice,only:__float2ll_rz
use cudadevice,only:__float2ull_rd
use cudadevice,only:__float2ull_rn
use cudadevice,only:__float2ull_ru
use cudadevice,only:__float2ull_rz
use cudadevice,only:__half2float
use cudadevice,only:double_as_longlong
use cudadevice,only:longlong_as_double
use cudadevice,only:__double2int_rd
use cudadevice,only:__double2int_rn
use cudadevice,only:__double2int_ru
use cudadevice,only:__double2int_rz
use cudadevice,only:__double2uint_rd
use cudadevice,only:__double2uint_rn
use cudadevice,only:__double2uint_ru
use cudadevice,only:__double2uint_rz
use cudadevice,only:__double2float_rn
use cudadevice,only:__double2float_rz
use cudadevice,only:__double2float_ru
use cudadevice,only:__double2float_rd
use cudadevice,only:__double2loint
use cudadevice,only:__double2hiint
use cudadevice,only:__hiloint2double
use cudadevice,only:__int2float_rd
use cudadevice,only:__int2float_rn
use cudadevice,only:__int2float_ru
use cudadevice,only:__int2float_rz
use cudadevice,only:__int2double_rn
use cudadevice,only:__uint2float_rd
use cudadevice,only:__uint2float_rn
use cudadevice,only:__uint2float_ru
use cudadevice,only:__uint2float_rz
use cudadevice,only:__uint2double_rn
use cudadevice,only:__double2ll_rd
use cudadevice,only:__double2ll_rn
use cudadevice,only:__double2ll_ru
use cudadevice,only:__double2ll_rz
use cudadevice,only:__double2ull_rd
use cudadevice,only:__double2ull_rn
use cudadevice,only:__double2ull_ru
use cudadevice,only:__double2ull_rz
use cudadevice,only:__ll2float_rd
use cudadevice,only:__ll2float_rn
use cudadevice,only:__ll2float_ru
use cudadevice,only:__ll2float_rz
use cudadevice,only:__ll2double_rd
use cudadevice,only:__ll2double_rn
use cudadevice,only:__ll2double_ru
use cudadevice,only:__ll2double_rz
use cudadevice,only:__ull2double_rd
use cudadevice,only:__ull2double_rn
use cudadevice,only:__ull2double_ru
use cudadevice,only:__ull2double_rz
use cudadevice,only:__ull2float_rd
use cudadevice,only:__ull2float_rn
use cudadevice,only:__ull2float_ru
use cudadevice,only:__ull2float_rz
use cudadevice,only:__mul24
use cudadevice,only:__umul24
use cudadevice,only:__drcp_rd
use cudadevice,only:__drcp_rn
use cudadevice,only:__drcp_ru
use cudadevice,only:__drcp_rz
use cudadevice,only:__dsqrt_rd
use cudadevice,only:__dsqrt_rn
use cudadevice,only:__dsqrt_ru
use cudadevice,only:__dsqrt_rz
use cudadevice,only:__ddiv_rn
use cudadevice,only:__ddiv_rz
use cudadevice,only:__ddiv_ru
use cudadevice,only:__ddiv_rd
use cudadevice,only:__clz
use cudadevice,only:__ffs
use cudadevice,only:__popc
use cudadevice,only:__brev
use cudadevice,only:atomicadd
use cudadevice,only:atomicaddvector
use cudadevice,only:atomicaddreal4x2
use cudadevice,only:atomicaddreal4x4
use cudadevice,only:atomicsub
use cudadevice,only:atomicmax
use cudadevice,only:atomicmin
use cudadevice,only:atomicand
use cudadevice,only:atomicor
use cudadevice,only:atomicinc
use cudadevice,only:atomicdec
use cudadevice,only:atomiccas
use cudadevice,only:atomicexch
use cudadevice,only:atomicxor
use cudadevice,only:match_all_sync
use cudadevice,only:match_any_sync
use cudadevice,only:all_sync
use cudadevice,only:any_sync
use cudadevice,only:ballot_sync
use cudadevice,only:__ldcg
use cudadevice,only:__ldca
use cudadevice,only:__ldcs
use cudadevice,only:__ldlu
use cudadevice,only:__ldcv
use cudadevice,only:__stwb
use cudadevice,only:__stcg
use cudadevice,only:__stcs
use cudadevice,only:__stwt
use cudadevice,only:barrier_arrive
use cudadevice,only:tma_bulk_load
use cudadevice,only:tma_bulk_store
type::cluster_group
type(c_devptr),private::handle
integer(4)::size
integer(4)::rank
end type
type::grid_group
type(c_devptr),private::handle
integer(4)::size
integer(4)::rank
end type
type::coalesced_group
type(c_devptr),private::handle
integer(4)::size
integer(4)::rank
end type
type::thread_group
type(c_devptr),private::handle
integer(4)::size
integer(4)::rank
end type
interface
attributes(device) function cluster_block_index()
use,intrinsic::cudadevice,only:dim3
type(dim3)::cluster_block_index
end
end interface
interface
attributes(device) function cluster_dim_blocks()
use,intrinsic::cudadevice,only:dim3
type(dim3)::cluster_dim_blocks
end
end interface
interface
attributes(device) function this_cluster()
import::cluster_group
type(cluster_group)::this_cluster
end
end interface
interface
attributes(device) function this_grid()
import::grid_group
type(grid_group)::this_grid
end
end interface
interface
attributes(device) function this_thread_block()
import::thread_group
type(thread_group)::this_thread_block
end
end interface
interface
attributes(device) function this_warp()
import::coalesced_group
type(coalesced_group)::this_warp
end
end interface
interface this_warp
procedure::this_warp
end interface
end
