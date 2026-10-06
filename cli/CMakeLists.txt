
set(SOURCE
		include/chiaki-cli.h
		src/discover.c
		src/wakeup.c
		src/stream.c)

add_library(chiaki-cli-lib STATIC ${SOURCE})
target_include_directories(chiaki-cli-lib PUBLIC "include")
target_link_libraries(chiaki-cli-lib chiaki-lib)

if(CHIAKI_LIB_JSONC_EXTERNAL_PROJECT)
	target_link_libraries(chiaki-cli-lib json-c)
	target_include_directories(chiaki-cli-lib PRIVATE "${JSONC_PREFIX_INCLUDE_DIR}")
	target_include_directories(chiaki-cli-lib PRIVATE "${json-c_BINARY_DIR}")
elseif(CHIAKI_IS_SWITCH)
	find_library(JSONC_LIB json-c ${PORTLIBS}/lib)
	target_link_libraries(chiaki-cli-lib ${JSONC_LIB})
	target_include_directories(chiaki-cli-lib PUBLIC "${PORTLIBS}/include/json-c")
elseif(TARGET PkgConfig::json-c)
	target_link_libraries(chiaki-cli-lib PkgConfig::json-c)
endif()

if(CHIAKI_ENABLE_FFMPEG_DECODER)
	target_compile_definitions(chiaki-cli-lib PRIVATE CHIAKI_ENABLE_FFMPEG_DECODER=1)
	if(TARGET FFMPEG::swscale)
		target_link_libraries(chiaki-cli-lib FFMPEG::swscale)
	endif()
	if(TARGET FFMPEG::avcodec)
		target_link_libraries(chiaki-cli-lib FFMPEG::avcodec)
	endif()
	if(TARGET FFMPEG::avutil)
		target_link_libraries(chiaki-cli-lib FFMPEG::avutil)
	endif()
endif()

if(CHIAKI_CLI_ARGP_STANDALONE)
	find_package(Argp REQUIRED)
	target_link_libraries(chiaki-cli-lib Argp::Argp)
endif()

add_executable(chiaki-cli src/main.c)
target_link_libraries(chiaki-cli chiaki-cli-lib)
install(TARGETS chiaki-cli)
