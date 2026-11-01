!mod$ v1 sum:f399daef6cce6e22
!need$ d676699be0556005 n __fortran_builtins
module __cuda_builtins
use __fortran_builtins,only:threadidx=>__builtin_threadidx
use __fortran_builtins,only:blockdim=>__builtin_blockdim
use __fortran_builtins,only:blockidx=>__builtin_blockidx
use __fortran_builtins,only:griddim=>__builtin_griddim
use __fortran_builtins,only:warpsize=>__builtin_warpsize
end
