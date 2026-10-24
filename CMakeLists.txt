cmake_minimum_required(VERSION 3.23)
# Speed Test: times every TLS handshake method Notion Classic and Claude 68
# have used. The engine and every method come straight from Claude 68's
# tree (CLAUDE68_DIR), nothing copied, so the test runs the apps' own code.
project(SpeedTest)

set(CMAKE_C_STANDARD 99)
enable_language(ASM)

# No -m68020 here (it would pick Retro68's FPU runtime, see Claude 68);
# the curve code gets it per file below.
SET(CMAKE_C_FLAGS   "-Os")
SET(CMAKE_CXX_FLAGS "-Os -fno-exceptions -fno-rtti")

set(CLAUDE68_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Claude68" CACHE PATH "Claude 68 source tree")
set(OTFIX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/OpenTransportBugPatcher" CACHE PATH "Open Transport Bug Patcher source tree")
set(CORE ${CLAUDE68_DIR}/src/core)
set(NET  ${CORE}/net)

add_definitions(-DSSL_ENABLED)
include_directories(src src/variants ${NET} ${CORE}/lib/mbedtls/include ${CORE}/lib ${OTFIX_DIR}/src)

# ── micro-ecc builds (src/variants/uecc_*.c) ─────────────────────────
# The settings every build shares, as Claude 68's UECC_FLAGS
set(UECC "-m68020 -DuECC_OPTIMIZATION_LEVEL=2 -DuECC_SQUARE_FUNC=1 -DuECC_WORD_SIZE=4 \
 -DuECC_SUPPORTS_secp256r1=1 -DuECC_SUPPORTS_secp160r1=0 -DuECC_SUPPORTS_secp192r1=0 \
 -DuECC_SUPPORTS_secp224r1=0 -DuECC_SUPPORTS_secp256k1=0 -DuECC_VLI_NATIVE_LITTLE_ENDIAN=0")

set_source_files_properties(src/variants/uecc_c_os.c     PROPERTIES COMPILE_FLAGS "${UECC} -Os")
set_source_files_properties(src/variants/uecc_c_o3.c     PROPERTIES COMPILE_FLAGS "${UECC} -O3")
set_source_files_properties(src/variants/uecc_unrolled.c PROPERTIES COMPILE_FLAGS "${UECC} -O3 -Dasm_mult=1")
set_source_files_properties(src/variants/uecc_karatsuba.c PROPERTIES COMPILE_FLAGS "${UECC} -O3 -Dasm_mult=1")
set_source_files_properties(src/variants/karatsuba.c     PROPERTIES COMPILE_FLAGS "-m68020 -O3")
set_source_files_properties(src/variants/uecc_comba_c.c  PROPERTIES COMPILE_FLAGS
    "${UECC} -O3 -Dasm_mult=1 -Dasm_square=1")
set_source_files_properties(src/variants/uecc_comba.c    PROPERTIES COMPILE_FLAGS
    "${UECC} -O3 -Dasm_mult=1 -Dasm_square=1 -Dasm_solinas_p256=1")

# ── X25519 builds ────────────────────────────────────────────────────
set_source_files_properties(src/variants/x25519_donna_c.c src/variants/x25519_donna_asm.c
    src/variants/x25519_donna_fused.c PROPERTIES COMPILE_FLAGS "-m68020 -O3")
set_source_files_properties(src/variants/x25519_sat.c PROPERTIES COMPILE_FLAGS "-m68020 -O3 -Dsat_vli_asm=1")

# ── Assembly ─────────────────────────────────────────────────────────
# Claude 68's .s files, copied with their global symbols given the prefix
# of the micro-ecc build they belong to (see src/variants/uecc_rename.h)
set(ASM_SYMBOLS uECC_vli_mult uECC_vli_square solinas_p256_columns_m68k)
function(prefixed_asm src prefix out_var)
    get_filename_component(name ${src} NAME)
    set(out ${CMAKE_CURRENT_BINARY_DIR}/asm/${prefix}${name})
    file(READ ${src} text)
    foreach(sym ${ASM_SYMBOLS})
        string(REGEX REPLACE "([^A-Za-z0-9_])${sym}([^A-Za-z0-9_])" "\\1${prefix}${sym}\\2" text "${text}")
    endforeach()
    file(WRITE ${out} "${text}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${src})
    set(${out_var} ${out} PARENT_SCOPE)
endfunction()

prefixed_asm(${NET}/vli_mult_comba_m68k.s   vCmb_ ASM_CMB_MULT)
prefixed_asm(${NET}/vli_square_comba_m68k.s vCmb_ ASM_CMB_SQUARE)
prefixed_asm(${NET}/solinas_p256_m68k.s     vCmb_ ASM_CMB_SOLINAS)
prefixed_asm(${NET}/vli_mult_comba_m68k.s   vCmc_ ASM_CMC_MULT)
prefixed_asm(${NET}/vli_square_comba_m68k.s vCmc_ ASM_CMC_SQUARE)
prefixed_asm(${CMAKE_CURRENT_SOURCE_DIR}/src/variants/unused_vli_mult_m68k.s vUnr_ ASM_UNR_MULT)
set(ASM_SOURCES
    ${ASM_CMB_MULT} ${ASM_CMB_SQUARE} ${ASM_CMB_SOLINAS} ${ASM_CMC_MULT} ${ASM_CMC_SQUARE} ${ASM_UNR_MULT}
    src/variants/unused_donna_m68k.s
    src/variants/unused_donna_fused_m68k.s)
set_source_files_properties(${ASM_SOURCES} PROPERTIES COMPILE_FLAGS "-m68020")

# ── The app ──────────────────────────────────────────────────────────
add_application(SpeedTest
    src/main.cpp
    src/log.cpp
    src/tests.cpp
    src/bench_alt.c
    src/build_stamp.c
    src/variants/uecc_c_os.c
    src/variants/uecc_c_o3.c
    src/variants/uecc_unrolled.c
    src/variants/uecc_karatsuba.c
    src/variants/karatsuba.c
    src/variants/uecc_comba_c.c
    src/variants/uecc_comba.c
    src/variants/x25519_donna_c.c
    src/variants/x25519_donna_asm.c
    src/variants/x25519_donna_fused.c
    src/variants/x25519_sat.c
    ${ASM_SOURCES}
    # Claude 68's engine, unchanged
    ${NET}/net_sockets_mac.c
    ${NET}/fast_alloc.c
    ${NET}/mac_entropy.c
    ${NET}/http_parser.c
    ${NET}/RootCert.cpp
    # Open Transport Bug Patcher's check, for the header of each run
    ${OTFIX_DIR}/src/patch.c
    src/resources.r
    CREATOR "SpTs"
)

# App objects link first, so with --allow-multiple-definition the hooks in
# bench_alt.c and the pool allocator in fast_alloc.c win, as in Claude 68.
target_link_libraries(SpeedTest
    ${CORE}/lib/mbedtls/libmbedtls.a
    ${CORE}/lib/mbedtls/libmbedx509.a
    ${CORE}/lib/mbedtls/libmbedcrypto.a
    ${CORE}/lib/mactcp/libMacTCPHelper.a)

set_target_properties(SpeedTest PROPERTIES
    LINK_FLAGS "-Wl,--allow-multiple-definition -Wl,-gc-sections -Wl,-Map,SpeedTest.map")

add_custom_target(build_appl ALL DEPENDS SpeedTest_APPL)

# Touch build_stamp.c before every build, so the build number in the
# results file's name ("SP - 10241725 - 16-bit") is the time of the build
add_custom_target(build_stamp_touch
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_SOURCE_DIR}/src/build_stamp.c
)
add_dependencies(SpeedTest build_stamp_touch)

# Retro68 leaves the Finder flags at 0; turn on hasBundle and hasCustomIcon so
# the Finder shows the app's icon, from the .APPL, .bin and .dsk alike.
add_custom_command(TARGET SpeedTest_APPL POST_BUILD
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/set_finder_flags.py ${CMAKE_CURRENT_BINARY_DIR} SpeedTest
    COMMENT "Setting the Finder flags")
