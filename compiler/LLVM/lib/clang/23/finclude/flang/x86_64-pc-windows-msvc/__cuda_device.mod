!mod$ v1 sum:6abd6bc56a20510c
!need$ f399daef6cce6e22 n __cuda_builtins
module __cuda_device
use __cuda_builtins,only:threadidx
use __cuda_builtins,only:blockdim
use __cuda_builtins,only:blockidx
use __cuda_builtins,only:griddim
use __cuda_builtins,only:warpsize
end
