# Release packages prioritize size, including statically linked dependencies.
# Keep normal floating-point semantics, exception handling and legacy runtime.
if(NOT MSVC)
  message(FATAL_ERROR "The size-first Release profile requires MSVC")
endif()
set(TTP_SIZE_INLINE_LEVEL "2" CACHE STRING "Release inlining level (0, 1 or 2)")
set_property(CACHE TTP_SIZE_INLINE_LEVEL PROPERTY STRINGS 0 1 2)
if(NOT TTP_SIZE_INLINE_LEVEL MATCHES "^[012]$")
  message(FATAL_ERROR "TTP_SIZE_INLINE_LEVEL must be 0, 1 or 2")
endif()
foreach(_language C CXX)
  set(CMAKE_${_language}_FLAGS_RELEASE "/O1 /Ob${TTP_SIZE_INLINE_LEVEL} /Os /Gy /Gw /GF /DNDEBUG"
    CACHE STRING "Size-first Release flags" FORCE)
endforeach()
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
add_compile_options("$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:CXX>>:/GR->")
add_link_options("$<$<CONFIG:Release>:/OPT:REF>" "$<$<CONFIG:Release>:/OPT:ICF>"
  "$<$<CONFIG:Release>:/INCREMENTAL:NO>" "$<$<CONFIG:Release>:/DEBUG:NONE>")
