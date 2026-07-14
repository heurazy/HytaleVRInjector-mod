if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

set(controller_types
    oculus_touch
    knuckles
    vive_controller
    holographic_controller
    hpmotioncontroller
    vive_cosmos_controller
)
set(binding_files
    hytalevr_bindings_oculus_touch.json
    hytalevr_bindings_knuckles.json
    hytalevr_bindings_vive_controller.json
    hytalevr_bindings_holographic_controller.json
    hytalevr_bindings_hpmotioncontroller.json
    hytalevr_bindings_vive_cosmos_controller.json
)

file(READ "${PROJECT_SOURCE_DIR}/hytalevr_actions.json" action_manifest)
string(JSON manifest_binding_count LENGTH "${action_manifest}" default_bindings)

list(LENGTH controller_types expected_binding_count)
if(NOT manifest_binding_count EQUAL expected_binding_count)
    message(FATAL_ERROR
        "Expected ${expected_binding_count} default bindings, found ${manifest_binding_count}")
endif()

math(EXPR last_binding_index "${expected_binding_count} - 1")
foreach(index RANGE ${last_binding_index})
    list(GET controller_types ${index} expected_type)
    list(GET binding_files ${index} expected_file)

    string(JSON manifest_type GET
        "${action_manifest}" default_bindings ${index} controller_type)
    string(JSON manifest_file GET
        "${action_manifest}" default_bindings ${index} binding_url)
    if(NOT manifest_type STREQUAL expected_type OR
       NOT manifest_file STREQUAL expected_file)
        message(FATAL_ERROR
            "Binding ${index} is ${manifest_type}/${manifest_file}; expected ${expected_type}/${expected_file}")
    endif()

    file(READ "${PROJECT_SOURCE_DIR}/${expected_file}" binding_json)
    string(JSON binding_type GET "${binding_json}" controller_type)
    string(JSON action_set_type TYPE "${binding_json}" bindings /actions/hytale)
    if(NOT binding_type STREQUAL expected_type)
        message(FATAL_ERROR
            "${expected_file} declares controller_type ${binding_type}; expected ${expected_type}")
    endif()
    if(NOT action_set_type STREQUAL "OBJECT")
        message(FATAL_ERROR "${expected_file} does not define /actions/hytale")
    endif()
endforeach()
