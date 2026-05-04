if(TARGET toupcam)
    return()
endif()

get_filename_component(TOUPCAM_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)

add_library(toupcam SHARED IMPORTED)

set_target_properties(toupcam PROPERTIES
    IMPORTED_IMPLIB "${TOUPCAM_SDK_DIR}/win/x64/toupcam.lib"
    IMPORTED_LOCATION "${TOUPCAM_SDK_DIR}/win/x64/toupcam.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${TOUPCAM_SDK_DIR}/inc"
)

set(TOUPCAM_DLL "${TOUPCAM_SDK_DIR}/win/x64/toupcam.dll")
