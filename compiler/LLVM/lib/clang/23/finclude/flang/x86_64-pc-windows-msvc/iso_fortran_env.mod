!mod$ v1 sum:bcbe64e7ab8e05a7
!need$ d676699be0556005 n __fortran_builtins
!need$ d7d52c84f2903e96 n iso_fortran_env_impl
module iso_fortran_env
use __fortran_builtins,only:event_type=>__builtin_event_type
use __fortran_builtins,only:notify_type=>__builtin_notify_type
use __fortran_builtins,only:lock_type=>__builtin_lock_type
use __fortran_builtins,only:team_type=>__builtin_team_type
use __fortran_builtins,only:atomic_int_kind=>__builtin_atomic_int_kind
use __fortran_builtins,only:atomic_logical_kind=>__builtin_atomic_logical_kind
use __fortran_builtins,only:compiler_options=>__builtin_compiler_options
use __fortran_builtins,only:compiler_version=>__builtin_compiler_version
use iso_fortran_env_impl,only:selectedint8
use iso_fortran_env_impl,only:selectedint16
use iso_fortran_env_impl,only:selectedint32
use iso_fortran_env_impl,only:selectedint64
use iso_fortran_env_impl,only:selectedint128
use iso_fortran_env_impl,only:safeint8
use iso_fortran_env_impl,only:safeint16
use iso_fortran_env_impl,only:safeint32
use iso_fortran_env_impl,only:safeint64
use iso_fortran_env_impl,only:safeint128
use iso_fortran_env_impl,only:int8
use iso_fortran_env_impl,only:int16
use iso_fortran_env_impl,only:int32
use iso_fortran_env_impl,only:int64
use iso_fortran_env_impl,only:int128
use iso_fortran_env_impl,only:selecteduint8
use iso_fortran_env_impl,only:selecteduint16
use iso_fortran_env_impl,only:selecteduint32
use iso_fortran_env_impl,only:selecteduint64
use iso_fortran_env_impl,only:selecteduint128
use iso_fortran_env_impl,only:safeuint8
use iso_fortran_env_impl,only:safeuint16
use iso_fortran_env_impl,only:safeuint32
use iso_fortran_env_impl,only:safeuint64
use iso_fortran_env_impl,only:safeuint128
use iso_fortran_env_impl,only:uint8
use iso_fortran_env_impl,only:uint16
use iso_fortran_env_impl,only:uint32
use iso_fortran_env_impl,only:uint64
use iso_fortran_env_impl,only:uint128
use iso_fortran_env_impl,only:logical8
use iso_fortran_env_impl,only:logical16
use iso_fortran_env_impl,only:logical32
use iso_fortran_env_impl,only:logical64
use iso_fortran_env_impl,only:selectedreal16
use iso_fortran_env_impl,only:selectedbfloat16
use iso_fortran_env_impl,only:selectedreal32
use iso_fortran_env_impl,only:selectedreal64
use iso_fortran_env_impl,only:selectedreal80
use iso_fortran_env_impl,only:selectedreal64x2
use iso_fortran_env_impl,only:selectedreal128
use iso_fortran_env_impl,only:safereal16
use iso_fortran_env_impl,only:safebfloat16
use iso_fortran_env_impl,only:safereal32
use iso_fortran_env_impl,only:safereal64
use iso_fortran_env_impl,only:safereal80
use iso_fortran_env_impl,only:safereal64x2
use iso_fortran_env_impl,only:safereal128
use iso_fortran_env_impl,only:real16
use iso_fortran_env_impl,only:bfloat16
use iso_fortran_env_impl,only:real32
use iso_fortran_env_impl,only:real64
use iso_fortran_env_impl,only:real80
use iso_fortran_env_impl,only:real64x2
use iso_fortran_env_impl,only:real128
use iso_fortran_env_impl,only:integer_kinds=>__builtin_integer_kinds
use iso_fortran_env_impl,only:real_kinds=>__builtin_real_kinds
use iso_fortran_env_impl,only:logical_kinds=>__builtin_logical_kinds
integer(4),parameter,private::selectedascii=1_4
intrinsic::selected_char_kind
private::selected_char_kind
integer(4),parameter,private::selecteducs_2=2_4
integer(4),parameter,private::selectedunicode=4_4
integer(4),parameter::character_kinds(1_8:*)=[INTEGER(4)::1_4,2_4,4_4]
intrinsic::pack
private::pack
integer(4),parameter::current_team=-1_4
integer(4),parameter::initial_team=-2_4
integer(4),parameter::parent_team=-3_4
integer(4),parameter::character_storage_size=8_4
integer(4),parameter::file_storage_size=8_4
intrinsic::__builtin_numeric_storage_size
private::__builtin_numeric_storage_size
integer(4),parameter::numeric_storage_size=__builtin_numeric_storage_size()
integer(4),parameter::output_unit=6_4
integer(4),parameter::input_unit=5_4
integer(4),parameter::error_unit=0_4
integer(4),parameter::iostat_end=(-1_4)
integer(4),parameter::iostat_eor=(-2_4)
integer(4),parameter::iostat_inquire_internal_unit=256_4
integer(4),parameter::stat_failed_image=101_4
integer(4),parameter::stat_locked=102_4
integer(4),parameter::stat_locked_other_image=103_4
integer(4),parameter::stat_stopped_image=104_4
integer(4),parameter::stat_unlocked=105_4
integer(4),parameter::stat_unlocked_failed_image=106_4
end
