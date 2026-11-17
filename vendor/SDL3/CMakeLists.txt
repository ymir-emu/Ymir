set(SDL_STATIC ON)
set(SDL_SHARED OFF)

set(SDL_ASSEMBLY ON)
if ("x86_64" IN_LIST ARCHITECTURES)
	set(SDL_MMX ON)
	set(SDL_SSE ON)
	set(SDL_SSE2 ON)
	set(SDL_SSE3 ${Ymir_AVX2})
	set(SDL_SSE4_1 ${Ymir_AVX2})
	set(SDL_SSE4_2 ${Ymir_AVX2})
	set(SDL_AVX ${Ymir_AVX2})
	set(SDL_AVX2 ${Ymir_AVX2})
	set(SDL_AVX512F OFF)  # not supported yet
endif ()

if ("arm64" IN_LIST ARCHITECTURES)
	set(SDL_ARMNEON ON)
endif ()

add_subdirectory(SDL)
