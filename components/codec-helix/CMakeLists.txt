cmake_minimum_required(VERSION 3.16)

if (DEFINED ESP_PLATFORM)
  idf_component_register(
      SRC_DIRS "src/libhelix-aac"
      INCLUDE_DIRS "src" "src/libhelix-aac"
  )

  target_compile_options(${COMPONENT_LIB} PUBLIC 
      -Wno-error 
      -Wno-format 
      -Wno-unused-variable 
      -Wno-unused-but-set-variable
      -DESP32 
      -DUSE_DEFAULT_STDLIB
  )
endif()
