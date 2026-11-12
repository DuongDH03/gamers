# Applies the constitution's compiler rules to one of our own targets.
# Third-party code (SDL3) never goes through this function, so its warnings
# cannot break our build.
function(scraps_target_defaults target)
  target_compile_features(${target} PUBLIC cxx_std_17)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()
