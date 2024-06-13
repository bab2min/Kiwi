add_executable( "${PROJECT_NAME}-wasm"
  kiwi_wasm.cpp
)

target_link_libraries( "${PROJECT_NAME}-wasm"
  "${PROJECT_NAME}_static"
)

set_target_properties("${PROJECT_NAME}-wasm" PROPERTIES
  LINK_FLAGS "--bind -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -s EXPORT_ES6=1 -s MODULARIZE=1 -s EXPORT_NAME=kiwi -s 'EXPORTED_RUNTIME_METHODS=[\"FS\"]'"
)
