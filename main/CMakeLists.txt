set(TARGET_NAME "main")

set(SOURCES 
    main.c
    bch15_5.c
    isaac.c
    qrdec.c
    scanner.c
    image.c
    refcnt.c
    util.c
    binarize.c
    decoder.c
    qrdectxt.c
    svg.c
    img_scanner.c
    qr_finder.c
    rs.c error.c
    symbol.c
)

add_exec_vxp(${TARGET_NAME} 
    SRCS ${SOURCES}
    RESOURCES resources
    APP_NAME "${APP_NAME}"
    APPID "${APPID}"
    BACKGROUND ${BACKGROUND}
    API "${API}"
    RAM "${RAM}"
)

include_directories(${MRE_SDK}/include/service ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(${TARGET_NAME} mreapi)

target_compile_definitions(${TARGET_NAME}
    PRIVATE __MRE_SDK__)

target_compile_options(${TARGET_NAME}
    PRIVATE -fpermissive)
