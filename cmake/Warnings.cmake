# Warning and floating-point flags shared by every WarpForge target.
#
# -ffp-contract=off / /fp:precise matter for correctness, not style: the
# constant folder evaluates f32 arithmetic on the host and must get exactly
# the bits the GPU produces with add.rn/mul.rn. Letting the host compiler fuse
# a*b+c into an FMA would make the folder disagree with the hardware.
function(warpforge_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /fp:precise /utf-8)
    if(WARPFORGE_WERROR)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
      -ffp-contract=off)
    if(WARPFORGE_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()
