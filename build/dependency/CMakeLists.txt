include(${CMAKE_BINARY_DIR}/_deps/xcmake_tools/Common.cmake)

# Fetch dependencies
FetchAndPopulate("https://github.com/LIONant-depot/xbits.git" "main")
FetchAndPopulate("https://github.com/LIONant-depot/xserializer.git" "main")
FetchAndPopulate("https://github.com/LIONant-depot/xresource_pipeline_v2.git" "main")
FetchAndPopulate("https://github.com/LIONant-depot/xstrtool.git" "main" )
FetchAndPopulate("https://github.com/LIONant-depot/xscheduler.git" "main")
FetchAndPopulate("https://github.com/LIONant-depot/xAnimation.git" "main")


DefineInterfaceComponent(xretarget_compiler_ "dependencies"
  "source/xretarget_details.h"
  "source/xretarget_descriptor.h"
  "source/xretarget_mapping.h"
  "source/xretarget.h"
  "source/compiler/xretarget_compiler.cpp"
  "source/compiler/xretarget_compiler.h"
)
